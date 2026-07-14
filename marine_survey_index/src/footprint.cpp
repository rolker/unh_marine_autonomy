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

#include "marine_survey_index/footprint.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace marine_survey_index
{

std::vector<gggs::GridIndex> tilesForBoundingBox(
  double lat_min, double lon_min, double lat_max, double lon_max,
  const gggs::Level & level)
{
  if (lat_min > lat_max) {
    std::swap(lat_min, lat_max);
  }
  lon_min = gggs::normalizeLongitude(lon_min);
  lon_max = gggs::normalizeLongitude(lon_max);
  if (lon_min > lon_max) {
    std::swap(lon_min, lon_max);
  }
  // Antimeridian-crossing boxes are unsupported (see the header contract).
  // After normalization into [-180, 180) a crossing box always presents as a
  // >180-degree span — without this guard it would silently enumerate the
  // long way around (a near-whole-world tile set at fine levels). Fail loud;
  // the indexer's per-bag error isolation and the query CLI both surface it.
  if (lon_max - lon_min > 180.0) {
    throw std::invalid_argument(
      "tilesForBoundingBox: longitude span " + std::to_string(lon_max - lon_min) +
      " deg exceeds 180 — boxes crossing the antimeridian are not supported");
  }
  lat_min = std::clamp(lat_min, -90.0, 90.0);
  lat_max = std::clamp(lat_max, -90.0, 90.0);

  const gggs::GridIndex south_west = level.gridIndex(lat_min, lon_min);
  const gggs::GridIndex north_east = level.gridIndex(lat_max, lon_max);

  std::vector<gggs::GridIndex> tiles;
  for (gggs::GridAreaIterator it(south_west, north_east); it.valid(); it.next()) {
    tiles.push_back(*it);
  }
  return tiles;
}

}  // namespace marine_survey_index
