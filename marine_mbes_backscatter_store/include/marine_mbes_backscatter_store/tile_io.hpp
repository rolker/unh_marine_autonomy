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

#ifndef MARINE_MBES_BACKSCATTER_STORE__TILE_IO_HPP_
#define MARINE_MBES_BACKSCATTER_STORE__TILE_IO_HPP_

#include <cstddef>
#include <string>

#include "marine_autonomy/gggs.h"
#include "marine_mbes_backscatter_store/mbes_cell.hpp"
#include "marine_mbes_backscatter_store/mbes_store.hpp"
#include "marine_mbes_backscatter_store/mbes_tile.hpp"
#include "marine_mbes_backscatter_store/registry.hpp"

/// @file
/// @brief Per-tile GeoTIFF persistence (ADR-0007 D6, #248 amendment A.1/A.3).
///
/// Each GGGS tile is persisted as **one** GeoTIFF, WGS84-georeferenced from its
/// grid corners and written north-up (raster row 0 = north), so persistence flips
/// rows relative to the in-memory GGGS cell order (row 0 = south):
///
/// - `<level>_<row>_<col>.tif` — 3-band `Float32` value tile
///   (mean, standard_error, sample_sd), NaN no-data on all three.
///
/// The pre-#248 `_time.tif` (Int64 ns) and `_source.tif` (UInt16 source index)
/// companions were dropped (#248 amendment A.3). The sole layer (`Cube`) is
/// encoded as the on-disk subdirectory (`cube/`); coarse provenance lives in the
/// store-wide `registry.json` `StoreMetadata` (ADR-0005 #248).
///
/// @note Round-trip persistence is validated for **non-polar** latitudes
/// (|lat| < 72°), the intended lake/coastal survey envelope; polar tiles are out
/// of scope for this phase (same caveat as `marine_tiled_raster_store`).

namespace marine_mbes_backscatter_store
{

/// @brief GeoTIFF filename (no directory) for a grid's **value** tile:
///        `<level>_<row>_<col>.tif`.
std::string tileFilename(const gggs::GridIndex & grid);

/// @brief Subdirectory name for a source layer (`"cube"`).
std::string layerDirName(SourceLayer layer);

/// @brief Write one tile as a single 3-band value GeoTIFF at @p path (`<grid>.tif`).
/// @throws std::runtime_error on any GDAL failure.
void saveTile(const MbesTile & tile, const std::string & path);

/// @brief Load a tile from its value GeoTIFF, reconstructing its GridIndex at
///        @p level.
///
/// @p path is the value tile (`<grid>.tif`). The grid is recovered from the value
/// file's geotransform, so a file written at a different level is rejected. The
/// returned tile is clean (not dirty).
/// @throws std::runtime_error on GDAL failure, or wrong dimensions/level.
MbesTile loadTile(const std::string & path, const gggs::Level & level);

/// @brief Persist every **dirty** tile of @p store under @p dir, then clear
///        their dirty flags. Also writes the store-wide `registry.json`.
///
/// Layout: `<dir>/<layer>/<level>_<row>_<col>.tif`, plus `<dir>/registry.json`.
/// Creates directories as needed. Clean tiles are skipped (incremental save). The
/// coarse `StoreMetadata` (a store-wide sidecar, not per-layer) is written once at
/// the end via @p metadata; pass `nullptr` to skip it.
/// @return The number of tiles written.
/// @throws std::runtime_error on any GDAL failure; std::filesystem::filesystem_error
///         on a directory-creation failure.
std::size_t save(
  MbesBackscatterStore & store, const std::string & dir,
  const StoreMetadata * metadata = nullptr);

/// @brief Load every tile found under @p dir into @p store, plus the metadata.
///
/// Scans `<dir>/<layer>/` for value tiles (`<grid>.tif`). @p store must already be
/// at the level the tiles were written at (loadTile enforces per-file). If @p
/// metadata is non-null, `registry.json` is loaded into it. Loaded tiles are clean.
/// @return The number of tiles loaded.
/// @throws std::runtime_error on any GDAL failure or level mismatch;
///         std::filesystem::filesystem_error on a directory-iteration failure.
std::size_t load(
  MbesBackscatterStore & store, const std::string & dir,
  StoreMetadata * metadata = nullptr);

}  // namespace marine_mbes_backscatter_store

#endif  // MARINE_MBES_BACKSCATTER_STORE__TILE_IO_HPP_
