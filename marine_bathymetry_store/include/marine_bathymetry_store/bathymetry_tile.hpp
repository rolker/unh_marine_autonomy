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

#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathy_cell.hpp"

namespace marine_bathymetry_store
{

/// @brief One GGGS grid (960×960 cells) of bathymetric data for a single layer.
///
/// Stored as dense, row-major structure-of-arrays (one array each for depth,
/// uncertainty, timestamp) in **GGGS cell coordinates**: row 0 is the southern
/// edge and increases north; column 0 is the western edge and increases east.
/// (Persistence flips rows to north-up GeoTIFF order — see `tile_io`.)
///
/// SoA mirrors the GeoTIFF band layout, making persistence a per-band copy.
/// Arrays are allocated lazily by the owning `BathymetryStore` only when a
/// tile is first written, so empty regions cost nothing. A fully-allocated
/// tile holds 3 × 960 × 960 doubles ≈ 22 MB (see `BathyCell` for why double).
class BathymetryTile
{
public:
  /// @brief Number of cells along each grid edge (GGGS constant).
  static constexpr uint16_t edge = gggs::cell_rows_per_grid;
  /// @brief Total cells in a tile (edge × edge).
  static constexpr uint32_t cell_count = gggs::grid_total_cell_count;

  /// @brief Construct an empty tile for @p index (all cells no-data).
  explicit BathymetryTile(gggs::GridIndex index)
  : index_(index),
    depth_(cell_count, std::numeric_limits<double>::quiet_NaN()),
    uncertainty_(cell_count, std::numeric_limits<double>::quiet_NaN()),
    timestamp_(cell_count, 0.0)
  {
  }

  /// @brief The grid this tile covers.
  const gggs::GridIndex & index() const noexcept {return index_;}

  /// @brief Write a cell at (@p row, @p col) within the grid; marks the tile dirty.
  void set(uint16_t row, uint16_t col, const BathyCell & cell)
  {
    const uint32_t i = offset(row, col);
    depth_[i] = cell.depth;
    uncertainty_[i] = cell.uncertainty;
    timestamp_[i] = cell.timestamp;
    dirty_ = true;
  }

  /// @brief Read the cell at (@p row, @p col) within the grid.
  BathyCell get(uint16_t row, uint16_t col) const
  {
    const uint32_t i = offset(row, col);
    return BathyCell{depth_[i], uncertainty_[i], timestamp_[i]};
  }

  /// @brief Whether this tile has unsaved changes.
  bool dirty() const noexcept {return dirty_;}
  /// @brief Mark all changes saved (called by persistence after a successful write).
  void clearDirty() noexcept {dirty_ = false;}
  /// @brief Force the dirty flag (used when reconstructing a tile in memory).
  void markDirty() noexcept {dirty_ = true;}

  /// @brief Raw band accessors (row-major, GGGS cell order) for persistence.
  /// @{
  const std::vector<double> & depthBand() const noexcept {return depth_;}
  const std::vector<double> & uncertaintyBand() const noexcept {return uncertainty_;}
  const std::vector<double> & timestampBand() const noexcept {return timestamp_;}
  std::vector<double> & depthBand() noexcept {return depth_;}
  std::vector<double> & uncertaintyBand() noexcept {return uncertainty_;}
  std::vector<double> & timestampBand() noexcept {return timestamp_;}
  /// @}

  /// @brief Row-major offset of cell (@p row, @p col). Asserts in debug builds.
  static uint32_t offset(uint16_t row, uint16_t col)
  {
    assert(row < edge && col < edge && "BathymetryTile cell out of bounds");
    return static_cast<uint32_t>(row) * edge + col;
  }

private:
  gggs::GridIndex index_;
  std::vector<double> depth_;
  std::vector<double> uncertainty_;
  std::vector<double> timestamp_;
  bool dirty_ = false;
};

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__BATHYMETRY_TILE_HPP_
