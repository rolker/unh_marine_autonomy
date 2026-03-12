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

#ifndef PROJECT11_GGGS_BOUNDS_H
#define PROJECT11_GGGS_BOUNDS_H

#include "grid_index.h"

namespace gggs
{

/// @brief Axis-aligned bounding box over GridIndex values at a single level.
///
/// Tracks row range and geographic longitude extent as grids are added
/// via expand(). Uses per-row column counts to correctly handle polar
/// latitude bands where column counts change.
class GridBounds
{
public:
  /// @brief Check if bounds contain at least one valid grid.
  bool valid() const
  {
    return level_ < levels.size();
  }

  /// @brief Expand bounds to include the given grid.
  /// @param index Grid to include. Must be at the same level as existing bounds.
  /// @throws std::out_of_range if levels differ.
  void expand(const GridIndex& index)
  {
    if(index.valid())
    {
      if(valid())
      {
        verifySameLevel(minimum(), index);
        min_row_ = std::min(min_row_, index.row());
        max_row_ = std::max(max_row_, index.row());
        west_lon_ = std::min(west_lon_, index.westLongitude());
        east_lon_ = std::max(east_lon_, index.eastLongitude());
      }
      else
      {
        level_ = index.level();
        min_row_ = index.row();
        max_row_ = index.row();
        west_lon_ = index.westLongitude();
        east_lon_ = index.eastLongitude();
      }
    }
  }

  GridIndex minimum() const
  {
    if(!valid())
      return GridIndex();
    return GridIndex(level_, min_row_, firstColumn(min_row_));
  }

  GridIndex maximum() const
  {
    if(!valid())
      return GridIndex();
    return GridIndex(level_, max_row_, lastColumn(max_row_));
  }

  uint32_t gridRowCount() const
  {
    if(!valid())
      return 0;
    return 1 + max_row_ - min_row_;
  }

  uint32_t gridColumnCount(uint32_t row) const
  {
    if(!valid())
      return 0;
    return 1 + lastColumn(row) - firstColumn(row);
  }

  uint64_t cellRowCount() const
  {
    return gridRowCount() * cell_rows_per_grid;
  }

  uint64_t cellColumnCount(uint32_t row) const
  {
    return gridColumnCount(row) * cell_columns_per_grid;
  }

  friend std::ostream& operator<< (std::ostream& stream, const GridBounds& bounds)
  {
    if(bounds.valid())
      stream << "min: " << bounds.minimum() << " max: " << bounds.maximum();
    else
      stream << "invalid GridBounds";
    return stream;
  }

private:
  uint32_t firstColumn(uint32_t row) const
  {
    double span = levels[level_].gridLongitudinalSpan(row);
    return static_cast<uint32_t>((west_lon_ + 180.0) / span);
  }

  uint32_t lastColumn(uint32_t row) const
  {
    double span = levels[level_].gridLongitudinalSpan(row);
    uint32_t col = static_cast<uint32_t>((east_lon_ + 180.0) / span);
    // east_lon_ is the east edge of a grid; if it falls exactly on a boundary,
    // it belongs to the previous column
    if(east_lon_ + 180.0 > 0.0 && std::fmod(east_lon_ + 180.0, span) < 1e-10)
      col--;
    return col;
  }

  uint8_t level_ = 255;
  uint32_t min_row_ = 0;
  uint32_t max_row_ = 0;
  double west_lon_ = 0.0;
  double east_lon_ = 0.0;
};

}

#endif
