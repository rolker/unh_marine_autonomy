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

#include "marine_mbes_backscatter_store/tile_io.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "marine_tiled_raster_store/tile_io.hpp"

/// @file
/// @brief MBES backscatter persistence (ADR-0007 D6): the multi-layer,
/// SourceLayer-subdirectory save/load, delegating each tile's GeoTIFF read/write
/// to the generic `marine_tiled_raster_store::saveTile` / `loadTile`. Each GGGS
/// tile persists as three files: a 2-band `Float32` value tile (intensity,
/// intensity_variance; NaN no-data on both), a 1-band `Int64` time tile
/// (`_time.tif`, timestamp ns; no no-data tag), and a 1-band `UInt16` source tile
/// (`_source.tif`, registry index; 0 no-data). A store-wide `registry.json`
/// sidecar maps source indices to provenance (ADR-0005 D2/D8).

namespace marine_mbes_backscatter_store
{

namespace
{
constexpr const char * kTimeSuffix = "_time";
constexpr const char * kSourceSuffix = "_source";

/// Per-band no-data for the 2-band Float32 value tile: NaN on both intensity and
/// variance. One entry per band, in band order.
const std::vector<std::optional<float>> & valueBandNoData()
{
  static const float nan = std::numeric_limits<float>::quiet_NaN();
  static const std::vector<std::optional<float>> nodata{nan, nan};
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

/// True if @p stem ends with @p suffix (companion-tile detection on directory scan).
bool stemEndsWith(const std::string & stem, const char * suffix)
{
  const std::string s(suffix);
  return stem.size() >= s.size() &&
         stem.compare(stem.size() - s.size(), s.size(), s) == 0;
}
}  // namespace

std::string tileFilename(const gggs::GridIndex & grid)
{
  // Delegate to the generic naming so robot/operator stores and the sync layer
  // agree on the filename<->GridIndex mapping.
  return marine_tiled_raster_store::tileFilename(grid);
}

std::string layerDirName(SourceLayer layer)
{
  switch (layer) {
    case SourceLayer::Processed: return "processed";
    case SourceLayer::Draft: return "draft";
  }
  throw std::runtime_error("layerDirName: unknown SourceLayer");
}

void saveTile(const MbesTile & tile, const std::string & path)
{
  namespace mtrs = marine_tiled_raster_store;
  // Write ordering: value tile first, then time, then source.  The three files
  // are not written atomically — a crash mid-sequence leaves a partial set on
  // disk.  Writing the value tile first is intentional: it is the load-bearing
  // file (intensity + variance), and load() 0-fills a missing _time / _source
  // companion, so a crashed write degrades gracefully rather than losing the
  // backscatter value.
  mtrs::saveTile<float>(tile.valueRaster(), path, valueBandNoData());
  mtrs::saveTile<int64_t>(
    tile.timeRaster(), companionPath(path, kTimeSuffix), timeBandNoData());
  mtrs::saveTile<uint16_t>(
    tile.sourceRaster(), companionPath(path, kSourceSuffix), sourceBandNoData());
}

MbesTile loadTile(const std::string & path, const gggs::Level & level)
{
  namespace mtrs = marine_tiled_raster_store;
  namespace fs = std::filesystem;

  MbesTile::Raster value =
    mtrs::loadTile<float>(path, level, MbesTile::value_band_count);
  const gggs::GridIndex grid = value.index();

  // Companion tiles: load if present, else fill with 0 (degraded-mode recovery
  // after a partial write — the value tile survives without its companions).
  const std::string time_path = companionPath(path, kTimeSuffix);
  MbesTile::TimeRaster time =
    fs::is_regular_file(time_path)
    ? mtrs::loadTile<int64_t>(time_path, level, MbesTile::time_band_count)
    : MbesTile::TimeRaster(grid, MbesTile::time_band_count, int64_t{0});

  // Enforce GridIndex consistency: a mis-renamed companion (manual store
  // reorganisation or tampering) would combine cell-for-cell data from different
  // geographic tiles, silently associating wrong timestamps with intensities.
  if (fs::is_regular_file(time_path) && !(time.index() == grid)) {
    throw std::runtime_error(
            "loadTile: companion \"" + time_path +
            "\" has a GridIndex that does not match the value tile at \"" + path +
            "\".  The companion file may have been mis-renamed or the store is"
            " inconsistent.");
  }

  const std::string source_path = companionPath(path, kSourceSuffix);
  MbesTile::SourceRaster source =
    fs::is_regular_file(source_path)
    ? mtrs::loadTile<uint16_t>(source_path, level, MbesTile::source_band_count)
    : MbesTile::SourceRaster(grid, MbesTile::source_band_count, uint16_t{0});

  if (fs::is_regular_file(source_path) && !(source.index() == grid)) {
    throw std::runtime_error(
            "loadTile: companion \"" + source_path +
            "\" has a GridIndex that does not match the value tile at \"" + path +
            "\".  The companion file may have been mis-renamed or the store is"
            " inconsistent.");
  }

  return MbesTile(std::move(value), std::move(time), std::move(source));
}

std::size_t save(
  MbesBackscatterStore & store, const std::string & dir, const SourceRegistry * registry)
{
  namespace fs = std::filesystem;
  std::size_t written = 0;
  for (const SourceLayer layer : source_layers_by_priority) {
    const fs::path layer_dir = fs::path(dir) / layerDirName(layer);
    bool created_dir = false;
    // Iterate the layer's tiles (a const view) and write each dirty one. The
    // dirty flag is cleared via getOrCreateTile() on the same (existing) key:
    // that's a lookup, not an insert, and clearDirty() only flips a bool on the
    // tile value — neither mutates the map, so the iterator stays valid.
    for (const auto & [grid, tile] : store.tiles(layer)) {
      if (!tile.dirty()) {
        continue;
      }
      if (!created_dir) {
        fs::create_directories(layer_dir);
        created_dir = true;
      }
      saveTile(tile, (layer_dir / tileFilename(grid)).string());
      store.getOrCreateTile(layer, grid).clearDirty();
      ++written;
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
  MbesBackscatterStore & store, const std::string & dir, SourceRegistry * registry)
{
  namespace fs = std::filesystem;
  std::size_t loaded = 0;
  for (const SourceLayer layer : source_layers_by_priority) {
    const fs::path layer_dir = fs::path(dir) / layerDirName(layer);
    if (!fs::is_directory(layer_dir)) {
      continue;
    }
    for (const auto & entry : fs::directory_iterator(layer_dir)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".tif") {
        continue;
      }
      // Skip the companion tiles — they are loaded alongside their value tile,
      // not as standalone value tiles (a bare *.tif glob would otherwise
      // mis-load _time / _source as value tiles).
      const std::string stem = entry.path().stem().string();
      if (stemEndsWith(stem, kTimeSuffix) || stemEndsWith(stem, kSourceSuffix)) {
        continue;
      }
      MbesTile tile = loadTile(entry.path().string(), store.level());
      store.getOrCreateTile(layer, tile.index()) = std::move(tile);
      ++loaded;
    }
  }
  if (registry != nullptr) {
    registry->loadRegistry(dir);
  }
  return loaded;
}

}  // namespace marine_mbes_backscatter_store
