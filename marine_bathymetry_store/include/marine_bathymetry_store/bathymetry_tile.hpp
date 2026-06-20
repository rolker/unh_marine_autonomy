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
/// A bathy-semantic wrapper over **three** `marine_tiled_raster_store::
/// TiledRasterTile<>` rasters (#172), one per on-disk tile file (#178):
///
/// - **value** (`TiledRasterTile<double>`, 2 bands): depth + uncertainty — both
///   float, read together on the costmap hot path, so co-located.
/// - **time** (`TiledRasterTile<int64_t>`, 1 band): timestamp ns — different
///   dtype, cold access, so its own tile (ROS-native, exact).
/// - **source** (`TiledRasterTile<uint16_t>`, 1 band): registry source index —
///   different dtype, multi-platform provenance (ADR-0005 D2/D8).
///
/// Each generic tile owns its storage and GGGS-cell-order layout (row 0 = south);
/// this wrapper adds the per-cell `BathyCell` record and the named band accessors
/// the store and persistence rely on. The **value raster's dirty flag is
/// authoritative** for the whole tile: `set()` marks it; `clearDirty()` /
/// `markDirty()` operate through it; persistence writes/clears all three rasters
/// together. Tiles are allocated lazily by the owning `BathymetryStore`.
class BathymetryTile
{
public:
  /// @brief The underlying generic raster tile type for depth + uncertainty.
  using Raster = marine_tiled_raster_store::TiledRasterTile<double>;
  /// @brief The underlying raster type for the timestamp tile (int64 ns).
  using TimeRaster = marine_tiled_raster_store::TiledRasterTile<int64_t>;
  /// @brief The underlying raster type for the source-index tile (uint16).
  using SourceRaster = marine_tiled_raster_store::TiledRasterTile<uint16_t>;

  /// @brief Number of cells along each grid edge (GGGS constant).
  static constexpr uint16_t edge = Raster::edge;
  /// @brief Total cells in a tile (edge × edge).
  static constexpr uint32_t cell_count = Raster::cell_count;

  /// @brief Number of bands in each on-disk tile file.
  /// @{
  static constexpr std::size_t value_band_count = 2;   ///< depth + uncertainty
  static constexpr std::size_t time_band_count = 1;    ///< timestamp ns
  static constexpr std::size_t source_band_count = 1;  ///< source index
  /// @}

  /// @brief Construct an empty tile for @p index (depth/uncertainty no-data, ts
  ///        and source 0 = unset).
  explicit BathymetryTile(gggs::GridIndex index)
  : value_(index, std::vector<double>{
      std::numeric_limits<double>::quiet_NaN(),    // depth
      std::numeric_limits<double>::quiet_NaN()}),  // uncertainty
    time_(index, time_band_count, int64_t{0}),     // timestamp (0 = unset)
    source_(index, source_band_count, uint16_t{0})  // source index (0 = unset)
  {
  }

  /// @brief Wrap rasters loaded from disk (persistence path).
  ///
  /// The three rasters must cover the same grid; the value raster's grid is
  /// authoritative for `index()`. The constructed tile is clean.
  BathymetryTile(Raster value, TimeRaster time, SourceRaster source)
  : value_(std::move(value)), time_(std::move(time)), source_(std::move(source)) {}

  /// @brief The grid this tile covers.
  const gggs::GridIndex & index() const noexcept {return value_.index();}

  /// @brief Write a cell at (@p row, @p col) within the grid; marks the tile dirty.
  void set(uint16_t row, uint16_t col, const BathyCell & cell)
  {
    const uint32_t i = offset(row, col);
    value_.band(kDepth)[i] = cell.depth;
    value_.band(kUncertainty)[i] = cell.uncertainty;
    time_.band(0)[i] = cell.timestamp;
    source_.band(0)[i] = cell.source_index;
    value_.markDirty();   // value raster's dirty flag is authoritative
  }

  /// @brief Read the cell at (@p row, @p col) within the grid.
  BathyCell get(uint16_t row, uint16_t col) const
  {
    const uint32_t i = offset(row, col);
    return BathyCell{
      value_.band(kDepth)[i], value_.band(kUncertainty)[i],
      time_.band(0)[i], source_.band(0)[i]};
  }

  /// @brief Whether this tile has unsaved changes (value raster is authoritative).
  bool dirty() const noexcept {return value_.dirty();}
  /// @brief Mark all changes saved (called by persistence after a successful write).
  void clearDirty() noexcept {value_.clearDirty();}
  /// @brief Force the dirty flag (used when reconstructing a tile in memory).
  void markDirty() noexcept {value_.markDirty();}

  /// @brief Raw band accessors (row-major, GGGS cell order) for persistence.
  /// @{
  const std::vector<double> & depthBand() const noexcept {return value_.band(kDepth);}
  const std::vector<double> & uncertaintyBand() const noexcept
  {return value_.band(kUncertainty);}
  const std::vector<int64_t> & timestampBand() const noexcept {return time_.band(0);}
  const std::vector<uint16_t> & sourceBand() const noexcept {return source_.band(0);}
  std::vector<double> & depthBand() noexcept {return value_.band(kDepth);}
  std::vector<double> & uncertaintyBand() noexcept {return value_.band(kUncertainty);}
  std::vector<int64_t> & timestampBand() noexcept {return time_.band(0);}
  std::vector<uint16_t> & sourceBand() noexcept {return source_.band(0);}
  /// @}

  /// @brief The underlying generic raster tiles (for persistence delegation).
  /// @{
  Raster & valueRaster() noexcept {return value_;}
  const Raster & valueRaster() const noexcept {return value_;}
  TimeRaster & timeRaster() noexcept {return time_;}
  const TimeRaster & timeRaster() const noexcept {return time_;}
  SourceRaster & sourceRaster() noexcept {return source_;}
  const SourceRaster & sourceRaster() const noexcept {return source_;}
  /// @}

  /// @brief Row-major offset of cell (@p row, @p col). Asserts in debug builds.
  static uint32_t offset(uint16_t row, uint16_t col) {return Raster::offset(row, col);}

private:
  static constexpr std::size_t kDepth = 0;
  static constexpr std::size_t kUncertainty = 1;

  Raster value_;
  TimeRaster time_;
  SourceRaster source_;
};

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__BATHYMETRY_TILE_HPP_
