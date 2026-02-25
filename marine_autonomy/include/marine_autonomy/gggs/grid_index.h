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

#ifndef PROJECT11_GGGS_GRID_INDEX_H
#define PROJECT11_GGGS_GRID_INDEX_H

#include "level_spec.h"

namespace gggs
{
/// @brief Index identifying a single grid in the GGGS quadtree.
///
/// A GridIndex uniquely identifies a grid by its quadtree level, row, and column.
/// Rows are numbered from south to north starting at -96° latitude; columns
/// are numbered from west to east starting at -180° longitude. A default-
/// constructed GridIndex is invalid (sentinel value).
class GridIndex
{
public:
  /// @brief Construct an invalid (sentinel) GridIndex.
  GridIndex(){}

  // GridIndex(double latitude, double longitude, uint8_t level) : level_(level)
  // {
  //   row_ = (latitude + 96.0) / levels[level].grid_angular_span;
  //   column_ = (longitude + 180.0) / (levels[level].grid_angular_span * latitudeScaleFactor(latitude));
  // }

  /// @brief Check if this index refers to an existing grid.
  /// @return true if level, row, and column are within valid ranges.
  bool valid() const noexcept
  {
    return level_ < levels.size() && row_ < levels[level_].row_count && column_ < levels[level_].columnCount(row_);
  }

  constexpr uint8_t level() const noexcept
  {
    return level_;
  }

  constexpr const uint32_t &row() const noexcept
  {
    return row_;
  }

  constexpr const uint32_t &column() const noexcept
  {
    return column_;
  }

  static constexpr uint16_t cellRowCount() noexcept
  {
    return cell_rows_per_grid;
  }

  static constexpr uint16_t cellColumnCount() noexcept
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

  friend bool operator==(const GridIndex& lhs, const GridIndex& rhs)
  {
    if (!lhs.valid() && !rhs.valid())
      return true;
    if (!lhs.valid() || !rhs.valid())
      return false;
    return lhs.level_ == rhs.level_ &&
      lhs.row_ == rhs.row_ && lhs.column_ == rhs.column_;
  }

  friend bool operator!=(const GridIndex& lhs, const GridIndex& rhs)
  {
    return !(lhs == rhs);
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
