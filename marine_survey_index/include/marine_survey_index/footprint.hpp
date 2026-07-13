// Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
// Hydrographic Center, University of New Hampshire
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef MARINE_SURVEY_INDEX__FOOTPRINT_HPP_
#define MARINE_SURVEY_INDEX__FOOTPRINT_HPP_

/// @file
/// @brief Footprint → GGGS-tile enumeration.
///
/// A ping footprint is reduced to its geographic bounding box; this maps the
/// box to the set of GGGS grids (tiles) it overlaps at a level. Straddling a
/// grid boundary yields every touched grid, not just the origin's.

#include <vector>

#include "marine_autonomy/gggs.h"

namespace marine_survey_index
{

/// @brief All GGGS grids at @p level overlapping the geographic bounding box.
///
/// @param lat_min,lat_max Latitude bounds (degrees); swapped inputs are
///   normalized. Values are clamped to [-90, 90].
/// @param lon_min,lon_max Longitude bounds (degrees); swapped inputs are
///   normalized. The box must not cross the antimeridian — survey areas in
///   this workspace (Massabesic, Isles of Shoals) never do; a crossing box
///   would enumerate the long way around.
/// @return The touched grids, in iterator (row-major) order.
std::vector<gggs::GridIndex> tilesForBoundingBox(
  double lat_min, double lon_min, double lat_max, double lon_max,
  const gggs::Level & level);

}  // namespace marine_survey_index

#endif  // MARINE_SURVEY_INDEX__FOOTPRINT_HPP_
