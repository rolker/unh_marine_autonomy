#ifndef PROJECT11_GGGS_LEVEL_SPEC_H
#define PROJECT11_GGGS_LEVEL_SPEC_H

#include "core.h"

namespace gggs
{
/// Metadata for a given level in the quadtree
class LevelSpecs
{
public:
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

  uint8_t latitudeScaleFactor(uint32_t row) const
  {
    if(row >= row_minus_72 && row < row_plus_72)
      return 1;
    if(row >= row_minus_80 && row < row_plus_80)
      return 3;
    return 9;
  }

  double gridLongitudinalSpan(uint32_t row) const
  {
    return grid_angular_span*latitudeScaleFactor(row);
  }

  uint32_t columnCount(uint32_t row) const
  {
    return column_count/latitudeScaleFactor(row);
  }

  uint8_t level;

  // How many degrees a grid spans at this level
  double grid_angular_span;

  // How many degrees a grid cell spans at this level
  double cell_angular_span;

  // Estimate of the size in meters of a grid at this level
  double nominal_grid_size;
  // Estimate of the size in meters of a grid cell at this level
  double nominal_cell_size;

  uint32_t row_count;
  uint32_t column_count;

  uint32_t row_minus_80;
  uint32_t row_minus_72;
  uint32_t row_plus_72;
  uint32_t row_plus_80;
};

// cache the metadata speed up index calculations
inline std::array<LevelSpecs, 21> levels = {{
  LevelSpecs(0),  LevelSpecs(1),  LevelSpecs(2),  LevelSpecs(3),  LevelSpecs(4),  LevelSpecs(5),
  LevelSpecs(6),  LevelSpecs(7),  LevelSpecs(8),  LevelSpecs(9),  LevelSpecs(10), LevelSpecs(11),
  LevelSpecs(12), LevelSpecs(13), LevelSpecs(14), LevelSpecs(15), LevelSpecs(16), LevelSpecs(17),
  LevelSpecs(18), LevelSpecs(19), LevelSpecs(20),
}};

} // namespace gggs

#endif

