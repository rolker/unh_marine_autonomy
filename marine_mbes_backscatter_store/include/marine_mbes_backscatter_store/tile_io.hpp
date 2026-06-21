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
/// @brief Per-tile GeoTIFF persistence (ADR-0007 D6).
///
/// Each GGGS tile is persisted as **three** GeoTIFFs, all WGS84-georeferenced
/// from the same grid corners and written north-up (raster row 0 = north), so
/// persistence flips rows relative to the in-memory GGGS cell order
/// (row 0 = south):
///
/// - `<level>_<row>_<col>.tif` — 2-band `Float32` value tile (intensity,
///   intensity_variance), NaN no-data on both.
/// - `<level>_<row>_<col>_time.tif` — 1-band `Int64` timestamp tile
///   (nanoseconds since epoch, ROS-native and exact; 0 = unset, no no-data tag).
/// - `<level>_<row>_<col>_source.tif` — 1-band `UInt16` source-index tile
///   (registry index, ADR-0005 D2/D8; 0 = no-data/unset).
///
/// The maturity axis (`Processed` / `Draft`) is encoded as the on-disk
/// subdirectory (`processed/`, `draft/`); the platform/sensor provenance axis is
/// the per-cell source index + the store-wide `registry.json` (ADR-0005 D8). On
/// load, a missing `_time.tif` / `_source.tif` companion fills its band with 0
/// (degraded-mode recovery after a partial write).
///
/// @note Round-trip persistence is validated for **non-polar** latitudes
/// (|lat| < 72°), the intended lake/coastal survey envelope; polar tiles are out
/// of scope for this phase (same caveat as `marine_tiled_raster_store`).

namespace marine_mbes_backscatter_store
{

/// @brief GeoTIFF filename (no directory) for a grid's **value** tile:
///        `<level>_<row>_<col>.tif`.
std::string tileFilename(const gggs::GridIndex & grid);

/// @brief Subdirectory name for a source layer (`"processed"` / `"draft"`).
std::string layerDirName(SourceLayer layer);

/// @brief Write one tile as three GeoTIFFs derived from the value-tile @p path.
///
/// @p path is the value tile (`<grid>.tif`); the `_time.tif` and `_source.tif`
/// companions are written alongside it (same directory, suffixed stem). The
/// value tile is written first (it is the load-bearing file).
/// @throws std::runtime_error on any GDAL failure.
void saveTile(const MbesTile & tile, const std::string & path);

/// @brief Load a tile from its three GeoTIFFs, reconstructing its GridIndex at
///        @p level.
///
/// @p path is the value tile (`<grid>.tif`); the `_time.tif` and `_source.tif`
/// companions are read from the derived paths. A **missing** companion fills the
/// corresponding band with 0 (degraded-mode recovery). The grid is recovered
/// from the value file's geotransform, so a file written at a different level is
/// rejected, as is a companion whose GridIndex does not match the value tile. The
/// returned tile is clean (not dirty).
/// @throws std::runtime_error on GDAL failure, wrong dimensions/level, or a
///         companion grid mismatch.
MbesTile loadTile(const std::string & path, const gggs::Level & level);

/// @brief Persist every **dirty** tile of @p store under @p dir, then clear
///        their dirty flags. Also writes the store-wide `registry.json`.
///
/// Layout: `<dir>/<layer>/<level>_<row>_<col>{,_time,_source}.tif`, plus
/// `<dir>/registry.json`. Creates directories as needed. Clean tiles are skipped
/// (incremental save). The registry (a store-wide sidecar, not per-layer) is
/// written once at the end via @p registry; pass `nullptr` to skip it.
/// @return The number of tiles written.
/// @throws std::runtime_error on any GDAL failure; std::filesystem::filesystem_error
///         on a directory-creation failure.
std::size_t save(
  MbesBackscatterStore & store, const std::string & dir,
  const SourceRegistry * registry = nullptr);

/// @brief Load every tile found under @p dir into @p store, plus the registry.
///
/// Scans `<dir>/<layer>/` for value tiles (`<grid>.tif`), **skipping** the
/// `_time.tif` / `_source.tif` companions (they are loaded with their value
/// tile). @p store must already be at the level the tiles were written at
/// (loadTile enforces per-file). If @p registry is non-null, `registry.json` is
/// loaded into it. Loaded tiles are clean.
/// @return The number of tiles loaded.
/// @throws std::runtime_error on any GDAL failure or level mismatch;
///         std::filesystem::filesystem_error on a directory-iteration failure.
std::size_t load(
  MbesBackscatterStore & store, const std::string & dir,
  SourceRegistry * registry = nullptr);

}  // namespace marine_mbes_backscatter_store

#endif  // MARINE_MBES_BACKSCATTER_STORE__TILE_IO_HPP_
