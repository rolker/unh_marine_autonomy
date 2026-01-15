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

