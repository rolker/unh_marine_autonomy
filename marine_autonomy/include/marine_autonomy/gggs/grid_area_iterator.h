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

#ifndef PROJECT11_GGGS_GRID_AREA_ITERATOR_H
#define PROJECT11_GGGS_GRID_AREA_ITERATOR_H

#include "grid_index.h"

namespace gggs
{

/// @brief Iterates over a rectangular area of GridIndex values.
///
/// Traverses all grids in the rectangle defined by two corner GridIndex
/// values (inclusive), row by row from south-west to north-east.
/// Uses longitude-based per-row column ranges to correctly handle
/// polar latitude bands where column counts change.
class GridAreaIterator
{
public:
  /// @brief Construct an iterator over grids from @p from to @p to (inclusive).
  /// @param from One corner grid.
  /// @param to Opposite corner grid.
  /// @throws std::out_of_range if levels differ.
  GridAreaIterator(GridIndex from, GridIndex to)
  {
    verifySameLevel(from, to);
    if(!from.valid() || !to.valid())
      return;  // current_ stays default-invalid
    level_ = from.level();
    from_row_ = std::min(from.row(), to.row());
    to_row_ = std::max(from.row(), to.row());
    west_lon_ = std::min(from.westLongitude(), to.westLongitude());
    east_lon_ = std::max(from.eastLongitude(), to.eastLongitude());
    current_row_ = from_row_;
    computeRowColumns(current_row_);
    current_col_ = row_first_col_;
    updateCurrent();
  }

  const GridIndex& operator*() const
  {
    return current_;
  }

  const GridIndex* operator->() const
  {
    return &current_;
  }

  /// @brief Reset to the first grid in the rectangle.
  /// @return true if the iterator is valid after reset.
  bool reset()
  {
    current_row_ = from_row_;
    computeRowColumns(current_row_);
    current_col_ = row_first_col_;
    updateCurrent();
    return valid();
  }

  /// @brief Check if the iterator points to a valid grid.
  bool valid() const
  {
    return current_.valid() && current_row_ <= to_row_;
  }

  /// @brief Advance to the next grid.
  /// @return true if the iterator is valid after advancing.
  bool next()
  {
    if(!valid())
      return false;
    current_col_++;
    if(current_col_ > row_last_col_)
    {
      current_row_++;
      if(current_row_ > to_row_)
      {
        current_ = GridIndex();
        return false;
      }
      computeRowColumns(current_row_);
      current_col_ = row_first_col_;
    }
    updateCurrent();
    return valid();
  }

private:
  void computeRowColumns(uint32_t row)
  {
    double span = levels[level_].gridLongitudinalSpan(row);
    row_first_col_ = static_cast<uint32_t>((west_lon_ + 180.0) / span);
    row_last_col_ = static_cast<uint32_t>((east_lon_ + 180.0) / span);
    // east_lon_ is the east edge; if it falls exactly on a grid boundary,
    // it belongs to the previous column
    if(east_lon_ + 180.0 > 0.0 && std::fmod(east_lon_ + 180.0, span) < 1e-10)
      row_last_col_--;
    // Clamp to valid column range
    uint32_t max_col = levels[level_].columnCount(row) - 1;
    row_first_col_ = std::min(row_first_col_, max_col);
    row_last_col_ = std::min(row_last_col_, max_col);
  }

  void updateCurrent()
  {
    current_ = GridIndex(level_, current_row_, current_col_);
  }

  uint8_t level_ = 255;
  uint32_t from_row_ = 0, to_row_ = 0;
  double west_lon_ = 0.0, east_lon_ = 0.0;
  uint32_t current_row_ = 1, current_col_ = 0;
  uint32_t row_first_col_ = 0, row_last_col_ = 0;
  GridIndex current_;
};

} // namespace gggs

#endif
