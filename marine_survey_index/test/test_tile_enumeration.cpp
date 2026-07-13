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

#include <gtest/gtest.h>

#include <algorithm>

#include "marine_survey_index/footprint.hpp"

namespace
{

using marine_survey_index::tilesForBoundingBox;

// Lake Massabesic — the survey area this index was built for; mid-latitude,
// far from grid-level latitude bands and the antimeridian.
constexpr double kLat = 43.02;
constexpr double kLon = -71.36;
// Bathy-store native level: L10 grids span 8/2^10 deg ~ 0.0078 deg.
const gggs::Level kLevel(10);

bool contains(const std::vector<gggs::GridIndex> & tiles, const gggs::GridIndex & g)
{
  return std::any_of(tiles.begin(), tiles.end(),
           [&g](const gggs::GridIndex & t) {
             return t.level() == g.level() && t.row() == g.row() && t.column() == g.column();
    });
}

TEST(TileEnumeration, PointInsideOneTileYieldsExactlyThatTile)
{
  const gggs::GridIndex home = kLevel.gridIndex(kLat, kLon);
  // A degenerate box well inside the home tile.
  const double lat = 0.5 * (home.southLatitude() + home.northLatitude());
  const double lon = 0.5 * (home.westLongitude() + home.eastLongitude());
  const auto tiles = tilesForBoundingBox(lat, lon, lat, lon, kLevel);
  ASSERT_EQ(tiles.size(), 1u);
  EXPECT_TRUE(contains(tiles, home));
}

TEST(TileEnumeration, BoxStraddlingEastEdgeYieldsBothTiles)
{
  const gggs::GridIndex home = kLevel.gridIndex(kLat, kLon);
  const double lat = 0.5 * (home.southLatitude() + home.northLatitude());
  const double east = home.eastLongitude();
  const double span = home.eastLongitude() - home.westLongitude();
  // A box reaching a quarter-tile across the shared east edge.
  const auto tiles =
    tilesForBoundingBox(lat, east - 0.25 * span, lat, east + 0.25 * span, kLevel);
  const gggs::GridIndex east_neighbor = kLevel.gridIndex(lat, east + 0.25 * span);
  ASSERT_EQ(tiles.size(), 2u);
  EXPECT_TRUE(contains(tiles, home));
  EXPECT_TRUE(contains(tiles, east_neighbor));
}

TEST(TileEnumeration, BoxStraddlingCornerYieldsFourTiles)
{
  const gggs::GridIndex home = kLevel.gridIndex(kLat, kLon);
  const double north = home.northLatitude();
  const double east = home.eastLongitude();
  const double lat_span = home.northLatitude() - home.southLatitude();
  const double lon_span = home.eastLongitude() - home.westLongitude();
  const auto tiles = tilesForBoundingBox(
    north - 0.25 * lat_span, east - 0.25 * lon_span,
    north + 0.25 * lat_span, east + 0.25 * lon_span, kLevel);
  EXPECT_EQ(tiles.size(), 4u);
  EXPECT_TRUE(contains(tiles, home));
}

TEST(TileEnumeration, MultiTileSwathCountMatchesSpan)
{
  const gggs::GridIndex home = kLevel.gridIndex(kLat, kLon);
  const double lat = 0.5 * (home.southLatitude() + home.northLatitude());
  const double west = home.westLongitude();
  const double span = home.eastLongitude() - home.westLongitude();
  // From inside the home tile eastward across two full neighbours -> 3 tiles
  // in one row.
  const auto tiles =
    tilesForBoundingBox(lat, west + 0.5 * span, lat, west + 2.5 * span, kLevel);
  EXPECT_EQ(tiles.size(), 3u);
  EXPECT_TRUE(contains(tiles, home));
}

TEST(TileEnumeration, SwappedBoundsNormalize)
{
  const gggs::GridIndex home = kLevel.gridIndex(kLat, kLon);
  const double lat = 0.5 * (home.southLatitude() + home.northLatitude());
  const double lon = 0.5 * (home.westLongitude() + home.eastLongitude());
  const auto tiles = tilesForBoundingBox(lat + 0.001, lon + 0.001, lat, lon, kLevel);
  EXPECT_FALSE(tiles.empty());
  EXPECT_TRUE(contains(tiles, home));
}

TEST(TileEnumeration, EveryReturnedTileTouchesTheBox)
{
  // The enumeration must not over-return: each tile's extent overlaps the box.
  const double lat_min = kLat, lat_max = kLat + 0.02;
  const double lon_min = kLon, lon_max = kLon + 0.02;
  const auto tiles = tilesForBoundingBox(lat_min, lon_min, lat_max, lon_max, kLevel);
  ASSERT_FALSE(tiles.empty());
  for (const auto & t : tiles) {
    EXPECT_LE(t.southLatitude(), lat_max);
    EXPECT_GE(t.northLatitude(), lat_min);
    EXPECT_LE(t.westLongitude(), lon_max);
    EXPECT_GE(t.eastLongitude(), lon_min);
  }
}

}  // namespace
