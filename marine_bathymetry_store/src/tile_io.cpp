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
/// `survey/`/`reference/`/`chart/` layer subdirectory — an old-layout (or
/// foreign) store from which NOTHING loads.
///
/// This is a navigation-safety *observability* guard: `load()`/`loadWindow()`
/// skip a layer whose subdir is absent, so a store written only in the still-
/// obsolete taxonomy (`draft/`, `processed/` — D8, not yet implemented) loads as
/// **empty** without error. (`chart/` is a real layer since #275, so it is
/// recognized here and no longer part of the obsolete set.)
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
            << "' has content but no recognized 'survey/', 'reference/', or "
            << "'chart/' layer subdirectory — NOTHING was loaded. A pre-#248 "
            << "old-layout store (draft/processed) is not migrated (#221/#248); "
            << "regenerate it. An empty bathy prior reads as unsurveyed (navigable "
            << "unless unsurveyed_is_lethal is set).\n";
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
    case SourceLayer::Survey: return "survey";
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
        // Dirty-tile guard: a dirty (unsaved) Survey tile is live sensor data that
        // has not yet reached disk; evicting it would lose that data with no
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
  std::error_code eq_ec;
  if (fs::exists(chart) && fs::equivalent(staged, chart, eq_ec)) {
    throw std::runtime_error(
            "replaceChartLayer: staged dir '" + staged_chart_dir +
            "' is the live chart/ layer — nothing to swap in");
  }
  if (fs::exists(backup) && fs::equivalent(staged, backup, eq_ec)) {
    throw std::runtime_error(
            "replaceChartLayer: staged dir '" + staged_chart_dir +
            "' is the .chart_backup/ recovery path — refusing to swap");
  }
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
    // Mirror the load path (:343): a directory named `foo.tif` is not a value
    // tile, so gate on is_regular_file() before accepting the staged layer.
    if (entry.is_regular_file() && entry.path().extension() == ".tif") {
      has_tile = true;
    }
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
  // After a restore the backup no longer exists, so the remove_all is a no-op;
  // otherwise it clears a genuinely stale backup that would else make the
  // chart -> backup rename fail with ENOTEMPTY.
  if (fs::exists(backup) && !fs::exists(chart)) {
    fs::rename(backup, chart);
  }
  fs::remove_all(backup);

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
    fs::remove_all(backup);
  }
}

}  // namespace marine_bathymetry_store
