// Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
// Hydrographic Center, University of New Hampshire
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "marine_bathymetry_store/tile_io.hpp"

#include <sys/stat.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "geographic_msgs/msg/geo_point.hpp"

#include "marine_tiled_raster_store/tile_io.hpp"

/// @file
/// @brief Bathymetry persistence (ADR-0002 §D5, simplified by #248): the
/// multi-layer, SourceLayer subdirectory save/load, delegating each tile's
/// GeoTIFF read/write to the generic `marine_tiled_raster_store::saveTile` /
/// `loadTile` (#172). Each GGGS tile persists as a single 2-band `Float64` value
/// tile (depth, uncertainty; NaN no-data on both) — the pre-#248 `_time` /
/// `_source` companions were dropped (Amendment A2.2). A store-wide
/// `registry.json` sidecar holds coarse `StoreMetadata` (ADR-0005 #248).

namespace marine_bathymetry_store
{

namespace
{
/// Per-band no-data for the 2-band Float64 value tile: NaN on both depth and
/// uncertainty. One entry per band, in band order.
const std::vector<std::optional<double>> & valueBandNoData()
{
  static const double nan = std::numeric_limits<double>::quiet_NaN();
  static const std::vector<std::optional<double>> nodata{nan, nan};
  return nodata;
}

/// Reconstruct a `GridIndex` from a tile filename stem `<level>_<row>_<col>`
/// without opening the file (called before the GDAL I/O to gate overlap checks).
///
/// Strategy (must-fix from plan-review): `GridIndex(level, row, col)` is a
/// private constructor only accessible to `gggs::Level`, `GridAreaIterator`, and
/// `GridBounds`.  We derive (southLat, westLon) from the parsed (row, col), nudge
/// them half a cell inward to stay strictly inside the tile, and round-trip
/// through `gggs::Level(lvl).gridIndex()` which calls the private constructor
/// via the Level friend relationship.
///
/// @param filename Basename (with or without `.tif`) of the form
///        `<level>_<row>_<col>[.tif]`.
/// @throws std::runtime_error if the filename cannot be parsed.
gggs::GridIndex gridIndexFromTileFilename(const std::string & filename)
{
  namespace fs = std::filesystem;
  const std::string stem = fs::path(filename).stem().string();
  // Parse "<level>_<row>_<col>" from the stem.
  const auto first_under = stem.find('_');
  if (first_under == std::string::npos || first_under == 0) {
    throw std::runtime_error(
            "gridIndexFromTileFilename: cannot parse level from '" + filename + "'");
  }
  const auto second_under = stem.find('_', first_under + 1);
  if (second_under == std::string::npos || second_under == first_under + 1) {
    throw std::runtime_error(
            "gridIndexFromTileFilename: cannot parse row from '" + filename + "'");
  }
  const std::string level_str = stem.substr(0, first_under);
  const std::string row_str = stem.substr(first_under + 1, second_under - first_under - 1);
  const std::string col_str = stem.substr(second_under + 1);

  // Validate digits up front so the documented `std::runtime_error` contract
  // holds (raw std::stoul would throw std::invalid_argument/std::out_of_range),
  // then wrap the conversion to rethrow as runtime_error on overflow.
  const auto all_digits = [](const std::string & s) {
      return !s.empty() && s.find_first_not_of("0123456789") == std::string::npos;
    };
  if (!all_digits(level_str) || !all_digits(row_str) || !all_digits(col_str)) {
    throw std::runtime_error(
            "gridIndexFromTileFilename: non-numeric level/row/col in '" + filename + "'");
  }
  std::uint64_t lvl_raw = 0, row_raw = 0, col_raw = 0;
  try {
    lvl_raw = std::stoull(level_str);
    row_raw = std::stoull(row_str);
    col_raw = std::stoull(col_str);
  } catch (const std::out_of_range &) {
    throw std::runtime_error(
            "gridIndexFromTileFilename: level/row/col out of range in '" + filename + "'");
  }
  // Range-check the level BEFORE indexing the fixed-size `gggs::levels` table
  // (out-of-bounds operator[] is UB; a stray name like `99_0_0.tif` would hit it).
  if (lvl_raw >= gggs::levels.size()) {
    throw std::runtime_error(
            "gridIndexFromTileFilename: level " + std::to_string(lvl_raw) +
            " out of range in '" + filename + "'");
  }
  const uint8_t lvl = static_cast<uint8_t>(lvl_raw);
  const uint32_t row = static_cast<uint32_t>(row_raw);
  const uint32_t col = static_cast<uint32_t>(col_raw);

  // Derive the tile's south-west corner from (level, row, col) using the
  // precomputed LevelSpecs table (same formulas as GridIndex::southLatitude()
  // and GridIndex::westLongitude()).
  const gggs::LevelSpecs & spec = gggs::levels[lvl];
  const double south_lat = -96.0 + row * spec.grid_angular_span;
  const double west_lon = -180.0 + col * spec.gridLongitudinalSpan(row);

  // Nudge half a cell-span inward so the query point is strictly inside the
  // tile's geographic AABB even after floating-point rounding.
  const double epsilon = spec.cell_angular_span * 0.5;
  return gggs::Level(lvl).gridIndex(south_lat + epsilon, west_lon + epsilon);
}

/// Return `true` when tile @p grid's geographic AABB intersects the bounding
/// box [@p min_pt … @p max_pt] (both inclusive — a tile whose edge exactly
/// touches the box boundary is treated as overlapping).
///
/// Uses `GridIndex::{south,north}Latitude()` and `{west,east}Longitude()` for
/// the AABB, which are correct at any GGGS level without a `GridAreaIterator`
/// (ADR-0002 §D2 multi-level).
bool tileOverlapsBox(
  const gggs::GridIndex & grid,
  const geographic_msgs::msg::GeoPoint & min_pt,
  const geographic_msgs::msg::GeoPoint & max_pt)
{
  // Axis-aligned rectangle intersection: disjoint if one box is entirely to the
  // left, right, above, or below the other.
  if (grid.northLatitude() < min_pt.latitude) {return false;}
  if (grid.southLatitude() > max_pt.latitude) {return false;}
  if (grid.eastLongitude() < min_pt.longitude) {return false;}
  if (grid.westLongitude() > max_pt.longitude) {return false;}
  return true;
}

/// @brief Warn when a store directory holds content but exposes no recognized
/// `draft/`/`processed/`/`reference/`/`chart/` layer subdirectory — a foreign or
/// old-layout store from which NOTHING loads.
///
/// This is a navigation-safety *observability* guard: `load()`/`loadWindow()`
/// skip a layer whose subdir is absent, so a store whose only tiles live under an
/// unrecognized name loads as **empty** without error. A lone legacy `survey/` is
/// NOT such a case — it auto-migrates to `processed/` before this check runs (D8,
/// see migrateLegacySurveyDir), so it never reaches here as "unrecognized".
/// With `BathymetryLayer`'s `unsurveyed_is_lethal_ == false` default, an empty
/// bathy prior reads as *navigable*, so a silently-empty load must not pass
/// unnoticed. Matches the store's existing stale-subdir WARNING idiom.
///
/// Stays silent for a genuinely fresh/absent store (a missing dir, or one
/// holding only the `registry.json` metadata sidecar and no tiles yet):
/// "content" here means a subdirectory or a top-level `.tif`, i.e. tile data
/// that the recognized-layer walk could not reach.
void warnIfUnrecognizedStoreLayout(const std::string & dir, bool any_layer_dir_present)
{
  namespace fs = std::filesystem;
  if (any_layer_dir_present) {
    return;
  }
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    return;   // fresh/absent store — nothing to warn about
  }
  bool has_store_content = false;
  for (const auto & entry : fs::directory_iterator(dir, ec)) {
    if (entry.is_directory() ||
      (entry.is_regular_file() && entry.path().extension() == ".tif"))
    {
      has_store_content = true;
      break;
    }
  }
  if (!has_store_content) {
    return;   // empty store, or metadata-only sidecar — legitimately nothing loaded
  }
  std::cerr << "[marine_bathymetry_store] WARNING: store directory '" << dir
            << "' has content but no recognized 'draft/', 'processed/', "
            << "'reference/', or 'chart/' layer subdirectory — NOTHING was loaded. "
            << "A legacy 'survey/' auto-migrates to 'processed/' (ADR-0010 D8), so "
            << "this is a foreign or otherwise old-layout store; regenerate it. An "
            << "empty bathy prior reads as unsurveyed (navigable unless "
            << "unsurveyed_is_lethal is set).\n";
}

/// @brief True if @p filename is a dropped pre-#248 companion raster
/// (`*_time.tif` / `*_source.tif`).
///
/// The per-cell `_time`/`_source` companions were dropped by #248
/// (Amendment A2.2). One may still linger inside a new-layout layer dir on a
/// store written by old code; it is not a 2-band value tile and must be skipped
/// rather than fed to `loadTile()`, which would throw and abort the whole load.
/// (Value tiles are `<level>_<row>_<col>.tif` — all-numeric stems — so there is
/// no collision with these non-numeric suffixes.)
bool isDroppedCompanionTile(const std::string & filename)
{
  const auto hasSuffix = [&filename](const std::string & suffix) {
      return filename.size() >= suffix.size() &&
             filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
  return hasSuffix("_time.tif") || hasSuffix("_source.tif");
}

/// @brief True if @p dir_name is the depth overview-pyramid sidecar directory or
///        one of its crash-safe-swap transients (`overviews.tmp/`/`overviews.old/`).
///
/// The `build_depth_overviews` builder (ADR-0010 D9 / ADR-0011) writes the flat
/// `overviews/` sidecar next to a `draft/`/`processed/` layer's fine tiles, plus
/// the two staging siblings the crash-safe rename-aside swap leaves transiently
/// (`overviews.tmp/` while building or as crashed-run debris, `overviews.old/` in
/// the mid-swap window). These are a known, expected part of a depth layer dir —
/// the flat-layout scan below must skip them **silently**, not warn as if they were
/// stale epoch-layout tiles (ADR-0011 Consequences: the loader fix that lands with
/// the pyramid). Any OTHER subdirectory is still unexpected and still warns.
bool isOverviewSidecarDir(const std::string & dir_name)
{
  return dir_name == "overviews" || dir_name == "overviews.tmp" ||
         dir_name == "overviews.old";
}

/// @brief Auto-migrate a legacy `survey/` layer dir to `processed/` (ADR-0010 D8).
///
/// The pre-D8 fused `survey/` layer carries no per-cell live-vs-re-run provenance
/// (A2 dropped it), so there is nothing to split — it is re-classified wholesale
/// to `processed/` (ADR-0010 D1/D8: the Massabesic stores were regenerated via the
/// authoritative `import_bag` path, so `processed/` is accurate; `draft/` starts
/// empty). Called by BOTH `load()` and `loadWindow()` before the layer scan so an
/// existing on-disk store opens transparently under the new taxonomy.
///
/// Mechanism (operator decision, 2026-08-20): a **single same-filesystem
/// `rename(survey/ → processed/)`** — the atomic commit point. `survey/` and
/// `processed/` are siblings under @p dir, so the rename is always intra-filesystem
/// and cannot hit EXDEV. The rename is the sole state change; there is no
/// layer-keyed registry/metadata to update (`registry.json` is a store-root
/// sidecar, untouched by the layer rename), so re-opening a migrated store is
/// naturally idempotent — the second open finds only `processed/` and this is a
/// no-op.
///
/// Refuses **loudly** (throws) only if BOTH `survey/` and `processed/` exist: that
/// is an ambiguous half-migrated or hand-edited store, and silently merging or
/// picking one could lose the authoritative surface. The operator must resolve it.
///
/// `save()` and `evictOutside()` deliberately do NOT migrate: they operate on an
/// already-migrated / freshly-written store and never touch a legacy `survey/` dir.
void migrateLegacySurveyDir(const std::string & dir)
{
  namespace fs = std::filesystem;
  const fs::path survey = fs::path(dir) / "survey";
  const fs::path processed = fs::path(dir) / layerDirName(SourceLayer::Processed);
  // Separate error_codes per probe: a single shared `ec` would carry only the
  // LAST call's state, so anyone who later reads it to see WHICH path failed to
  // stat would be misled. Each is_directory() records its own path's outcome. A
  // stat error is treated as "absent" either way (the safe default — a store we
  // cannot even stat is not one we silently rename).
  std::error_code survey_ec;
  std::error_code processed_ec;
  const bool has_survey = fs::is_directory(survey, survey_ec);
  const bool has_processed = fs::is_directory(processed, processed_ec);
  if (!has_survey) {
    return;   // nothing to migrate (fresh/new-layout store)
  }
  // A symlinked `survey/` slipped past is_directory() above — it follows links —
  // but rename() moves the LINK itself, leaving `processed/` a symlink pointing
  // out of the store: the same out-of-store-link hazard replaceChartLayer refuses.
  // Migration must adopt a real directory, not a link's target, so refuse loudly.
  std::error_code symlink_ec;
  if (fs::is_symlink(survey, symlink_ec)) {
    throw std::runtime_error(
            "marine_bathymetry_store: legacy 'survey/' in store '" + dir +
            "' is a symlink — refusing to auto-migrate it to 'processed/' (ADR-0010 "
            "D8). A symlinked layer would make 'processed/' point outside the store; "
            "resolve by hand (replace the link with a real directory, or migrate its "
            "target).");
  }
  if (has_processed) {
    throw std::runtime_error(
            "marine_bathymetry_store: store '" + dir + "' has BOTH a legacy 'survey/' "
            "and a 'processed/' layer dir — refusing to auto-migrate an ambiguous "
            "store (ADR-0010 D8). Resolve by hand: 'survey/' is the pre-D8 fused "
            "layer, re-classified wholesale to 'processed/'; merge or remove one.");
  }
  // The atomic commit point: a single intra-filesystem rename. Use the
  // error_code overload and re-throw as std::runtime_error so a rename failure
  // surfaces through the SAME "refuse loudly" idiom as the both-exist guard above
  // (the throwing overload would raise std::filesystem_error, a different type
  // callers would have to catch separately).
  std::error_code rename_ec;
  fs::rename(survey, processed, rename_ec);
  if (rename_ec) {
    throw std::runtime_error(
            "marine_bathymetry_store: failed to auto-migrate legacy 'survey/' to "
            "'processed/' in store '" + dir + "' (ADR-0010 D8): " + rename_ec.message());
  }
  std::cerr << "[marine_bathymetry_store] migrated legacy 'survey/' layer to "
            << "'processed/' in '" << dir << "' (ADR-0010 D8: the pre-D8 fused "
            << "layer is the authoritative import_bag re-run; draft/ starts empty).\n";
}

}  // namespace

uint8_t levelFromTileFilename(const std::string & filename)
{
  namespace fs = std::filesystem;
  const std::string stem = fs::path(filename).stem().string();
  const auto underscore = stem.find('_');
  if (underscore == std::string::npos || underscore == 0) {
    throw std::runtime_error(
            "levelFromTileFilename: no level prefix in tile filename '" + filename + "'");
  }
  const std::string level_str = stem.substr(0, underscore);
  for (const char c : level_str) {
    if (c < '0' || c > '9') {
      throw std::runtime_error(
              "levelFromTileFilename: non-numeric level prefix in '" + filename + "'");
    }
  }
  const int level = std::stoi(level_str);
  if (level < 0 || level > 20) {
    throw std::runtime_error(
            "levelFromTileFilename: level " + std::to_string(level) +
            " out of GGGS range [0,20] in '" + filename + "'");
  }
  return static_cast<uint8_t>(level);
}

std::string tileFilename(const gggs::GridIndex & grid)
{
  // Delegate to the generic naming so robot/operator stores and the sync layer
  // agree on the filename↔GridIndex mapping.
  return marine_tiled_raster_store::tileFilename(grid);
}

std::string layerDirName(SourceLayer layer)
{
  switch (layer) {
    case SourceLayer::Processed: return "processed";
    case SourceLayer::Draft: return "draft";
    case SourceLayer::Reference: return "reference";
    case SourceLayer::Chart: return "chart";
  }
  throw std::runtime_error("layerDirName: unknown SourceLayer");
}

void saveTile(const BathymetryTile & tile, const std::string & path)
{
  namespace mtrs = marine_tiled_raster_store;
  // A single 2-band Float64 value tile per grid (#248): the pre-#178 _time /
  // _source companions were dropped (ADR-0002 Amendment A2.2).
  mtrs::saveTile<double>(tile.valueRaster(), path, valueBandNoData());
}

BathymetryTile loadTile(const std::string & path, const gggs::Level & level)
{
  namespace mtrs = marine_tiled_raster_store;
  BathymetryTile::Raster value =
    mtrs::loadTile<double>(path, level, BathymetryTile::value_band_count);
  return BathymetryTile(std::move(value));
}

std::size_t save(
  BathymetryStore & store, const std::string & dir, const StoreMetadata * metadata)
{
  namespace fs = std::filesystem;
  std::size_t written = 0;
  for (const SourceLayer layer : source_layers_by_priority) {
    const fs::path layer_dir = fs::path(dir) / layerDirName(layer);
    bool created_dir = false;
    const auto ensure_dir = [&]() {
        if (!created_dir) {
          fs::create_directories(layer_dir);
          created_dir = true;
        }
      };

    // Iterate the layer's tiles (a const view) and write each dirty one. The
    // dirty flag is cleared via getOrCreateTile() on the same (existing) grid:
    // a lookup, not an insert, and clearDirty() only flips a bool on the tile
    // value — neither mutates the map, so the iterator stays valid. (Since #221
    // there is no epoch subdirectory and no supersedes_disk / provenance
    // marker; the layer is one fused surface saved incrementally.)
    for (const auto & [grid, tile] : store.tiles(layer)) {
      if (!tile.dirty()) {
        continue;
      }
      ensure_dir();
      saveTile(tile, (layer_dir / tileFilename(grid)).string());
      store.getOrCreateTile(layer, grid).clearDirty();
      ++written;
    }
  }
  // The metadata is a store-wide sidecar (not per-layer): persist it once at the
  // end. Written unconditionally when present so coarse provenance is saved even
  // when no tile is dirty.
  if (metadata != nullptr) {
    metadata->save(dir);
  }
  return written;
}

std::size_t load(
  BathymetryStore & store, const std::string & dir, StoreMetadata * metadata)
{
  namespace fs = std::filesystem;
  // Auto-migrate a legacy `survey/` layer to `processed/` before scanning
  // (ADR-0010 D8); refuses if both dirs exist. See migrateLegacySurveyDir. NOTE:
  // this makes a read MUTATE the store on a pre-D8 dir (needs write access; races
  // across processes on a shared un-migrated store) — documented on the header
  // declaration so callers know a load is not always a pure read.
  migrateLegacySurveyDir(dir);
  std::size_t loaded = 0;
  bool any_layer_dir_present = false;
  for (const SourceLayer layer : source_layers_by_priority) {
    const fs::path layer_dir = fs::path(dir) / layerDirName(layer);
    if (!fs::is_directory(layer_dir)) {
      continue;
    }
    any_layer_dir_present = true;
    // Flat layout (#221): value tiles live directly under <dir>/<layer>/. A
    // subdirectory entry (e.g. a stale old-style epoch dir) is ignored — there
    // is no production data to migrate, so old epoch-subdir stores are simply
    // discarded. Warn so the dropped tiles are not a silent surprise.
    for (const auto & entry : fs::directory_iterator(layer_dir)) {
      if (entry.is_directory()) {
        // The depth overview-pyramid sidecar (`overviews/`) and its crash-safe
        // swap transients (`overviews.tmp/`/`overviews.old/`) are an expected part
        // of a draft/processed layer dir (ADR-0010 D9 / ADR-0011) — skip them
        // SILENTLY. The store loads its coarse levels from the fine tiles' own
        // level-tagged names, not from this sidecar, so there is nothing to load
        // here and nothing to warn about (ADR-0011 Consequences).
        if (isOverviewSidecarDir(entry.path().filename().string())) {
          continue;
        }
        std::cerr << "[marine_bathymetry_store] WARNING: ignoring unexpected "
                  << "subdirectory in flat-layout store: " << entry.path()
                  << " — old epoch-layout tiles are not migrated (#221); its "
                  << "tiles will NOT be loaded.\n";
        continue;
      }
      if (!entry.is_regular_file() || entry.path().extension() != ".tif") {
        continue;
      }
      // Skip dropped pre-#248 companions (`*_time.tif`/`*_source.tif`) that may
      // linger in a new-layout dir: they are not value tiles and would throw in
      // loadTile(), aborting the load of every other (valid) tile.
      if (isDroppedCompanionTile(entry.path().filename().string())) {
        std::cerr << "[marine_bathymetry_store] WARNING: skipping dropped "
                  << "companion raster (not a value tile; #248): " << entry.path()
                  << "\n";
        continue;
      }
      // Multi-level: recover each tile's level from its filename and load at
      // that level (the store is not pinned to one level, ADR-0002 §D2).
      const uint8_t lvl = levelFromTileFilename(entry.path().filename().string());
      BathymetryTile tile = loadTile(entry.path().string(), gggs::Level(lvl));
      store.getOrCreateTile(layer, tile.index()) = std::move(tile);
      ++loaded;
    }
  }
  warnIfUnrecognizedStoreLayout(dir, any_layer_dir_present);
  if (metadata != nullptr) {
    metadata->load(dir);
  }
  return loaded;
}

std::size_t loadWindow(
  BathymetryStore & store, const std::string & dir,
  const geographic_msgs::msg::GeoPoint & min_pt,
  const geographic_msgs::msg::GeoPoint & max_pt,
  StoreMetadata * metadata)
{
  namespace fs = std::filesystem;
  // Auto-migrate a legacy `survey/` layer to `processed/` before scanning
  // (ADR-0010 D8); refuses if both dirs exist. See migrateLegacySurveyDir. NOTE:
  // this makes a read MUTATE the store on a pre-D8 dir (needs write access; races
  // across processes on a shared un-migrated store) — documented on the header
  // declaration so callers know a load is not always a pure read.
  migrateLegacySurveyDir(dir);
  std::size_t loaded = 0;
  bool any_layer_dir_present = false;
  for (const SourceLayer layer : source_layers_by_priority) {
    const fs::path layer_dir = fs::path(dir) / layerDirName(layer);
    if (!fs::is_directory(layer_dir)) {
      continue;
    }
    any_layer_dir_present = true;
    // Flat layout (#221): value tiles live directly under <dir>/<layer>/.
    for (const auto & entry : fs::directory_iterator(layer_dir)) {
      if (entry.is_directory()) {
        // The depth overview-pyramid sidecar (`overviews/`) and its crash-safe
        // swap transients (`overviews.tmp/`/`overviews.old/`) are an expected part
        // of a draft/processed layer dir (ADR-0010 D9 / ADR-0011) — skip them
        // SILENTLY. The store loads its coarse levels from the fine tiles' own
        // level-tagged names, not from this sidecar, so there is nothing to load
        // here and nothing to warn about (ADR-0011 Consequences).
        if (isOverviewSidecarDir(entry.path().filename().string())) {
          continue;
        }
        std::cerr << "[marine_bathymetry_store] WARNING: ignoring unexpected "
                  << "subdirectory in flat-layout store: " << entry.path()
                  << " — old epoch-layout tiles are not migrated (#221); its "
                  << "tiles will NOT be loaded.\n";
        continue;
      }
      if (!entry.is_regular_file() || entry.path().extension() != ".tif") {
        continue;
      }
      // Skip dropped pre-#248 companions (`*_time.tif`/`*_source.tif`): not value
      // tiles; parsing their filename / feeding them to loadTile() would throw.
      if (isDroppedCompanionTile(entry.path().filename().string())) {
        std::cerr << "[marine_bathymetry_store] WARNING: skipping dropped "
                  << "companion raster (not a value tile; #248): " << entry.path()
                  << "\n";
        continue;
      }

      // Gate on geographic overlap BEFORE paying the GDAL I/O cost.
      const gggs::GridIndex candidate_grid =
        gridIndexFromTileFilename(entry.path().filename().string());
      if (!tileOverlapsBox(candidate_grid, min_pt, max_pt)) {
        continue;
      }

      // Idempotency: skip tiles already resident in this layer.
      if (store.tiles(layer).count(candidate_grid) != 0) {
        continue;
      }

      // Load the tile (GDAL I/O path — only for overlapping, non-resident tiles).
      const uint8_t lvl = levelFromTileFilename(entry.path().filename().string());
      BathymetryTile tile = loadTile(entry.path().string(), gggs::Level(lvl));
      store.getOrCreateTile(layer, tile.index()) = std::move(tile);
      ++loaded;
    }
  }
  warnIfUnrecognizedStoreLayout(dir, any_layer_dir_present);
  if (metadata != nullptr) {
    metadata->load(dir);
  }
  return loaded;
}

std::size_t evictOutside(
  BathymetryStore & store,
  const geographic_msgs::msg::GeoPoint & min_pt,
  const geographic_msgs::msg::GeoPoint & max_pt)
{
  std::size_t evicted = 0;
  for (const SourceLayer layer : source_layers_by_priority) {
    // Use the non-const layerMap() (private, accessible here via friendship) to
    // obtain a mutable reference — do NOT const_cast store.tiles(layer).
    auto & tiles = store.layerMap(layer);
    auto it = tiles.begin();
    while (it != tiles.end()) {
      const gggs::GridIndex & grid = it->first;
      const BathymetryTile & tile = it->second;
      if (!tileOverlapsBox(grid, min_pt, max_pt)) {
        // Dirty-tile guard: a dirty (unsaved) Draft tile is live CUBE sensor data
        // that has not yet reached disk; evicting it would lose that data with no
        // reload path.  Reference tiles are always clean (never mutated at
        // runtime) and are always safely evictable.  Skip dirty tiles here and
        // let them survive until they are either saved (clearing the flag) or
        // explicitly discarded by the caller.
        if (tile.dirty()) {
          ++it;
          continue;
        }
        it = tiles.erase(it);
        ++evicted;
      } else {
        ++it;
      }
    }
  }
  return evicted;
}

void replaceChartLayer(
  const std::string & staged_chart_dir, const std::string & store_dir)
{
  namespace fs = std::filesystem;
  const fs::path staged(staged_chart_dir);
  const fs::path chart = fs::path(store_dir) / layerDirName(SourceLayer::Chart);
  const fs::path backup = fs::path(store_dir) / ".chart_backup";

  // Validate the store dir up front: a non-directory store path would otherwise
  // surface as an opaque ENOTDIR mid-swap once the renames start. Fail clearly
  // before the live layer is ever touched.
  if (!fs::is_directory(store_dir)) {
    throw std::runtime_error(
            "replaceChartLayer: store dir '" + store_dir +
            "' does not exist or is not a directory");
  }

  // Validate the staged layer BEFORE touching the live one: it must exist and
  // hold at least one value tile, so an empty/failed export can never swap in
  // (ADR-0010 D7 — a corrupt regeneration must leave the old layer standing).
  if (!fs::is_directory(staged)) {
    throw std::runtime_error(
            "replaceChartLayer: staged dir '" + staged_chart_dir +
            "' does not exist or is not a directory");
  }
  // is_directory() follows symlinks, so a symlink-to-directory passes the check
  // above; rename(staged, chart) would then move the SYMLINK into the store,
  // leaving chart/ a link to an out-of-store tree (silently breakable, and
  // remove_all(backup) semantics murky). Reject a symlinked staged dir outright.
  if (fs::is_symlink(staged)) {
    throw std::runtime_error(
            "replaceChartLayer: staged dir '" + staged_chart_dir +
            "' is a symlink — refusing to move a symlink into the store");
  }
  // Reject staged aliasing the live chart/ or its backup: were staged the same
  // file as chart/ (or .chart_backup/), the chart -> backup rename and the
  // remove_all(backup) recovery step would operate on the very tree we mean to
  // swap in. equivalent() requires both paths to exist, so guard on existence
  // (the error_code overload returns false without throwing for a missing side).
  // Fail CLOSED if the equivalence check itself errors. The error_code overload
  // returns false on failure, so an unreadable path or a race would silently
  // degrade the guard to "no check" — precisely when the filesystem is already
  // misbehaving. An indeterminate answer is treated as a refusal; the swap is
  // destructive and a caller can always retry. (Not unit-tested: with both sides'
  // existence already established just above, only a genuine race can make
  // equivalent() fail, which a test cannot deterministically stage.)
  const auto refuseIfAliases =
    [&staged, &staged_chart_dir](const fs::path & other, const std::string & what) {
      if (!fs::exists(other)) {
        return;
      }
      std::error_code eq_ec;
      const bool same = fs::equivalent(staged, other, eq_ec);
      if (eq_ec) {
        throw std::runtime_error(
                "replaceChartLayer: cannot determine whether staged dir '" +
                staged_chart_dir + "' is " + what + " ('" + other.string() + "'): " +
                eq_ec.message() + " — refusing the swap rather than assuming it is not");
      }
      if (same) {
        throw std::runtime_error(
                "replaceChartLayer: staged dir '" + staged_chart_dir + "' is " + what +
                " — refusing to swap");
      }
    };
  refuseIfAliases(chart, "the live chart/ layer");
  refuseIfAliases(backup, "the .chart_backup/ recovery path");
  bool has_tile = false;
  for (const auto & entry : fs::directory_iterator(staged)) {
    // A symlinked entry inside staged is the same out-of-store-link hazard the
    // symlinked-staged-dir guard above prevents, one level down: is_regular_file()
    // follows symlinks, so a symlinked `foo.tif` would satisfy the value-tile check
    // and then ride into chart/ on the commit rename, leaving the live navigation
    // layer dependent on a tree outside the store. The load path can tolerate that
    // (it only reads); replaceChartLayer commits it permanently, so refuse. Scan
    // every entry rather than stopping at the first tile so the refusal does not
    // depend on directory iteration order.
    if (entry.is_symlink()) {
      throw std::runtime_error(
              "replaceChartLayer: staged dir '" + staged_chart_dir +
              "' contains a symlinked entry '" + entry.path().filename().string() +
              "' — refusing to commit a link into the store");
    }
    // A directory named `foo.tif` is not a value tile, so gate on
    // is_regular_file() before accepting it — exactly as the load path does (see
    // `load()`; is_regular_file() there guards the same case).
    if (!entry.is_regular_file() || entry.path().extension() != ".tif") {
      continue;
    }
    const std::string name = entry.path().filename().string();
    // Dropped pre-#248 companions (`*_time.tif`/`*_source.tif`) are not value
    // tiles; `load()` skips them via isDroppedCompanionTile() rather than parsing
    // their (non-numeric) name, so do the same here — neither validate nor count
    // them as the required tile.
    if (isDroppedCompanionTile(name)) {
      continue;
    }
    // Validate the staged tile name the SAME way the load path will read it:
    // `load()`/`loadWindow()` call levelFromTileFilename() on every non-companion
    // `.tif`, and it THROWS on a non-numeric or out-of-GGGS-range level prefix
    // (`chart.tif`, `foo.tif`, `99_0_0.tif`). `Chart` is last in
    // source_layers_by_priority, so at load time that throw aborts the whole load
    // *after* survey/reference were read, and `bathymetry_layer` then contributes
    // nothing for the rest of the run — one mis-named staged tile permanently
    // blacks out the layer. Validate here, before the commit, so such a tile is
    // refused while the old `chart/` still stands (D7) rather than after it is live.
    levelFromTileFilename(name);   // throws std::runtime_error on a bad tile name
    has_tile = true;
  }
  if (!has_tile) {
    throw std::runtime_error(
            "replaceChartLayer: staged dir '" + staged_chart_dir +
            "' contains no .tif tiles — refusing to swap in an empty layer");
  }

  // Fail fast on a cross-device staged dir. rename(2) across filesystems returns
  // EXDEV, so a staged dir on another mount cannot atomically commit. Detecting
  // it here — BEFORE the live chart/ is moved aside — means such a caller-contract
  // breach is refused without ever disturbing the old layer (D7).
  {
    struct stat staged_st{};
    struct stat store_st{};
    if (::stat(staged.c_str(), &staged_st) != 0 ||
      ::stat(store_dir.c_str(), &store_st) != 0)
    {
      throw std::runtime_error(
              "replaceChartLayer: cannot stat staged dir or store dir to verify "
              "same-filesystem placement");
    }
    if (staged_st.st_dev != store_st.st_dev) {
      throw std::runtime_error(
              "replaceChartLayer: staged dir '" + staged_chart_dir +
              "' is on a different filesystem than store dir '" + store_dir +
              "' — a cross-device rename cannot atomically commit (EXDEV); stage "
              "on the same filesystem as the store");
    }
  }

  // Crash recovery. A `.chart_backup/` left by a crashed prior run marks an
  // interrupted swap; which repair to apply depends on whether `chart/` exists:
  //   - chart/ ABSENT, backup present: the crash struck between the
  //     chart/ -> backup rename and the staged -> chart commit. The backup holds
  //     the ONLY copy of the old layer, so RESTORE it — discarding it would leave
  //     the store with no chart layer at all.
  //   - chart/ PRESENT, backup present: the crash struck after the commit but
  //     before cleanup, so the backup is truly stale (the live chart/ supersedes
  //     it) and is dropped below.
  // After a restore the backup no longer exists, so the drop below is a no-op;
  // otherwise it clears a genuinely stale backup that would else make the
  // chart -> backup rename fail with ENOTEMPTY.
  if (fs::exists(backup) && !fs::exists(chart)) {
    fs::rename(backup, chart);
  }
  // Drop the stale backup TOLERANTLY. This is the step the post-commit cleanup
  // warning below delegates to ("cleared by the next regeneration run"), so it
  // must not itself be the thing that wedges regeneration: every realistic cause
  // of a failed cleanup (EACCES on the backup dir, EROFS, EBUSY) is persistent,
  // and a throwing remove_all here would make every subsequent run fail at the
  // same point until a human intervened. Two-stage: try to delete it, and if the
  // deletion fails while the backup still stands, rename it aside to
  // `.chart_backup.stale.<n>/` — a rename needs only store-dir write permission
  // (not permission to unlink the backup's contents), so a stubborn backup is
  // moved out of the swap's way instead of blocking it. Aside dirs are not layer
  // dirs, so `load()` (which reads only survey/reference/chart) ignores them;
  // they are inert until a human clears them.
  {
    std::error_code drop_ec;
    fs::remove_all(backup, drop_ec);
    // Every filesystem probe in this tolerant block uses the `error_code`
    // overload: the whole point is not to throw here (a throw is what would wedge
    // the next run). Fail CLOSED on an indeterminate answer — if `exists()` itself
    // errors (an unreadable `store_dir`, the very condition this block guards
    // against), treat the backup as still present so the aside/refusal path runs
    // rather than the throwing `chart/` → backup rename below.
    std::error_code exist_ec;
    const bool backup_remains = fs::exists(backup, exist_ec) || exist_ec;
    if (drop_ec && backup_remains) {
      // Find a free `.chart_backup.stale.<n>` slot. Bound the search: an unbounded
      // loop would spin forever if `store_dir` became unreadable (every `exists()`
      // erroring to false reads as "taken"), so cap it and turn exhaustion into a
      // clean refusal before the live layer is touched.
      constexpr int kMaxStaleAsides = 1000;
      fs::path aside;
      bool found_slot = false;
      for (int n = 0; n < kMaxStaleAsides; ++n) {
        aside = fs::path(store_dir) / (".chart_backup.stale." + std::to_string(n));
        std::error_code slot_ec;
        if (!fs::exists(aside, slot_ec) && !slot_ec) {
          found_slot = true;
          break;
        }
      }
      if (!found_slot) {
        throw std::runtime_error(
                "replaceChartLayer: a stale '" + backup.string() +
                "' could not be removed (" + drop_ec.message() +
                ") and no free '.chart_backup.stale.<n>' slot was available under '" +
                store_dir + "' within " + std::to_string(kMaxStaleAsides) +
                " tries — clear it by hand before regenerating the chart layer");
      }
      std::error_code aside_ec;
      fs::rename(backup, aside, aside_ec);
      if (aside_ec) {
        // Neither removable nor movable: the chart -> backup rename would fail
        // with ENOTEMPTY mid-swap, so refuse now, before the live layer is
        // touched (D7 — a failed regeneration leaves the old layer standing).
        throw std::runtime_error(
                "replaceChartLayer: a stale '" + backup.string() +
                "' could not be removed (" + drop_ec.message() +
                ") nor renamed aside (" + aside_ec.message() +
                ") — clear it by hand before regenerating the chart layer");
      }
      std::cerr << "[marine_bathymetry_store] WARNING: replaceChartLayer could not remove "
                << "the stale backup at '" << backup.string() << "': " << drop_ec.message()
                << " — it was renamed aside to '" << aside.string()
                << "' so the swap can proceed; remove that directory by hand.\n";
    }
  }

  const bool had_chart = fs::exists(chart);
  if (had_chart) {
    fs::rename(chart, backup);
  }
  try {
    // The commit point: atomic on a single filesystem (rename(2)).
    fs::rename(staged, chart);
  } catch (...) {
    if (had_chart) {
      // Best-effort restore via the error_code overload: if the restore rename
      // ALSO fails (double fault) it must not throw over and mask the original
      // swap failure — that original is what we rethrow. The old layer then
      // survives under the backup path for manual recovery.
      std::error_code restore_ec;
      fs::rename(backup, chart, restore_ec);
      if (restore_ec) {
        std::cerr << "[marine_bathymetry_store] CRITICAL: replaceChartLayer could "
                  << "not restore chart/ from '" << backup.string()
                  << "' after a failed swap: " << restore_ec.message()
                  << " — the previous layer survives under that backup path; "
                  << "manual recovery required.\n";
      }
    }
    throw;
  }
  if (had_chart) {
    // The swap has ALREADY committed; clearing the backup is terminal cleanup, not
    // part of the guarantee. Throwing here would report a successful swap as a
    // failure — the caller could not tell "swap failed, old layer stands" from
    // "swap succeeded, stale backup left behind" and might wrongly retry or abort
    // the regeneration run. Use the error_code overload (same idiom as the restore
    // path above) and warn instead; the next run's crash-recovery step clears the
    // leftover backup (chart/ is present, so it is treated as stale) — and clears
    // it *tolerantly* (rename-aside fallback), so a persistent cause of this
    // failure cannot wedge later regenerations.
    std::error_code cleanup_ec;
    fs::remove_all(backup, cleanup_ec);
    if (cleanup_ec) {
      std::cerr << "[marine_bathymetry_store] WARNING: replaceChartLayer committed the "
                << "new chart/ layer but could not remove the backup at '"
                << backup.string() << "': " << cleanup_ec.message()
                << " — the swap succeeded; the next regeneration run clears the stale "
                << "backup, renaming it aside if it still cannot be removed.\n";
    }
  }
}

}  // namespace marine_bathymetry_store
