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

#include <string>
#include <vector>

#include "marine_survey_index/footprint.hpp"
#include "marine_survey_index/query.hpp"
#include "marine_survey_index/schema.hpp"

namespace
{

// The query join, exercised against an in-memory index with known content:
// point and box lookups, the sensor filter (including 'sidescan' matching
// both channels), and the tile-mismatch negative case.

constexpr double kLat = 43.02;
constexpr double kLon = -71.36;

class QueryJoin : public testing::Test
{
protected:
  void SetUp() override
  {
    db_ = marine_survey_index::openIndexDb(":memory:");
    exec("INSERT INTO bags (id, path, size_bytes, mtime_ns, indexed_at_ns)"
      " VALUES (1, '/data/bagA', 10, 0, 0), (2, '/data/bagB', 20, 0, 0)");

    // Home tile at each sensor's native level.
    home10_ = gggs::Level(10).gridIndex(kLat, kLon);
    home13_ = gggs::Level(13).gridIndex(kLat, kLon);

    // bagA: an MBES pass (L10) + a port sidescan pass (L13) over the point.
    insertPass(1, home10_, "mbes-bathy", "/m3", 100, 200, 50);
    insertPass(1, home13_, "sidescan-port", "/ss_port", 100, 190, 40);
    // bagB: a later stbd sidescan pass (L13) over the point, and an MBES pass
    // over a far-away tile (must never match the point query).
    insertPass(2, home13_, "sidescan-stbd", "/ss_stbd", 1000, 1100, 60);
    const auto far10 = gggs::Level(10).gridIndex(kLat + 1.0, kLon + 1.0);
    insertPass(2, far10, "mbes-bathy", "/m3", 5000, 5100, 70);
  }

  void TearDown() override {sqlite3_close(db_);}

  void exec(const std::string & sql)
  {
    ASSERT_EQ(sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, nullptr), SQLITE_OK)
      << sqlite3_errmsg(db_);
  }

  void insertPass(
    int bag_id, const gggs::GridIndex & tile, const std::string & sensor,
    const std::string & topic, std::int64_t t0, std::int64_t t1, int pings)
  {
    exec(
      "INSERT INTO passes (bag_id, level, tile_row, tile_col, sensor_type, topic,"
      " t_start_ns, t_end_ns, ping_count) VALUES (" +
      std::to_string(bag_id) + ", " + std::to_string(tile.level()) + ", " +
      std::to_string(tile.row()) + ", " + std::to_string(tile.column()) + ", '" +
      sensor + "', '" + topic + "', " + std::to_string(t0) + ", " +
      std::to_string(t1) + ", " + std::to_string(pings) + ")");
  }

  // Tiles covering the query point at every level in the index (the CLI's
  // point-query path with no --level).
  std::vector<gggs::GridIndex> pointTiles(const std::string & sensor = "")
  {
    std::vector<gggs::GridIndex> tiles;
    for (const auto level : marine_survey_index::distinctLevels(db_, sensor)) {
      const auto per_level = marine_survey_index::tilesForBoundingBox(
        kLat, kLon, kLat, kLon, gggs::Level(level));
      tiles.insert(tiles.end(), per_level.begin(), per_level.end());
    }
    return tiles;
  }

  sqlite3 * db_ = nullptr;
  gggs::GridIndex home10_;
  gggs::GridIndex home13_;
};

TEST_F(QueryJoin, PointQueryFindsAllSensorsAcrossLevelsAndBags)
{
  const auto rows = marine_survey_index::queryPasses(db_, pointTiles(), "");
  ASSERT_EQ(rows.size(), 3u);   // far-away tile excluded
  EXPECT_EQ(rows[0].bag_path, "/data/bagA");
  EXPECT_EQ(rows[1].bag_path, "/data/bagA");
  EXPECT_EQ(rows[2].bag_path, "/data/bagB");
  // Ordered by bag then t_start within bag.
  EXPECT_LE(rows[0].t_start_ns, rows[1].t_start_ns);
}

TEST_F(QueryJoin, SensorFilterExactMatch)
{
  const auto rows = marine_survey_index::queryPasses(db_, pointTiles(), "mbes-bathy");
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].sensor_type, "mbes-bathy");
  EXPECT_EQ(rows[0].ping_count, 50);
}

TEST_F(QueryJoin, SidescanFilterMatchesBothChannels)
{
  const auto rows = marine_survey_index::queryPasses(db_, pointTiles(), "sidescan");
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].sensor_type, "sidescan-port");
  EXPECT_EQ(rows[1].sensor_type, "sidescan-stbd");
}

TEST_F(QueryJoin, DistinctLevelsRespectsSensorFilter)
{
  const auto all = marine_survey_index::distinctLevels(db_, "");
  ASSERT_EQ(all.size(), 2u);
  EXPECT_EQ(all[0], 10);
  EXPECT_EQ(all[1], 13);
  const auto ss_only = marine_survey_index::distinctLevels(db_, "sidescan");
  ASSERT_EQ(ss_only.size(), 1u);
  EXPECT_EQ(ss_only[0], 13);
}

