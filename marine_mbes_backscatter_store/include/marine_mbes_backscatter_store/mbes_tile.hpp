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

#ifndef MARINE_MBES_BACKSCATTER_STORE__MBES_TILE_HPP_
#define MARINE_MBES_BACKSCATTER_STORE__MBES_TILE_HPP_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_mbes_backscatter_store/mbes_cell.hpp"
#include "marine_tiled_raster_store/tiled_raster_tile.hpp"

namespace marine_mbes_backscatter_store
{

/// @brief One GGGS grid (960×960 cells) of MBES backscatter for a single layer.
///
/// An MBES-semantic wrapper over a single `marine_tiled_raster_store::
/// TiledRasterTile<float>` value raster — a **3-band `Float32`** tile
/// `{mean, standard_error, sample_sd}` (ADR-0007 D6, #248 amendment A.1). The
/// pre-#248 `_time` (int64 ns) and `_source` (uint16) companion rasters were
/// dropped (#248 amendment A.3).
///
/// The generic tile owns its storage and GGGS-cell-order layout (row 0 = south);
/// this wrapper adds the per-cell `MbesCell` record and the named raster
/// accessors the store and persistence rely on. The value raster's dirty flag is
/// authoritative for the whole tile. Tiles are allocated lazily by the owning
/// `MbesBackscatterStore`.
class MbesTile
{
public:
  /// @brief The underlying generic raster tile type for the 3-band value tile.
  using Raster = marine_tiled_raster_store::TiledRasterTile<float>;

  /// @brief Number of cells along each grid edge (GGGS constant).
  static constexpr uint16_t edge = Raster::edge;
  /// @brief Total cells in a tile (edge × edge).
  static constexpr uint32_t cell_count = Raster::cell_count;

  /// @brief Number of bands in the on-disk value tile (mean, SE, sample_sd).
  static constexpr std::size_t value_band_count = 3;

  /// @brief Construct an empty tile for @p index (all three bands no-data).
  explicit MbesTile(gggs::GridIndex index)
  : value_(index, std::vector<float>{
      std::numeric_limits<float>::quiet_NaN(),    // mean
      std::numeric_limits<float>::quiet_NaN(),    // standard_error
      std::numeric_limits<float>::quiet_NaN()})   // sample_sd
  {
  }

  /// @brief Wrap a value raster loaded from disk (persistence path).
  ///
  /// The constructed tile is clean.
  explicit MbesTile(Raster value)
  : value_(std::move(value)) {}

  /// @brief The grid this tile covers.
  const gggs::GridIndex & index() const noexcept {return value_.index();}

  /// @brief Write a cell at (@p row, @p col) within the grid; marks the tile dirty.
  void set(uint16_t row, uint16_t col, const MbesCell & cell)
  {
    const uint32_t i = offset(row, col);
    value_.band(kMean)[i] = cell.mean;
    value_.band(kStandardError)[i] = cell.standard_error;
    value_.band(kSampleSd)[i] = cell.sample_sd;
    value_.markDirty();
  }

  /// @brief Read the cell at (@p row, @p col) within the grid.
  MbesCell get(uint16_t row, uint16_t col) const
  {
    const uint32_t i = offset(row, col);
    return MbesCell{
      value_.band(kMean)[i], value_.band(kStandardError)[i], value_.band(kSampleSd)[i]};
  }

  /// @brief Whether this tile has unsaved changes.
  bool dirty() const noexcept {return value_.dirty();}
  /// @brief Mark all changes saved (called by persistence after a successful write).
  void clearDirty() noexcept {value_.clearDirty();}
  /// @brief Force the dirty flag (used when reconstructing a tile in memory).
  void markDirty() noexcept {value_.markDirty();}

  /// @brief The underlying generic raster tile (for persistence delegation).
  /// @{
  Raster & valueRaster() noexcept {return value_;}
  const Raster & valueRaster() const noexcept {return value_;}
  /// @}

  /// @brief Row-major offset of cell (@p row, @p col). Asserts in debug builds.
  static uint32_t offset(uint16_t row, uint16_t col) {return Raster::offset(row, col);}

  /// @brief Value-tile band indices.
  /// @{
  static constexpr std::size_t kMean = 0;
  static constexpr std::size_t kStandardError = 1;
  static constexpr std::size_t kSampleSd = 2;
  /// @}

private:
  Raster value_;
};

}  // namespace marine_mbes_backscatter_store

#endif  // MARINE_MBES_BACKSCATTER_STORE__MBES_TILE_HPP_
