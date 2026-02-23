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

#ifndef PROJECT11_GGGS_LEVEL_H
#define PROJECT11_GGGS_LEVEL_H

#include "cell_index.h"

namespace gggs
{

/// Represents a level in the quadtree where level 0 is 8 degrees square
/// (except close to the poles).
class Level
{
public:
  Level(uint8_t level):level_(level){}

  /// Constructs an instance where the level supports requested cell_size
  /// or smaller
  static Level fromCellSize(float cell_size)
  {
    auto grid_size = cell_size * cell_rows_per_grid;
    return Level(std::ceil(std::log2(level_0_grid_size / grid_size)));
  }

  /// Approximate cell size in meters
  double cellSize() const
  {
    return levels[level_].nominal_cell_size;
  }

  GridIndex gridIndex(double latitude, double longitude) const
  {
    uint32_t row = (latitude + 96.0) / levels[level_].grid_angular_span;
    uint32_t column = (longitude + 180.0) / (levels[level_].grid_angular_span * latitudeScaleFactor(latitude));

    return GridIndex(level_, row, column);
  }

  GridIndex gridIndex(const gz4d::PositionDegrees & position) const
  {
    return gridIndex(position.latitude, position.longitude);
  }

  CellIndex cellIndex(const gz4d::PositionDegrees & position) const
  {
    return CellIndex(gridIndex(position), position);
  }

  /// @brief Angular span of a cell
  /// @return Degrees (in latitude direction, not accounting for polar scales)
  double cellAngularSpan() const
  {
    return levels[level_].cell_angular_span;
  }

protected:
  /// Level of the quad tree where 0 is top with 8 degree tiles
  uint8_t level_ = 0;
};

}

#endif

