// Copyright 2026 Roland Arsenault
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

#ifndef PROJECT11_GGGS_INDEX_MATH_H
#define PROJECT11_GGGS_INDEX_MATH_H

#include <cstddef>
#include <vector>

#include "grid_index.h"
#include "level.h"
#include "grid_area_iterator.h"

namespace gggs
{

/// @brief The coarser-level grid (at `level - 1`) that contains @p child.
///
/// GGGS is a quadtree: each grid at level `L` lies within exactly one grid at
/// level `L-1`. This resolves that parent by looking up the child cell's centre
/// at the coarser level. Going through `Level::gridIndex` (rather than halving
/// row/column directly) keeps the polar `latitudeScaleFactor` (1/3/9) column
/// scaling correct across the latitude-band boundaries.
///
/// @param child A valid grid at level >= 1.
/// @return The containing grid at `child.level() - 1`, or an invalid GridIndex
///         when @p child is invalid or already at level 0 (the coarsest level,
///         which has no parent).
/// @note Grids sitting exactly on a pole (+/-90 deg) are geometrically
///       degenerate and are never produced by `Level::gridIndex`, so they lie
///       outside the supported input domain. For every grid reachable through
///       the normal geographic lookups the `children(parent(g))` set contains
///       `g` (the round-trip invariant the tests check).
inline GridIndex parent(const GridIndex& child)
{
  if (!child.valid() || child.level() == 0)
    return GridIndex();

  const double center_latitude = 0.5 * (child.southLatitude() + child.northLatitude());
  const double center_longitude = 0.5 * (child.westLongitude() + child.eastLongitude());
  return Level(child.level() - 1).gridIndex(center_latitude, center_longitude);
}

/// @brief The finer-level grids (at `level + 1`) that tile @p parent_grid.
///
/// The inverse of parent(): every returned child satisfies `parent(c) ==
/// parent_grid`. In temperate latitude bands this is the expected 2x2 = 4
/// children; where the polar `latitudeScaleFactor` changes the column count
/// between bands, the count differs accordingly. GridAreaIterator computes the
/// per-row column range from longitudes, so the polar case is handled without
/// manual column arithmetic.
///
/// @param parent_grid A valid grid coarser than the finest level.
/// @return The child grids at `parent_grid.level() + 1`, or an empty vector when
///         @p parent_grid is invalid or already at the finest level.
inline std::vector<GridIndex> children(const GridIndex& parent_grid)
{
  std::vector<GridIndex> result;
  if (!parent_grid.valid() ||
      static_cast<std::size_t>(parent_grid.level()) + 1 >= levels.size())
    return result;

  const Level child_level(parent_grid.level() + 1);

  // Sample just inside each corner so the lookup lands within the parent's
  // half-open extent rather than on a neighbouring grid across a shared edge.
  // The inset is far smaller than a child cell span at every level.
  constexpr double kCornerInset = 1e-9;
  const GridIndex south_west = child_level.gridIndex(
    parent_grid.southLatitude() + kCornerInset, parent_grid.westLongitude() + kCornerInset);
  const GridIndex north_east = child_level.gridIndex(
    parent_grid.northLatitude() - kCornerInset, parent_grid.eastLongitude() - kCornerInset);

  for (GridAreaIterator it(south_west, north_east); it.valid(); it.next())
    result.push_back(*it);
  return result;
}

}  // namespace gggs

#endif
