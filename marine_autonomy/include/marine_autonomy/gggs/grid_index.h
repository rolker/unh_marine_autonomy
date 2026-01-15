#ifndef PROJECT11_GGGS_GRID_INDEX_H
#define PROJECT11_GGGS_GRID_INDEX_H

#include "level_spec.h"

namespace gggs
{
// Index for a Global Geographic Grid System grid.
class GridIndex
{
public:
  GridIndex(){}

  // GridIndex(double latitude, double longitude, uint8_t level) : level_(level)
  // {
  //   row_ = (latitude + 96.0) / levels[level].grid_angular_span;
  //   column_ = (longitude + 180.0) / (levels[level].grid_angular_span * latitudeScaleFactor(latitude));
  // }

  bool valid() const
  {
    return level_ < levels.size() && row_ < levels[level_].row_count && column_ < levels[level_].columnCount(row_);
  }

  uint8_t level() const
  {
    return level_;
  }

  const uint32_t &row() const
  {
    return row_;
  }

  const uint32_t &column() const
  {
    return column_;
  }

  static uint16_t cellRowCount()
  {
    return cell_rows_per_grid;
  }

  static uint16_t cellColumnCount()
  {
    return cell_columns_per_grid;
  }

  double northLatitude() const
  {
    return std::max(-90.0, -96.0+(row_+1)*levels[level_].grid_angular_span);
  }

  double southLatitude() const
  {
    return std::max(-90.0, -96.0+row_*levels[level_].grid_angular_span);
  }

  double eastLongitude() const
  {
    return -180.0+(column_+1)*levels[level_].gridLongitudinalSpan(row_);
  }

  double westLongitude() const
  {
    return -180.0+column_*levels[level_].gridLongitudinalSpan(row_);
  }

  gz4d::PositionDegrees southWestPosition() const
  {
    return gz4d::PositionDegrees(southLatitude(), westLongitude());
  }

  gz4d::PositionDegrees northEastPosition() const
  {
    return gz4d::PositionDegrees(northLatitude(), eastLongitude());
  }

  double latitudinalSpan() const
  {
    return levels[level_].grid_angular_span;
  }

  double longitudinalSpan() const
  {
    return levels[level_].gridLongitudinalSpan(row_);
  }

  /// Less than operator allowing use as std::map key.
  friend bool operator<(const GridIndex& lhs, const GridIndex& rhs)
  {
    if (lhs.level_ != rhs.level_)
      return lhs.level_ < rhs.level_;
    if (lhs.row_ != rhs.row_)
      return lhs.row_ < rhs.row_;
    return lhs.column_ < rhs.column_;
  }

  friend bool operator!=(const GridIndex& lhs, const GridIndex& rhs)
  {
    return !lhs.valid() || !rhs.valid() || lhs.level_ != rhs.level_ ||
      lhs.row_ != rhs.row_ || lhs.column_ != rhs.column_;
  }

  friend bool operator==(const GridIndex& lhs, const GridIndex& rhs)
  {
    return !(lhs != rhs);
  }

  friend GridIndex max(const GridIndex& lhs, const GridIndex& rhs)
  {
    verifySameLevel(lhs, rhs);
    return GridIndex(lhs.level_, std::max(lhs.row_, rhs.row_), std::max(lhs.column_, rhs.column_));
  }

  friend GridIndex min(const GridIndex& lhs, const GridIndex& rhs)
  {
    verifySameLevel(lhs, rhs);
    return GridIndex(lhs.level_, std::min(lhs.row_, rhs.row_), std::min(lhs.column_, rhs.column_));
  }


  friend std::ostream& operator<< (std::ostream& stream, const GridIndex& index)
  {
    stream << "GridIndex level " << int(index.level_) << " row: " << index.row_ << " col: " << index.column_;
    return stream;
  }

private:
  GridIndex(uint8_t level, uint32_t row, uint32_t column):
    level_(level), row_(row), column_(column)
  {}

  friend class Level;
  friend class GridAreaIterator;

  /// Level of the quad tree where 0 is top with 8 degree tiles
  uint8_t level_ = 255;

  /// Positive integer row starting -96 degrees.
  /// The -96 degrees starting value accounts for rows by the poles only being 2 degrees.
  uint32_t row_ = 0;

  /// Positive integer column starting at -180 degrees.
  uint32_t column_ = 0;
};


}

#endif
