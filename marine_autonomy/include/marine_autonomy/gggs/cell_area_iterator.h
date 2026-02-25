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

#ifndef PROJECT11_GGGS_CELL_AREA_ITERATOR_H
#define PROJECT11_GGGS_CELL_AREA_ITERATOR_H

#include "cell_index.h"

namespace gggs
{

/// @brief Iterates over a rectangular area of cells within a single grid.
///
/// Traverses cells row by row within the specified GridIndex. Can iterate
/// over the full grid (960x960 cells) or a sub-region defined by geographic
/// bounds. Cell indices in the range are inclusive.
class CellAreaIterator
{
public:
  /// @brief Iterate over all cells in the grid.
  /// @param grid The grid to iterate.
  CellAreaIterator(GridIndex grid)
  {
    from_ = CellIndex(grid, 0, 0);
    to_ = CellIndex(grid, cell_rows_per_grid-1, cell_columns_per_grid-1);
    current_ = from_;
  }

  /// @brief Iterate over cells within a geographic bounding box.
  ///
  /// Indices are inclusive — the "to" cell is visited. If the bounds
  /// fall outside the grid, the iterator may be empty (invalid from start).
  /// @param grid The grid to iterate within.
  /// @param bounds Geographic bounding box to restrict iteration.
  CellAreaIterator(GridIndex grid, const gz4d::BoundsDegrees& bounds)
  {
    // Extract bound coordinates as doubles for comparison with grid edges.
    double min_lat = bounds.minimum().latitude;
    double max_lat = bounds.maximum().latitude;
    double min_lon = bounds.minimum().longitude;
    double max_lon = bounds.maximum().longitude;

    // Check if bounds are entirely outside the grid in any direction.
    if(max_lat <= grid.southLatitude())
      return;  // bounds entirely south of grid
    if(min_lat >= grid.northLatitude())
      return;  // bounds entirely north of grid
    if(max_lon <= grid.westLongitude())
      return;  // bounds entirely west of grid
    if(min_lon >= grid.eastLongitude())
      return;  // bounds entirely east of grid

    // Clamp the bounds to the grid extent, then compute cell indices.
    double from_lat = std::max(min_lat, grid.southLatitude());
    double to_lat = std::min(max_lat, grid.northLatitude());
    double from_lon = std::max(min_lon, grid.westLongitude());
    double to_lon = std::min(max_lon, grid.eastLongitude());

    from_ = CellIndex(grid, gz4d::PositionDegrees(from_lat, from_lon));
    to_ = CellIndex(grid, gz4d::PositionDegrees(to_lat, to_lon));
    current_ = from_;
  }

  const CellIndex& operator*() const
  {
    return current_;
  }

  const CellIndex* operator->() const
  {
    return &current_;
  }

  bool reset()
  {
    current_ = from_;
    return current_.valid();
  }

  bool valid() const
  {
    return current_.valid();
  }

  bool next()
  {
    if(valid())
    {
      auto new_row = current_.row();
      auto new_column = current_.column()+1;
      if(new_column > to_.column())
      {
        new_row += 1;
        new_column = from_.column();
      }
      if(new_row > to_.row())
        current_ = CellIndex();
      else
        current_ = CellIndex(current_.grid(), new_row, new_column);
    }
    return valid();
  }
private:
  CellIndex from_;
  CellIndex to_;
  CellIndex current_;
};

} // namespace gggs

#endif
