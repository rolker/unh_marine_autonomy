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

#include "marine_survey_index/interval_accumulator.hpp"

namespace
{

using marine_survey_index::IntervalAccumulator;
using marine_survey_index::PassInterval;
using marine_survey_index::TileKey;

constexpr std::int64_t kGap = 5000000000LL;  // 5 s in ns
constexpr std::int64_t kSec = 1000000000LL;

TileKey key(
  std::uint32_t row = 1, std::uint32_t col = 2,
  const char * sensor = "mbes-bathy", const char * topic = "/t")
{
  TileKey k;
  k.level = 10;
  k.tile_row = row;
  k.tile_col = col;
  k.sensor_type = sensor;
  k.topic = topic;
  return k;
}

TEST(IntervalMerge, GapWithinThresholdMerges)
{
  IntervalAccumulator acc(kGap);
  acc.addPing(key(), 0);
  acc.addPing(key(), 2 * kSec);
  acc.addPing(key(), 4 * kSec);
  const auto out = acc.flushAll();
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].t_start_ns, 0);
  EXPECT_EQ(out[0].t_end_ns, 4 * kSec);
  EXPECT_EQ(out[0].ping_count, 3);
}

TEST(IntervalMerge, GapBeyondThresholdSplits)
{
  IntervalAccumulator acc(kGap);
  acc.addPing(key(), 0);
  acc.addPing(key(), 2 * kSec);
  acc.addPing(key(), 20 * kSec);   // > 5 s after the last ping -> new pass
  acc.addPing(key(), 21 * kSec);
  auto out = acc.flushAll();
  ASSERT_EQ(out.size(), 2u);
  std::sort(out.begin(), out.end(),
    [](const PassInterval & a, const PassInterval & b) {return a.t_start_ns < b.t_start_ns;});
  EXPECT_EQ(out[0].t_start_ns, 0);
  EXPECT_EQ(out[0].t_end_ns, 2 * kSec);
  EXPECT_EQ(out[0].ping_count, 2);
  EXPECT_EQ(out[1].t_start_ns, 20 * kSec);
  EXPECT_EQ(out[1].t_end_ns, 21 * kSec);
  EXPECT_EQ(out[1].ping_count, 2);
}

TEST(IntervalMerge, ExactGapBoundaryStillMerges)
{
  IntervalAccumulator acc(kGap);
  acc.addPing(key(), 0);
  acc.addPing(key(), kGap);   // gap == threshold: extends (only > splits)
  const auto out = acc.flushAll();
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].ping_count, 2);
}

TEST(IntervalMerge, DistinctKeysAccumulateIndependently)
{
  IntervalAccumulator acc(kGap);
  acc.addPing(key(1, 2, "mbes-bathy"), 0);
  acc.addPing(key(1, 2, "sidescan-port"), 0);   // same tile, other sensor
  acc.addPing(key(3, 4, "mbes-bathy"), 0);      // other tile, same sensor
  acc.addPing(key(1, 2, "mbes-bathy"), kSec);
  const auto out = acc.flushAll();
  ASSERT_EQ(out.size(), 3u);
  const auto mbes_12 = std::find_if(out.begin(), out.end(),
      [](const PassInterval & p) {
        return p.key.sensor_type == "mbes-bathy" && p.key.tile_row == 1;
    });
  ASSERT_NE(mbes_12, out.end());
  EXPECT_EQ(mbes_12->ping_count, 2);
}

TEST(IntervalMerge, SlightlyOutOfOrderPingExtendsWithoutMovingEndBack)
{
  IntervalAccumulator acc(kGap);
  acc.addPing(key(), 2 * kSec);
  acc.addPing(key(), 1 * kSec);   // interleaving jitter: earlier than open end
  const auto out = acc.flushAll();
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].t_start_ns, 1 * kSec);
  EXPECT_EQ(out[0].t_end_ns, 2 * kSec);
  EXPECT_EQ(out[0].ping_count, 2);
}

TEST(IntervalMerge, FlushResetsForNextBag)
{
  IntervalAccumulator acc(kGap);
  acc.addPing(key(), 0);
  EXPECT_EQ(acc.flushAll().size(), 1u);
  EXPECT_TRUE(acc.flushAll().empty());
  acc.addPing(key(), 100 * kSec);
  EXPECT_EQ(acc.flushAll().size(), 1u);
}

}  // namespace
