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

/// @brief  Iterates over rectangular area of CellIndexes within a grid at given GridIndex.
class CellAreaIterator
{
public:
  CellAreaIterator(GridIndex grid)
  {
    from_ = CellIndex(grid, 0, 0);
    to_ = CellIndex(grid, cell_rows_per_grid-1, cell_columns_per_grid-1);
    current_ = from_;
  }

  /// Indicies are inclusive, so "to" is included and not considered past the array like std containers end().
  CellAreaIterator(GridIndex grid, const gz4d::BoundsDegrees& bounds)
  {
    CellIndex from(grid, bounds.minimum());
    CellIndex to(grid, bounds.maximum());

    auto from_row = from.row();
    if(from.grid().row() < grid.row())
      from_row = 0;
    if(from.grid().row() > grid.row())
      from_row = cell_rows_per_grid;
    
    auto from_column = from.column();
    if(from.grid().column() < grid.column())
      from_column = 0;
    if(from.grid().column() > grid.column())
      from_column = cell_columns_per_grid;

    from_ = CellIndex(grid, from_row, from_column);

    auto to_row = to.row();
    if(to.grid().row() < grid.row())
      to_row = cell_rows_per_grid;
    if(to.grid().row() > grid.row())
      to_row = cell_rows_per_grid - 1;
    
    auto to_column = to.column();
    if(to.grid().column() < grid.column())
      to_column = cell_columns_per_grid;
    if(to.grid().column() > grid.column())
      to_column = cell_columns_per_grid - 1;

    to_ = CellIndex(grid, to_row, to_column);

    if(to_row < from_row || to_column < from_column)
      from_ = CellIndex(grid); // invalid index

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
