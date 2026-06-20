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

#include <sstream>
#include <vector>

#include "marine_sidescan_mosaic/tier1.hpp"

using marine_sidescan_mosaic::readTier1Header;
using marine_sidescan_mosaic::readTier1Ping;
using marine_sidescan_mosaic::Tier1Channel;
using marine_sidescan_mosaic::Tier1Ping;
using marine_sidescan_mosaic::writeTier1Header;
using marine_sidescan_mosaic::writeTier1Ping;

namespace
{
Tier1Ping makePing()
{
  Tier1Ping p;
  p.stamp_ns = 1781234567890123456LL;   // near-now ns, needs the full int64 range.
  p.channel = Tier1Channel::Starboard;
  p.tx = 1.5e6; p.ty = -2.5e6; p.tz = 5.6e6;
  p.qx = 0.1; p.qy = 0.2; p.qz = 0.3; p.qw = 0.927;
  p.sound_speed = 1481.3;
  p.sample_rate = 51202.0;
  p.sample0 = 13;
  p.nadir_altitude_m = 2.08F;
  p.samples = {0.0F, 1.0F, 65535.0F, 42.5F};
  return p;
}
}  // namespace

TEST(Tier1, HeaderAndRecordRoundTrip)
{
  const Tier1Ping a = makePing();
  std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
  writeTier1Header(ss);
  writeTier1Ping(ss, a);

  ASSERT_TRUE(readTier1Header(ss));
  Tier1Ping b;
  ASSERT_TRUE(readTier1Ping(ss, b));

  EXPECT_EQ(b.stamp_ns, a.stamp_ns);   // exact: int64 ns, no float rounding.
  EXPECT_EQ(b.channel, a.channel);
  EXPECT_DOUBLE_EQ(b.tx, a.tx);
  EXPECT_DOUBLE_EQ(b.ty, a.ty);
  EXPECT_DOUBLE_EQ(b.tz, a.tz);
  EXPECT_DOUBLE_EQ(b.qw, a.qw);
  EXPECT_DOUBLE_EQ(b.sound_speed, a.sound_speed);
  EXPECT_DOUBLE_EQ(b.sample_rate, a.sample_rate);
  EXPECT_EQ(b.sample0, a.sample0);
  EXPECT_FLOAT_EQ(b.nadir_altitude_m, a.nadir_altitude_m);
  EXPECT_EQ(b.samples, a.samples);

  // A second read on an exhausted stream is a clean EOF, not a partial record.
  Tier1Ping c;
  EXPECT_FALSE(readTier1Ping(ss, c));
}

TEST(Tier1, RejectsBadMagic)
{
  std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
  const std::uint32_t junk = 0xDEADBEEFu;
  ss.write(reinterpret_cast<const char *>(&junk), sizeof(junk));
  ss.write(reinterpret_cast<const char *>(&junk), sizeof(junk));
  EXPECT_FALSE(readTier1Header(ss));
}

TEST(Tier1, EmptySamplesRoundTrip)
{
  Tier1Ping a = makePing();
  a.samples.clear();
  std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
  writeTier1Header(ss);
  writeTier1Ping(ss, a);
  ASSERT_TRUE(readTier1Header(ss));
  Tier1Ping b;
  ASSERT_TRUE(readTier1Ping(ss, b));
  EXPECT_TRUE(b.samples.empty());
}
