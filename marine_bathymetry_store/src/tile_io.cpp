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

#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "marine_tiled_raster_store/tile_io.hpp"

/// @file
/// @brief Bathymetry persistence (ADR-0002 §D5): the multi-layer, SourceLayer
/// subdirectory save/load, delegating each tile's GeoTIFF read/write to the
/// generic `marine_tiled_raster_store::saveTile`/`loadTile` (#172). The bathy
/// band semantics live here: 3 `Float64` bands (depth, uncertainty, timestamp)
/// with a NaN no-data tag on depth/uncertainty and none on timestamp.

namespace marine_bathymetry_store
{

namespace
{
/// Per-band no-data for a bathy tile: NaN on depth + uncertainty, none on the
/// timestamp band (0 = unset, never NaN). One entry per band, in band order.
const std::vector<std::optional<double>> & bathyBandNoData()
{
  static const double nan = std::numeric_limits<double>::quiet_NaN();
  static const std::vector<std::optional<double>> nodata{nan, nan, std::nullopt};
  return nodata;
}

constexpr std::size_t kBathyBandCount = 3;
}  // namespace

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
  marine_tiled_raster_store::saveTile<double>(tile.raster(), path, bathyBandNoData());
}

BathymetryTile loadTile(const std::string & path, const gggs::Level & level)
{
  return BathymetryTile(
    marine_tiled_raster_store::loadTile<double>(path, level, kBathyBandCount));
}

std::size_t save(BathymetryStore & store, const std::string & dir)
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
  return written;
}

std::size_t load(BathymetryStore & store, const std::string & dir)
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
      BathymetryTile tile = loadTile(entry.path().string(), store.level());
      store.getOrCreateTile(layer, tile.index()) = std::move(tile);
      ++loaded;
    }
  }
  return loaded;
}

}  // namespace marine_bathymetry_store
