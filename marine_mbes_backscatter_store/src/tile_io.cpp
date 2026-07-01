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
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "marine_tiled_raster_store/tile_io.hpp"

/// @file
/// @brief MBES backscatter persistence (ADR-0007 D6, #248 amendment A.1/A.3): the
/// SourceLayer-subdirectory save/load, delegating each tile's GeoTIFF read/write
/// to the generic `marine_tiled_raster_store::saveTile` / `loadTile`. Each GGGS
/// tile persists as a single 3-band `Float32` value tile
/// (mean, standard_error, sample_sd; NaN no-data on all three) — the pre-#248
/// `_time` / `_source` companions were dropped. A store-wide `registry.json`
/// sidecar holds coarse `StoreMetadata` (ADR-0005 #248).

namespace marine_mbes_backscatter_store
{

namespace
{
/// Per-band no-data for the 3-band Float32 value tile: NaN on all three bands.
/// One entry per band, in band order.
const std::vector<std::optional<float>> & valueBandNoData()
{
  static const float nan = std::numeric_limits<float>::quiet_NaN();
  static const std::vector<std::optional<float>> nodata{nan, nan, nan};
  return nodata;
}

/// True for a pre-#248 companion raster (`*_time.tif` / `*_source.tif`) that may
/// linger in a `survey/` dir. The per-cell `_time`/`_source` companions were
/// dropped by #248 (ADR-0007 amendment A.3); a straggler must be skipped, not
/// fed to `loadTile` (it would throw on the band-count mismatch and abort the
/// whole load). Mirrors the bathy store's guard.
bool isDroppedCompanionTile(const std::string & filename)
{
  const auto hasSuffix = [&filename](const std::string & suffix) {
      return filename.size() >= suffix.size() &&
             filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
  return hasSuffix("_time.tif") || hasSuffix("_source.tif");
}

/// Diagnostic (MBES is not a costmap input): warn when a store dir holds content
/// but exposes no recognized `survey/` layer subdir, so a silently-empty load is
/// observable. Silent for a fresh/absent or metadata-only store. Mirrors the
/// bathy store's guard.
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
  std::cerr << "[marine_mbes_backscatter_store] WARNING: store directory '" << dir
            << "' has content but no recognized 'survey/' layer subdirectory — "
            << "NOTHING was loaded. A pre-#248 old-layout store "
            << "(draft/processed) is not migrated (#221/#248); regenerate it.\n";
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
    case SourceLayer::Survey: return "survey";
  }
  throw std::runtime_error("layerDirName: unknown SourceLayer");
}

void saveTile(const MbesTile & tile, const std::string & path)
{
  namespace mtrs = marine_tiled_raster_store;
  // A single 3-band Float32 value tile per grid (#248): the pre-#248 _time /
  // _source companions were dropped (ADR-0007 amendment A.3).
  mtrs::saveTile<float>(tile.valueRaster(), path, valueBandNoData());
}

MbesTile loadTile(const std::string & path, const gggs::Level & level)
{
  namespace mtrs = marine_tiled_raster_store;
  MbesTile::Raster value =
    mtrs::loadTile<float>(path, level, MbesTile::value_band_count);
  return MbesTile(std::move(value));
}

std::size_t save(
  MbesBackscatterStore & store, const std::string & dir, const StoreMetadata * metadata)
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
  // The metadata is a store-wide sidecar (not per-layer): persist it once at the
  // end. Written unconditionally when present so coarse provenance is saved even
  // when no tile is dirty.
  if (metadata != nullptr) {
    metadata->save(dir);
  }
  return written;
}

std::size_t load(
  MbesBackscatterStore & store, const std::string & dir, StoreMetadata * metadata)
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
    for (const auto & entry : fs::directory_iterator(layer_dir)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".tif") {
        continue;
      }
      // Skip dropped pre-#248 companions (`*_time.tif`/`*_source.tif`) that may
      // linger in a survey/ dir; feeding one to loadTile would throw on the
      // band-count mismatch and abort the whole load.
      if (isDroppedCompanionTile(entry.path().filename().string())) {
        std::cerr << "[marine_mbes_backscatter_store] WARNING: skipping dropped "
                  << "companion raster (not a value tile; #248): " << entry.path()
                  << "\n";
        continue;
      }
      MbesTile tile = loadTile(entry.path().string(), store.level());
      store.getOrCreateTile(layer, tile.index()) = std::move(tile);
      ++loaded;
    }
  }
  // Observability: a store dir with content but no recognized layer subdir loaded
  // nothing (e.g. a pre-#248 draft/processed store). Diagnostic-only for MBES.
  warnIfUnrecognizedStoreLayout(dir, any_layer_dir_present);
  if (metadata != nullptr) {
    metadata->load(dir);
  }
  return loaded;
}

}  // namespace marine_mbes_backscatter_store
