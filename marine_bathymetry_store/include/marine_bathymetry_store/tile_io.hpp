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

#ifndef MARINE_BATHYMETRY_STORE__TILE_IO_HPP_
#define MARINE_BATHYMETRY_STORE__TILE_IO_HPP_

#include <cstddef>
#include <string>

#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathy_cell.hpp"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"

/// @file
/// @brief Per-tile GeoTIFF persistence (ADR-0002 §D5).
///
/// Each tile is one 3-band `Float64` GeoTIFF (depth, uncertainty, timestamp),
/// WGS84-georeferenced from its GGGS grid corners. The file carries no source
/// field — the layer is encoded as the subdirectory (`processed/`, `draft/`).
/// The GeoTIFF is written north-up (raster row 0 = north), so persistence flips
/// rows relative to the in-memory GGGS cell order (row 0 = south).
///
/// `Float64` (not `Float32`) is deliberate: the timestamp band holds absolute
/// Unix seconds, which `float` cannot represent at usable resolution. A single
/// GeoTIFF has one band data type, so all three bands are `Float64`.
///
/// @note Round-trip persistence is validated for **non-polar** latitudes
/// (|lat| < 72°), the intended lake/coastal survey envelope. Near GGGS's polar
/// longitude-scaling boundaries (±72°, ±80°) and the ±90° latitude clamp, a
/// grid's per-cell angular extent diverges from the single uniform pixel size a
/// GeoTIFF geotransform can express, so the linear geotransform becomes an
/// approximation and the level-match check on load may mis-fire. Polar tiles are
/// out of scope for this phase.

namespace marine_bathymetry_store
{

/// @brief GeoTIFF filename (no directory) for a grid: `<level>_<row>_<col>.tif`.
std::string tileFilename(const gggs::GridIndex & grid);

/// @brief Subdirectory name for a source layer (`"processed"` / `"draft"`).
std::string layerDirName(SourceLayer layer);

/// @brief Write one tile as a 3-band Float64 GeoTIFF at @p path.
/// @throws std::runtime_error on any GDAL failure.
void saveTile(const BathymetryTile & tile, const std::string & path);

/// @brief Load a tile GeoTIFF, reconstructing its GridIndex at @p level.
///
/// The grid is recovered from the file's geotransform (the cell center maps
/// back through @p level), so a file written at a different level is rejected.
/// The returned tile is clean (not dirty).
/// @throws std::runtime_error on GDAL failure, wrong dimensions, or a
///         geotransform that doesn't match any grid at @p level.
BathymetryTile loadTile(const std::string & path, const gggs::Level & level);

/// @brief Persist every **dirty** tile of @p store under @p dir, then clear
///        their dirty flags.
///
/// Layout: `<dir>/<layer>/<level>_<row>_<col>.tif`. Creates directories as
/// needed. Clean tiles are skipped (incremental save).
/// @return The number of tiles written.
/// @throws std::runtime_error on any GDAL failure; std::filesystem::filesystem_error
///         (a std::runtime_error subclass) on a directory-creation failure.
std::size_t save(BathymetryStore & store, const std::string & dir);

/// @brief Load every tile found under @p dir into @p store.
///
/// Scans `<dir>/<layer>/*.tif` for each known layer. @p store must already be
/// at the level the tiles were written at (loadTile enforces per-file).
/// Loaded tiles are clean.
/// @return The number of tiles loaded.
/// @throws std::runtime_error on any GDAL failure or level mismatch;
///         std::filesystem::filesystem_error (a std::runtime_error subclass) on a
///         directory-iteration failure.
std::size_t load(BathymetryStore & store, const std::string & dir);

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__TILE_IO_HPP_
