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

#ifndef MARINE_TILED_RASTER_STORE__TILED_RASTER_TILE_HPP_
#define MARINE_TILED_RASTER_STORE__TILED_RASTER_TILE_HPP_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "marine_autonomy/gggs.h"

namespace marine_tiled_raster_store
{

/// @brief One GGGS grid (960×960 cells) of an N-band raster for a single layer.
///
/// The format-agnostic generalization of `marine_bathymetry_store`'s
/// `BathymetryTile`: a GGGS-`GridIndex`-keyed tile holding @p band_count dense,
/// row-major bands of element type @p T, in **GGGS cell coordinates** (row 0 is
/// the southern edge and increases north; column 0 is the western edge and
/// increases east). Persistence (`tile_io`) flips rows to north-up GeoTIFF order.
///
/// Structure-of-arrays (one `std::vector<T>` per band) mirrors the GeoTIFF band
/// layout, making persistence a per-band copy. A tile is allocated whole — its
/// owning store is what allocates tiles lazily (only when a grid is first
/// written), so empty regions cost nothing. A tile carries a `dirty` flag so a
/// store can persist only what changed (incremental save).
///
/// @tparam T Per-cell element type. `tile_io` supports the explicitly
///   instantiated types (`double`, `std::uint16_t`); other types compile here
///   but have no persistence backing.
template<typename T>
class TiledRasterTile
{
public:
  /// @brief Number of cells along each grid edge (GGGS constant, 960).
  static constexpr uint16_t edge = gggs::cell_rows_per_grid;
  /// @brief Total cells in a tile (edge × edge).
  static constexpr uint32_t cell_count = gggs::grid_total_cell_count;

  /// @brief Construct a tile for @p index, one band per entry of @p band_fills,
  ///        every cell of band @c b initialized to @c band_fills[b].
  ///
  /// Per-band fills let a consumer give different bands different no-data
  /// sentinels (e.g. NaN for a depth band, 0 for a timestamp band).
  /// @throws std::invalid_argument if @p band_fills is empty.
  TiledRasterTile(gggs::GridIndex index, const std::vector<T> & band_fills)
  : index_(index)
  {
    if (band_fills.empty()) {
      throw std::invalid_argument("TiledRasterTile: band_fills must be non-empty");
    }
    bands_.reserve(band_fills.size());
    for (const T fill : band_fills) {
      bands_.emplace_back(cell_count, fill);
    }
  }

  /// @brief Construct a tile for @p index with @p band_count bands, every cell
  ///        of every band initialized to @p fill.
  TiledRasterTile(gggs::GridIndex index, std::size_t band_count, T fill)
  : TiledRasterTile(index, std::vector<T>(band_count, fill)) {}

  /// @brief The grid this tile covers.
  const gggs::GridIndex & index() const noexcept {return index_;}

  /// @brief Number of bands in this tile.
  std::size_t bandCount() const noexcept {return bands_.size();}

  /// @brief Write @p value at (@p row, @p col) in band @p band; marks dirty.
  void set(uint16_t row, uint16_t col, std::size_t band, T value)
  {
    bands_[band][offset(row, col)] = value;
    dirty_ = true;
  }

  /// @brief Read the value at (@p row, @p col) in band @p band.
  T get(uint16_t row, uint16_t col, std::size_t band) const
  {
    return bands_[band][offset(row, col)];
  }

  /// @brief Raw band accessors (row-major, GGGS cell order) for persistence.
  /// @{
  const std::vector<T> & band(std::size_t b) const {return bands_[b];}
  std::vector<T> & band(std::size_t b) {return bands_[b];}
  /// @}

  /// @brief Whether this tile has unsaved changes.
  bool dirty() const noexcept {return dirty_;}
  /// @brief Mark all changes saved (called by persistence after a successful write).
  void clearDirty() noexcept {dirty_ = false;}
  /// @brief Force the dirty flag (used when reconstructing a tile in memory).
  void markDirty() noexcept {dirty_ = true;}

  /// @brief Row-major offset of cell (@p row, @p col). Asserts in debug builds.
  static uint32_t offset(uint16_t row, uint16_t col)
  {
    assert(row < edge && col < edge && "TiledRasterTile cell out of bounds");
    return static_cast<uint32_t>(row) * edge + col;
  }

private:
  gggs::GridIndex index_;
  std::vector<std::vector<T>> bands_;
  bool dirty_ = false;
};

}  // namespace marine_tiled_raster_store

#endif  // MARINE_TILED_RASTER_STORE__TILED_RASTER_TILE_HPP_
