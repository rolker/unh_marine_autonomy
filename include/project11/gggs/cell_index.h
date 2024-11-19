#ifndef PROJECT11_GGGS_CELL_INDEX_H
#define PROJECT11_GGGS_CELL_INDEX_H

#include "level_spec.h"

namespace gggs
{
/// Index of a cell within a grid
class CellIndex
{
public:
  CellIndex(){}
  CellIndex(GridIndex grid):grid_index_(grid){}
  CellIndex(GridIndex grid, uint16_t row, uint16_t column):
    grid_index_(grid), row_(row), column_(column)
  {
  }

  // CellIndex(double latitude, double longitude, GridIndex grid)
  // {
  //   initialize(latitude, longitude, grid);
  // }

  CellIndex(GridIndex grid, const gz4d::PositionDegrees &position):
    grid_index_(grid)
  {
    grid_index_ = grid;

    // Note, we constrain to 1.0, but what we really need is up to 1.0.
    double row_p = std::max(0.0, std::min(1.0, position.latitude-grid_index_.southLatitude())/grid_index_.latitudinalSpan());
    // This std::min should filter out 1.0 from above
    row_ = std::min<uint16_t>(cell_rows_per_grid-1, cell_rows_per_grid*row_p);

    double column_p = std::max(0.0, (position.longitude-grid_index_.westLongitude())/grid_index_.longitudinalSpan());
    column_ = std::min<uint16_t>(cell_columns_per_grid-1, cell_columns_per_grid*column_p);
  }

  bool valid() const
  {
    return grid_index_.valid() && row_ < cell_rows_per_grid && column_ < cell_columns_per_grid;
  }

  const GridIndex& grid() const
  {
    return grid_index_;
  }

  uint8_t level() const
  {
    return grid_index_.level();
  }

  const uint16_t &row() const
  {
    return row_;
  }

  const uint16_t &column() const
  {
    return column_;
  }

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

  friend bool operator!=(const CellIndex& lhs, const CellIndex& rhs)
  {
    return !lhs.valid() || !rhs.valid() || lhs.grid_index_ != rhs.grid_index_ ||
      lhs.row_ != rhs.row_ || lhs.column_ != rhs.column_;
  }

  friend bool operator==(const CellIndex& lhs, const CellIndex& rhs)
  {
    return !(lhs != rhs);
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