TEST_F(QueryJoin, NonOverlappingPointFindsNothing)
{
  std::vector<gggs::GridIndex> tiles;
  for (const auto level : marine_survey_index::distinctLevels(db_, "")) {
    const auto per_level = marine_survey_index::tilesForBoundingBox(
      kLat - 0.5, kLon - 0.5, kLat - 0.5, kLon - 0.5, gggs::Level(level));
    tiles.insert(tiles.end(), per_level.begin(), per_level.end());
  }
  EXPECT_TRUE(marine_survey_index::queryPasses(db_, tiles, "").empty());
}

TEST_F(QueryJoin, EmptyTileSetReturnsEmpty)
{
  EXPECT_TRUE(marine_survey_index::queryPasses(db_, {}, "").empty());
}

class NavTrackQuery : public QueryJoin
{
protected:
  void SetUp() override
  {
    QueryJoin::SetUp();
    // bagA: three points marching east; bagB: one point nearby and one far
    // away (outside any box around kLat/kLon). Inserted out of time order to
    // prove the accessors sort.
    insertNav(1, 300, kLat, kLon + 0.0002);
    insertNav(1, 100, kLat, kLon);
    insertNav(1, 200, kLat, kLon + 0.0001);
    insertNav(2, 150, kLat + 0.0001, kLon);
    insertNav(2, 250, kLat + 1.0, kLon + 1.0);
  }

  void insertNav(int bag_id, std::int64_t t_ns, double lat, double lon)
  {
    exec(
      "INSERT INTO nav_track (bag_id, t_ns, latitude, longitude) VALUES (" +
      std::to_string(bag_id) + ", " + std::to_string(t_ns) + ", " +
      std::to_string(lat) + ", " + std::to_string(lon) + ")");
  }
};

TEST_F(NavTrackQuery, PerBagTrackIsTimeOrderedAndBagFiltered)
{
  const auto track = marine_survey_index::queryNavTrack(db_, 1);
  ASSERT_EQ(track.size(), 3u);
  EXPECT_EQ(track[0].t_ns, 100);
  EXPECT_EQ(track[1].t_ns, 200);
  EXPECT_EQ(track[2].t_ns, 300);
  for (const auto & p : track) {
    EXPECT_EQ(p.bag_id, 1);
  }
  EXPECT_DOUBLE_EQ(track[0].latitude, kLat);
  EXPECT_DOUBLE_EQ(track[0].longitude, kLon);
}

TEST_F(NavTrackQuery, UnknownBagYieldsEmptyTrack)
{
  EXPECT_TRUE(marine_survey_index::queryNavTrack(db_, 999).empty());
}

TEST_F(NavTrackQuery, BoxQueryReturnsInsidePointsOrderedByBagThenTime)
{
  const auto points = marine_survey_index::queryNavTrackInBox(
    db_, kLat - 0.01, kLon - 0.01, kLat + 0.01, kLon + 0.01);
  ASSERT_EQ(points.size(), 4u);   // bagB's far point excluded
  EXPECT_EQ(points[0].bag_id, 1);
  EXPECT_EQ(points[0].t_ns, 100);
  EXPECT_EQ(points[1].t_ns, 200);
  EXPECT_EQ(points[2].t_ns, 300);
  EXPECT_EQ(points[3].bag_id, 2);
  EXPECT_EQ(points[3].t_ns, 150);
}

TEST_F(NavTrackQuery, ReversedBoxIsSwapped)
{
  const auto points = marine_survey_index::queryNavTrackInBox(
    db_, kLat + 0.01, kLon + 0.01, kLat - 0.01, kLon - 0.01);
  EXPECT_EQ(points.size(), 4u);
}

TEST_F(NavTrackQuery, OutOfRangeLatitudesAreClamped)
{
  // Same clamp-into-[-90, 90] semantics as tilesForBoundingBox: beyond-pole
  // bounds behave like the poles, they do not throw or change the result.
  const auto points = marine_survey_index::queryNavTrackInBox(
    db_, -100.0, kLon - 0.01, 100.0, kLon + 0.01);
  EXPECT_EQ(points.size(), 4u);   // far point excluded by longitude only
}

TEST_F(NavTrackQuery, AntimeridianBoxThrows)
{
  // 170 E .. -170 E normalizes to a >180-degree span: the same fail-loud
  // contract as tilesForBoundingBox.
  EXPECT_THROW(
    marine_survey_index::queryNavTrackInBox(db_, 40.0, 170.0, 45.0, -170.0),
    std::invalid_argument);
}

TEST_F(QueryJoin, WrongLevelSameRowColDoesNotMatch)
{
  // A tile key is (level, row, col) — the same row/col at a different level
  // must not join. Query with the L10 home tile's row/col but claim L13.
  std::vector<gggs::GridIndex> tiles = {gggs::Level(13).gridIndex(
      0.5 * (home10_.southLatitude() + home10_.northLatitude()),
      0.5 * (home10_.westLongitude() + home10_.eastLongitude()))};
  const auto rows = marine_survey_index::queryPasses(db_, tiles, "mbes-bathy");
  EXPECT_TRUE(rows.empty());   // L13 tile over the point holds no mbes pass
}

}  // namespace
