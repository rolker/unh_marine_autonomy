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

#ifndef MARINE_TILED_RASTER_STORE__TILE_IO_HPP_
#define MARINE_TILED_RASTER_STORE__TILE_IO_HPP_

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_tiled_raster_store/tiled_raster_tile.hpp"

/// @file
/// @brief Generic per-tile GeoTIFF persistence for `TiledRasterTile<T>`.
///
/// The format-agnostic generalization of `marine_bathymetry_store/tile_io`
/// (ADR-0002 §D5/§D6 in `unh_marine_autonomy` is the origin and the contract
/// the #86-Phase-6 manifest/content-hash sync will build on here). Each tile is
/// one GeoTIFF with @c bandCount() bands of `T`, WGS84-georeferenced from its
/// GGGS grid corners and written **north-up** (raster row 0 = north), so a row
/// flip separates on-disk order from the in-memory GGGS cell order
/// (row 0 = south).
///
/// GDAL is intentionally absent from this header: the functions are templates
/// **explicitly instantiated** in `tile_io.cpp` for the supported element types
/// (`double`, `std::uint16_t`, `std::int64_t`), so consumers link the
/// instantiations without taking a public GDAL dependency. (`std::int64_t` →
/// `GDT_Int64` backs the bathy timestamp tile, #178; requires GDAL >= 3.5.)
///
/// @note Round-trip persistence is validated for **non-polar** latitudes
/// (|lat| < 72°), the intended lake/coastal survey envelope. Near GGGS's polar
/// longitude-scaling boundaries the linear geotransform becomes an
/// approximation and the level-match check on load may mis-fire. Polar tiles are
/// out of scope for this phase.

namespace marine_tiled_raster_store
{

/// @brief GeoTIFF filename (no directory) for a grid: `<level>_<row>_<col>.tif`.
std::string tileFilename(const gggs::GridIndex & grid);

/// @brief Write one tile as a `bandCount()`-band GeoTIFF (element type @p T) at @p path.
///
/// @param band_nodata Optional per-band no-data value, one entry per band (size
///   must equal @p tile.bandCount()). A `std::nullopt` entry writes no no-data
///   tag for that band (e.g. a timestamp band where every value is valid). A
///   present value must be exactly representable in @p T — it is written via
///   GDAL's `SetNoDataValue(double)`, so a sentinel outside @p T's range (or not
///   round-tripping through `double`) would tag a value the band can never hold.
/// @throws std::invalid_argument if @p band_nodata size != tile.bandCount().
/// @throws std::runtime_error on any GDAL failure.
template<typename T>
void saveTile(
  const TiledRasterTile<T> & tile, const std::string & path,
  const std::vector<std::optional<T>> & band_nodata);

/// @brief Load a tile GeoTIFF, reconstructing its GridIndex at @p level.
///
/// The grid is recovered from the file's geotransform (the cell center maps back
/// through @p level), so a file written at a different level is rejected. The
/// file must be a geographic WGS84 raster with the expected dimensions and at
/// least @p band_count bands; the first @p band_count bands are read. The
/// returned tile is clean (not dirty).
/// @throws std::runtime_error on GDAL failure, wrong dimensions/bands, a
///         non-WGS84 raster, or a geotransform that doesn't match any grid at
///         @p level.
template<typename T>
TiledRasterTile<T> loadTile(
  const std::string & path, const gggs::Level & level, std::size_t band_count);

/// @brief Persist every **dirty** tile of @p tiles into @p dir, then clear their
///        dirty flags.
///
/// Writes `<dir>/<level>_<row>_<col>.tif`, creating @p dir as needed. Clean
/// tiles are skipped (incremental save). @p band_nodata is forwarded to
/// `saveTile` for every tile.
/// @return The number of tiles written.
/// @throws std::runtime_error / std::filesystem::filesystem_error on failure.
template<typename T>
std::size_t saveTiles(
  std::map<gggs::GridIndex, TiledRasterTile<T>> & tiles, const std::string & dir,
  const std::vector<std::optional<T>> & band_nodata);

/// @brief Load every `*.tif` under @p dir into @p tiles (insert or replace),
///        reconstructing each tile at @p level with @p band_count bands.
/// @return The number of tiles loaded. Loaded tiles are clean.
/// @throws std::runtime_error / std::filesystem::filesystem_error on failure.
template<typename T>
std::size_t loadTiles(
  std::map<gggs::GridIndex, TiledRasterTile<T>> & tiles, const std::string & dir,
  const gggs::Level & level, std::size_t band_count);

}  // namespace marine_tiled_raster_store

#endif  // MARINE_TILED_RASTER_STORE__TILE_IO_HPP_
