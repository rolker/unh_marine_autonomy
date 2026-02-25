// Copyright 2016-2020 Roland Arsenault
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the Roland Arsenault nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef PROJECT11_GGGS_CELL_INDEX_H
#define PROJECT11_GGGS_CELL_INDEX_H

#include "level_spec.h"

namespace gggs
{
/// @brief Index of a single cell within a GGGS grid.
///
/// Each grid contains 960x960 cells. A CellIndex combines a GridIndex with
/// a local (row, column) within that grid. Rows are numbered from south;
/// columns from west.
class CellIndex
{
public:
  /// @brief Construct an invalid (sentinel) CellIndex.
  CellIndex(){}
  /// @brief Construct a CellIndex at cell (0,0) of the given grid.
  CellIndex(GridIndex grid):grid_index_(grid){}
  /// @brief Construct a CellIndex at a specific row and column.
  CellIndex(GridIndex grid, uint16_t row, uint16_t column):
    grid_index_(grid), row_(row), column_(column)
  {
  }

  // CellIndex(double latitude, double longitude, GridIndex grid)
  // {
  //   initialize(latitude, longitude, grid);
  // }

  /// @brief Construct a CellIndex from a geographic position within a grid.
  /// @param grid The grid containing the position.
  /// @param position Geographic coordinates (latitude, longitude).
  CellIndex(GridIndex grid, const gz4d::PositionDegrees &position):
    grid_index_(grid)
  {

    // Note, we constrain to 1.0, but what we really need is up to 1.0.
    double row_p = std::max(0.0, std::min(1.0, position.latitude-grid_index_.southLatitude())/grid_index_.latitudinalSpan());
    // This std::min should filter out 1.0 from above
    row_ = std::min<uint16_t>(cell_rows_per_grid-1, cell_rows_per_grid*row_p);

    double column_p = std::max(0.0, (position.longitude-grid_index_.westLongitude())/grid_index_.longitudinalSpan());
    column_ = std::min<uint16_t>(cell_columns_per_grid-1, cell_columns_per_grid*column_p);
  }

  /// @brief Check if this cell index is valid.
  bool valid() const noexcept
  {
    return grid_index_.valid() && row_ < cell_rows_per_grid && column_ < cell_columns_per_grid;
  }

  constexpr const GridIndex& grid() const noexcept
  {
    return grid_index_;
  }

  constexpr uint8_t level() const noexcept
  {
    return grid_index_.level();
  }

  constexpr const uint16_t &row() const noexcept
  {
    return row_;
  }

  constexpr const uint16_t &column() const noexcept
  {
    return column_;
  }

  /// @brief Compute the geographic position of this cell's south-west corner.
  /// @return Position in degrees.
  gz4d::PositionDegrees position() const
  {
    double row_p = row_/double(cell_rows_per_grid);
    double column_p = column_/double(cell_columns_per_grid);
    auto latitude = grid_index_.southLatitude()+row_p*grid_index_.latitudinalSpan();
    auto longitude = grid_index_.westLongitude()+column_p*grid_index_.longitudinalSpan();
    return gz4d::PositionDegrees(latitude, longitude);
  }

  /// Less than operator allowing use as std::map key.
  friend bool operator<(const CellIndex& lhs, const CellIndex& rhs)
  {
    if (lhs.grid_index_ != rhs.grid_index_)
      return lhs.grid_index_ < rhs.grid_index_;
    if (lhs.row_ != rhs.row_)
      return lhs.row_ < rhs.row_;
    return lhs.column_ < rhs.column_;
  }

  friend bool operator==(const CellIndex& lhs, const CellIndex& rhs)
  {
    if (!lhs.valid() && !rhs.valid())
      return true;
    if (!lhs.valid() || !rhs.valid())
      return false;
    return lhs.grid_index_ == rhs.grid_index_ &&
      lhs.row_ == rhs.row_ && lhs.column_ == rhs.column_;
  }

  friend bool operator!=(const CellIndex& lhs, const CellIndex& rhs)
  {
    return !(lhs == rhs);
  }


  friend std::ostream& operator<< (std::ostream& stream, const CellIndex& index)
  {
    stream << index.grid_index_ << " CellIndex row: " << index.row_ << " col: " << index.column_;
    return stream;
  }

private:
  GridIndex grid_index_;

  /// Positive integer row starting the bottom of the grid.
  uint16_t row_ = cell_rows_per_grid;

  /// Positive integer column starting at left edge of the grid.
  uint16_t column_ = cell_columns_per_grid;
};

}

#endif
