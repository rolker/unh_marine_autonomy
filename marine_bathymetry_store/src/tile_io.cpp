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

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "marine_bathymetry_store/epoch.hpp"
#include "marine_tiled_raster_store/tile_io.hpp"

/// @file
/// @brief Bathymetry persistence (ADR-0002 §D5, amended by #178): the
/// multi-layer, SourceLayer subdirectory save/load, delegating each tile's
/// GeoTIFF read/write to the generic `marine_tiled_raster_store::saveTile` /
/// `loadTile` (#172). Each GGGS tile persists as three files: a 2-band `Float64`
/// value tile (depth, uncertainty; NaN no-data on both), a 1-band `Int64` time
/// tile (`_time.tif`, timestamp ns; no no-data tag), and a 1-band `UInt16`
/// source tile (`_source.tif`, registry index; 0 no-data). A store-wide
/// `registry.json` sidecar maps source indices to provenance (ADR-0005 D2/D8).

namespace marine_bathymetry_store
{

namespace
{
constexpr const char * kTimeSuffix = "_time";
constexpr const char * kSourceSuffix = "_source";

/// Per-band no-data for the 2-band Float64 value tile: NaN on both depth and
/// uncertainty. One entry per band, in band order.
const std::vector<std::optional<double>> & valueBandNoData()
{
  static const double nan = std::numeric_limits<double>::quiet_NaN();
  static const std::vector<std::optional<double>> nodata{nan, nan};
  return nodata;
}

/// No-data for the 1-band Int64 time tile: none (0 = unset, never tagged).
const std::vector<std::optional<int64_t>> & timeBandNoData()
{
  static const std::vector<std::optional<int64_t>> nodata{std::nullopt};
  return nodata;
}

/// No-data for the 1-band UInt16 source tile: 0 = no-data/unset.
const std::vector<std::optional<uint16_t>> & sourceBandNoData()
{
  static const std::vector<std::optional<uint16_t>> nodata{std::optional<uint16_t>(0)};
  return nodata;
}

/// Derive a companion path (`_time` / `_source`) from a value-tile path: insert
/// @p suffix before the `.tif` extension. e.g. `5_1_2.tif` -> `5_1_2_time.tif`.
std::string companionPath(const std::string & value_path, const char * suffix)
{
  namespace fs = std::filesystem;
  const fs::path p(value_path);
  return (p.parent_path() / (p.stem().string() + suffix + p.extension().string())).string();
}

constexpr const char * kProvenanceMarker = "provenance";

/// Write the epoch's `provenance` marker file (`live-fused` / `replayed`) into
/// @p epoch_dir, plus a trailing newline.
void writeProvenanceMarker(const std::filesystem::path & epoch_dir, Provenance provenance)
{
  std::ofstream out(epoch_dir / kProvenanceMarker, std::ios::trunc);
  out << provenanceToken(provenance) << "\n";
}

/// Read the epoch's `provenance` marker, or `LiveFused` if the file is absent.
///
/// CRLF-safe: trailing whitespace (including a `\r` from a Windows / mixed
/// checkout) is trimmed before parsing, so a sidecar that round-tripped through
/// a CRLF filesystem is not mis-read as a parse failure that silently downgrades
/// a Replayed epoch to LiveFused (harvested #148 Copilot round-1 fix). A present
/// but unparseable marker throws — a corrupt provenance is a hard error, not a
/// silent downgrade.
Provenance readProvenanceMarker(const std::filesystem::path & epoch_dir)
{
  namespace fs = std::filesystem;
  const fs::path marker = epoch_dir / kProvenanceMarker;
  if (!fs::is_regular_file(marker)) {
    return Provenance::LiveFused;
  }
  std::ifstream in(marker);
  std::string token;
  std::getline(in, token);
  // Trim trailing whitespace / CR so a CRLF sidecar parses correctly.
  while (!token.empty() &&
    (token.back() == '\r' || token.back() == '\n' || token.back() == ' ' ||
    token.back() == '\t'))
  {
    token.pop_back();
  }
  return provenanceFromToken(token);   // throws std::invalid_argument on garbage
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
    case SourceLayer::Chart: return "chart";
  }
  throw std::runtime_error("layerDirName: unknown SourceLayer");
}

void saveTile(const BathymetryTile & tile, const std::string & path)
{
  namespace mtrs = marine_tiled_raster_store;
  // Write ordering: value tile first, then time, then source.  The three files
  // are not written atomically — a crash mid-sequence leaves a partial set on
  // disk.  Writing the value tile first is intentional: the safety query
  // (shallowestReliable) reads only the value tile, so a crashed write that
  // leaves an absent or stale _time / _source companion does not affect the
  // depth/uncertainty result used by the collision monitor.  A full reload
  // after a crash will 0-fill missing companion tiles (backward-compat path
  // in loadTile), which is the correct degraded-mode behaviour.
  mtrs::saveTile<double>(tile.valueRaster(), path, valueBandNoData());
  mtrs::saveTile<int64_t>(
    tile.timeRaster(), companionPath(path, kTimeSuffix), timeBandNoData());
  mtrs::saveTile<uint16_t>(
    tile.sourceRaster(), companionPath(path, kSourceSuffix), sourceBandNoData());
}

BathymetryTile loadTile(const std::string & path, const gggs::Level & level)
{
  namespace mtrs = marine_tiled_raster_store;
  namespace fs = std::filesystem;

  // Guard: reject legacy single-file 3-band Float64 tiles written before #178.
  // The pre-migration layout (depth / uncertainty / Float64-seconds-timestamp)
  // has exactly 3 bands in one file with no companion _time / _source tiles.
  // Loading it with value_band_count=2 would silently discard band 3 (the old
  // seconds timestamp) and 0-fill the new nadir Int64 time tile — losing
  // acquisition times without warning.  There are no on-disk tiles at the time
  // of this migration (pre-production), so this guard is a forward-safety check:
  // if such a tile appears (e.g., from a backup or a partial manual migration),
  // fail loudly rather than silently corrupt the provenance record.
  // Decision: explicit rejection is preferred over silent data-drop per the
  // workspace Quality Standard (never "good enough" when the proper fix exists).
  {
    const int band_count = mtrs::tileRasterCount(path);
    if (band_count == 3) {
      throw std::runtime_error(
              "loadTile: rejected pre-#178 legacy 3-band Float64 tile at \"" + path +
              "\".  This tile was written by a pre-migration store (depth/"
              "uncertainty/Float64-seconds in one file).  Regenerate or migrate"
              " the tile before loading it with the current store.");
    }
  }

  BathymetryTile::Raster value =
    mtrs::loadTile<double>(path, level, BathymetryTile::value_band_count);
  const gggs::GridIndex grid = value.index();

  // Companion tiles: load if present, else fill with 0 (pre-migration
  // single-platform data has no _time / _source tile — backward compatibility).
  const std::string time_path = companionPath(path, kTimeSuffix);
  BathymetryTile::TimeRaster time =
    fs::is_regular_file(time_path)
    ? mtrs::loadTile<int64_t>(time_path, level, BathymetryTile::time_band_count)
    : BathymetryTile::TimeRaster(grid, BathymetryTile::time_band_count, int64_t{0});

  // Enforce GridIndex consistency: the time companion must map to the same
  // tile as the value raster.  A mis-renamed companion (e.g., from a manual
  // store reorganisation or file tampering) would combine cell-for-cell data
  // from different geographic tiles, silently associating wrong timestamps with
  // depths.  The check is cheap (one comparison) and closes the tampering hole.
  if (fs::is_regular_file(time_path) && !(time.index() == grid)) {
    throw std::runtime_error(
            "loadTile: companion \"" + time_path +
            "\" has a GridIndex that does not match the value tile at \"" + path +
            "\".  The companion file may have been mis-renamed or the store is"
            " inconsistent.");
  }

  const std::string source_path = companionPath(path, kSourceSuffix);
  BathymetryTile::SourceRaster source =
    fs::is_regular_file(source_path)
    ? mtrs::loadTile<uint16_t>(source_path, level, BathymetryTile::source_band_count)
    : BathymetryTile::SourceRaster(grid, BathymetryTile::source_band_count, uint16_t{0});

  // Enforce GridIndex consistency for the source companion (same rationale as
  // the time-companion check above).
  if (fs::is_regular_file(source_path) && !(source.index() == grid)) {
    throw std::runtime_error(
            "loadTile: companion \"" + source_path +
            "\" has a GridIndex that does not match the value tile at \"" + path +
            "\".  The companion file may have been mis-renamed or the store is"
            " inconsistent.");
  }

  return BathymetryTile(std::move(value), std::move(time), std::move(source));
}

std::size_t save(
  BathymetryStore & store, const std::string & dir, const SourceRegistry * registry)
{
  namespace fs = std::filesystem;
  std::size_t written = 0;
  for (const SourceLayer layer : source_layers_by_priority) {
    const fs::path layer_dir = fs::path(dir) / layerDirName(layer);
    for (const auto & [epoch, epoch_tiles] : store.epochs(layer)) {
      const fs::path epoch_dir = layer_dir / epoch;
      bool created_dir = false;
      const auto ensure_dir = [&]() {
          if (!created_dir) {
            fs::create_directories(epoch_dir);
            created_dir = true;
          }
        };

      // A wholesale import (importEpoch) flags supersedes_disk: clear any stale
      // tile files for this epoch first, so a compacted epoch covering fewer
      // grids never resurrects a removed tile on the next load. The provenance
      // marker is rewritten below regardless.
      if (epoch_tiles.supersedes_disk && fs::is_directory(epoch_dir)) {
        for (const auto & e : fs::directory_iterator(epoch_dir)) {
          if (e.is_regular_file() && e.path().extension() == ".tif") {
            fs::remove(e.path());
          }
        }
      }

      // Persist the provenance marker whenever the epoch has anything to write
      // (a dirty tile or a supersession that just cleared the dir). The marker
      // is cheap and idempotent, so write it whenever we touch the epoch dir.
      bool any_dirty = epoch_tiles.supersedes_disk;
      for (const auto & [grid, tile] : epoch_tiles.tiles) {
        (void)grid;
        if (tile.dirty()) {
          any_dirty = true;
          break;
        }
      }
      if (any_dirty) {
        ensure_dir();
        writeProvenanceMarker(epoch_dir, epoch_tiles.provenance);
      }

      // Iterate the epoch's tiles (a const view) and write each dirty one. The
      // dirty flag is cleared via getOrCreateTile() on the same (existing)
      // (epoch, grid): a lookup, not an insert, and clearDirty() only flips a
      // bool on the tile value — neither mutates the map, so the iterator stays
      // valid.
      for (const auto & [grid, tile] : epoch_tiles.tiles) {
        if (!tile.dirty()) {
          continue;
        }
        ensure_dir();
        saveTile(tile, (epoch_dir / tileFilename(grid)).string());
        store.getOrCreateTile(layer, epoch, grid).clearDirty();
        ++written;
      }

      // The supersedes_disk flag has done its job once the epoch is written;
      // reset it so a subsequent incremental save does NOT re-clear the (now
      // clean) tile dir and silently delete the just-written tiles. The flag is
      // an in-memory hint only (never persisted). getOrCreateEpoch returns the
      // existing epoch (provenance unchanged) for the mutable handle.
      if (epoch_tiles.supersedes_disk) {
        store.getOrCreateEpoch(layer, epoch, epoch_tiles.provenance).supersedes_disk = false;
      }
    }
  }
  // The registry is a store-wide sidecar (not per-layer): persist it once at the
  // end. Written unconditionally when present so a freshly-registered source is
  // saved even when no tile is dirty.
  if (registry != nullptr) {
    registry->saveRegistry(dir);
  }
  return written;
}

std::size_t load(
  BathymetryStore & store, const std::string & dir, SourceRegistry * registry)
{
  namespace fs = std::filesystem;
  std::size_t loaded = 0;
  for (const SourceLayer layer : source_layers_by_priority) {
    const fs::path layer_dir = fs::path(dir) / layerDirName(layer);
    if (!fs::is_directory(layer_dir)) {
      continue;
    }
    // Each subdirectory of the layer dir is an epoch.
    for (const auto & epoch_entry : fs::directory_iterator(layer_dir)) {
      if (!epoch_entry.is_directory()) {
        continue;
      }
      const std::string epoch = epoch_entry.path().filename().string();
      validateEpochLabel(epoch);   // reject a stray non-epoch dir name
      const fs::path epoch_dir = epoch_entry.path();
      const Provenance provenance = readProvenanceMarker(epoch_dir);

      for (const auto & entry : fs::directory_iterator(epoch_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".tif") {
          continue;
        }
        // Skip the companion tiles — they are loaded alongside their value tile,
        // not as standalone value tiles (a bare *.tif glob would otherwise
        // mis-load _time / _source as value tiles).
        const std::string stem = entry.path().stem().string();
        const auto ends_with = [&stem](const char * suffix) {
            const std::string s(suffix);
            return stem.size() >= s.size() &&
                   stem.compare(stem.size() - s.size(), s.size(), s) == 0;
          };
        if (ends_with(kTimeSuffix) || ends_with(kSourceSuffix)) {
          continue;
        }
        // Multi-level: recover each tile's level from its filename and load at
        // that level (the store is not pinned to one level, ADR-0002 §D2).
        const uint8_t lvl = levelFromTileFilename(entry.path().filename().string());
        BathymetryTile tile = loadTile(entry.path().string(), gggs::Level(lvl));
        // getOrCreateTile creates the epoch as LiveFused; restore the recorded
        // provenance via getOrCreateEpoch (creation-time only, idempotent).
        store.getOrCreateEpoch(layer, epoch, provenance);
        store.getOrCreateTile(layer, epoch, tile.index()) = std::move(tile);
        ++loaded;
      }
    }
  }
  if (registry != nullptr) {
    registry->loadRegistry(dir);
  }
  return loaded;
}

}  // namespace marine_bathymetry_store
