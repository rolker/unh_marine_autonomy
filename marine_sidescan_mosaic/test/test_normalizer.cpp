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

#include <cstdint>
#include <vector>

#include "marine_sidescan_mosaic/normalizer.hpp"

namespace msm = marine_sidescan_mosaic;

TEST(Normalizer, ConstantPingMapsToTarget)
{
  msm::RollingNormalizer norm;   // default target 32768
  std::vector<double> raw(500, 100.0);
  std::vector<std::uint16_t> out;
  norm.normalize(raw, out);
  EXPECT_DOUBLE_EQ(norm.reference(), 100.0);
  for (auto v : out) {
    EXPECT_EQ(v, 32768);
  }
}

TEST(Normalizer, ScalesAndClamps)
{
  msm::RollingNormalizer norm;
  // 90 samples at 100 then 10 at 1000: the 0.85 quantile is 100 -> reference 100.
  std::vector<double> raw(90, 100.0);
  raw.insert(raw.end(), 10, 1000.0);
  std::vector<std::uint16_t> out;
  norm.normalize(raw, out);
  EXPECT_DOUBLE_EQ(norm.reference(), 100.0);
  EXPECT_EQ(out.front(), 32768);     // a 100 -> target
  EXPECT_EQ(out.back(), 65535);      // a 1000 -> 327680, clamped
}

TEST(Normalizer, AllDarkPingIsSafe)
{
  msm::RollingNormalizer norm;
  std::vector<double> raw(200, 0.0);
  std::vector<std::uint16_t> out;
  norm.normalize(raw, out);
  EXPECT_GE(norm.reference(), 1.0);  // floored, no divide-by-zero
  for (auto v : out) {
    EXPECT_EQ(v, 0);
  }
}

TEST(Normalizer, ReferenceRollsBetweenPings)
{
  msm::RollingNormalizer norm;   // ema_alpha 0.05
  std::vector<std::uint16_t> out;
  norm.normalize(std::vector<double>(100, 100.0), out);
  EXPECT_DOUBLE_EQ(norm.reference(), 100.0);       // first ping initializes
  norm.normalize(std::vector<double>(100, 200.0), out);
  EXPECT_DOUBLE_EQ(norm.reference(), 105.0);        // 0.05*200 + 0.95*100
}

TEST(Normalizer, EmptyPing)
{
  msm::RollingNormalizer norm;
  std::vector<double> raw;
  std::vector<std::uint16_t> out{1, 2, 3};
  norm.normalize(raw, out);
  EXPECT_TRUE(out.empty());
  EXPECT_LT(norm.reference(), 0.0);                 // never initialized
}
