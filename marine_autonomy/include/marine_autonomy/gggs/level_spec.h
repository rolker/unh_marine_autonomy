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

#ifndef PROJECT11_GGGS_LEVEL_SPEC_H
#define PROJECT11_GGGS_LEVEL_SPEC_H

#include "core.h"

namespace gggs
{
/// @brief Pre-computed metadata for a single quadtree level.
///
/// Stores angular spans, nominal sizes, row/column counts, and polar-boundary
/// row indices to avoid repeated computation during grid lookups.
class LevelSpecs
{
public:
  /// @brief Compute and cache all metadata for the given level.
  /// @param level Quadtree level (0-20).
  LevelSpecs(uint8_t level):level(level)
  {
    auto multiplier = pow(2, level);
    row_count = level_0_row_count*multiplier;
    column_count = level_0_column_count*multiplier;

    double shrink_factor = 1.0/multiplier;
    grid_angular_span = 8.0*shrink_factor;
    cell_angular_span = grid_angular_span / cell_rows_per_grid;
    nominal_grid_size = level_0_grid_size*shrink_factor;
    nominal_cell_size = nominal_grid_size / cell_rows_per_grid;

    row_minus_80 = (-80.0+96.0)/grid_angular_span;
    row_minus_72 = (-72.0+96.0)/grid_angular_span;
    row_plus_72 = (72.0+96.0)/grid_angular_span;
    row_plus_80 = (80.0+96.0)/grid_angular_span;
  }

  /// @brief Compute latitude scale factor from a grid row index.
  /// @param row Grid row at this level.
  /// @return 1, 3, or 9 depending on latitude band.
  uint8_t latitudeScaleFactor(uint32_t row) const
  {
    if(row >= row_minus_72 && row < row_plus_72)
      return 1;
    if(row >= row_minus_80 && row < row_plus_80)
      return 3;
    return 9;
  }

  /// @brief Longitudinal span of a grid in degrees at the given row.
  /// @param row Grid row at this level.
  /// @return Angular span in degrees (wider near the poles).
  double gridLongitudinalSpan(uint32_t row) const
  {
    return grid_angular_span*latitudeScaleFactor(row);
  }

  /// @brief Number of grid columns at the given row.
  /// @param row Grid row at this level.
  /// @return Column count (fewer near the poles due to wider grids).
  uint32_t columnCount(uint32_t row) const
  {
    return column_count/latitudeScaleFactor(row);
  }

  uint8_t level;                 ///< Quadtree level (0-20).

  double grid_angular_span;      ///< Angular span of a grid in degrees.
  double cell_angular_span;      ///< Angular span of a cell in degrees.

  double nominal_grid_size;      ///< Approximate grid size in meters at the equator.
  double nominal_cell_size;      ///< Approximate cell size in meters at the equator.

  uint32_t row_count;            ///< Total number of grid rows at this level.
  uint32_t column_count;         ///< Total number of grid columns at the equator.

  uint32_t row_minus_80;         ///< Row index of the -80° latitude boundary.
  uint32_t row_minus_72;         ///< Row index of the -72° latitude boundary.
  uint32_t row_plus_72;          ///< Row index of the +72° latitude boundary.
  uint32_t row_plus_80;          ///< Row index of the +80° latitude boundary.
};

/// @brief Pre-computed metadata for all 21 quadtree levels (0-20).
inline std::array<LevelSpecs, 21> levels = {{
  LevelSpecs(0),  LevelSpecs(1),  LevelSpecs(2),  LevelSpecs(3),  LevelSpecs(4),  LevelSpecs(5),
  LevelSpecs(6),  LevelSpecs(7),  LevelSpecs(8),  LevelSpecs(9),  LevelSpecs(10), LevelSpecs(11),
  LevelSpecs(12), LevelSpecs(13), LevelSpecs(14), LevelSpecs(15), LevelSpecs(16), LevelSpecs(17),
  LevelSpecs(18), LevelSpecs(19), LevelSpecs(20),
}};

} // namespace gggs

#endif

