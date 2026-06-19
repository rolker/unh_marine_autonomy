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

#ifndef MARINE_BATHYMETRY_STORE__BATHYMETRY_TILE_HPP_
#define MARINE_BATHYMETRY_STORE__BATHYMETRY_TILE_HPP_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathy_cell.hpp"
#include "marine_tiled_raster_store/tiled_raster_tile.hpp"

namespace marine_bathymetry_store
{

/// @brief One GGGS grid (960×960 cells) of bathymetric data for a single layer.
///
/// A thin, bathy-semantic wrapper over `marine_tiled_raster_store::
/// TiledRasterTile<double>` (the generic GGGS-tiled raster, #172): three
/// `Float64` bands — **depth**, **uncertainty**, **timestamp** — in that order,
/// matching the GeoTIFF band layout (`tile_io`). The generic tile owns the
/// storage, dirty flag, and GGGS-cell-order layout (row 0 = south); this wrapper
/// adds the per-cell `BathyCell` record and the named band accessors the store
/// and persistence rely on. A fully-allocated tile holds 3 × 960 × 960 doubles
/// ≈ 22 MB (see `BathyCell` for why `double`); tiles are allocated lazily by the
/// owning `BathymetryStore`.
class BathymetryTile
{
public:
  /// @brief The underlying generic raster tile type.
  using Raster = marine_tiled_raster_store::TiledRasterTile<double>;

  /// @brief Number of cells along each grid edge (GGGS constant).
  static constexpr uint16_t edge = Raster::edge;
  /// @brief Total cells in a tile (edge × edge).
  static constexpr uint32_t cell_count = Raster::cell_count;

  /// @brief Construct an empty tile for @p index (depth/uncertainty no-data, ts 0).
  explicit BathymetryTile(gggs::GridIndex index)
  : tile_(index, std::vector<double>{
      std::numeric_limits<double>::quiet_NaN(),    // depth
      std::numeric_limits<double>::quiet_NaN(),    // uncertainty
      0.0})                                        // timestamp (0 = unset)
  {
  }

  /// @brief Wrap a generic raster tile loaded from disk (persistence path).
  explicit BathymetryTile(Raster tile)
  : tile_(std::move(tile)) {}

  /// @brief The grid this tile covers.
  const gggs::GridIndex & index() const noexcept {return tile_.index();}

  /// @brief Write a cell at (@p row, @p col) within the grid; marks the tile dirty.
  void set(uint16_t row, uint16_t col, const BathyCell & cell)
  {
    const uint32_t i = offset(row, col);
    tile_.band(kDepth)[i] = cell.depth;
    tile_.band(kUncertainty)[i] = cell.uncertainty;
    tile_.band(kTimestamp)[i] = cell.timestamp;
    tile_.markDirty();
  }

  /// @brief Read the cell at (@p row, @p col) within the grid.
  BathyCell get(uint16_t row, uint16_t col) const
  {
    const uint32_t i = offset(row, col);
    return BathyCell{
      tile_.band(kDepth)[i], tile_.band(kUncertainty)[i], tile_.band(kTimestamp)[i]};
  }

  /// @brief Whether this tile has unsaved changes.
  bool dirty() const noexcept {return tile_.dirty();}
  /// @brief Mark all changes saved (called by persistence after a successful write).
  void clearDirty() noexcept {tile_.clearDirty();}
  /// @brief Force the dirty flag (used when reconstructing a tile in memory).
  void markDirty() noexcept {tile_.markDirty();}

  /// @brief Raw band accessors (row-major, GGGS cell order) for persistence.
  /// @{
  const std::vector<double> & depthBand() const noexcept {return tile_.band(kDepth);}
  const std::vector<double> & uncertaintyBand() const noexcept
  {return tile_.band(kUncertainty);}
  const std::vector<double> & timestampBand() const noexcept {return tile_.band(kTimestamp);}
  std::vector<double> & depthBand() noexcept {return tile_.band(kDepth);}
  std::vector<double> & uncertaintyBand() noexcept {return tile_.band(kUncertainty);}
  std::vector<double> & timestampBand() noexcept {return tile_.band(kTimestamp);}
  /// @}

  /// @brief The underlying generic raster tile (for persistence delegation).
  /// @{
  Raster & raster() noexcept {return tile_;}
  const Raster & raster() const noexcept {return tile_;}
  /// @}

  /// @brief Row-major offset of cell (@p row, @p col). Asserts in debug builds.
  static uint32_t offset(uint16_t row, uint16_t col) {return Raster::offset(row, col);}

private:
  static constexpr std::size_t kDepth = 0;
  static constexpr std::size_t kUncertainty = 1;
  static constexpr std::size_t kTimestamp = 2;

  Raster tile_;
};

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__BATHYMETRY_TILE_HPP_
