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

#include <cmath>

#include "marine_survey_index/nav_decimation.hpp"

namespace
{

using marine_survey_index::NavDecimator;
using marine_survey_index::haversineMeters;

// One degree of arc on the mean-radius sphere (R = 6371008.8 m):
// pi * R / 180.
constexpr double kMetersPerDegree = 111195.0797343687;

// At the equator, 1e-4 deg of longitude is ~11.12 m — comfortably over a
// 10 m stride; 5e-5 deg (~5.56 m) is comfortably under it.
constexpr double kOverStrideDeg = 1e-4;
constexpr double kUnderStrideDeg = 5e-5;

TEST(HaversineMeters, ZeroForIdenticalPoints)
{
  EXPECT_DOUBLE_EQ(haversineMeters(43.02, -71.36, 43.02, -71.36), 0.0);
}

TEST(HaversineMeters, OneDegreeOfLatitude)
{
  EXPECT_NEAR(haversineMeters(0.0, 0.0, 1.0, 0.0), kMetersPerDegree, 0.01);
  // Latitude arcs are great circles at any longitude and any latitude band.
  EXPECT_NEAR(haversineMeters(42.0, -71.0, 43.0, -71.0), kMetersPerDegree, 0.01);
}

TEST(HaversineMeters, LongitudeShrinksWithCosLatitude)
{
  EXPECT_NEAR(haversineMeters(0.0, 0.0, 0.0, 1.0), kMetersPerDegree, 0.01);
  // At 60 N a longitude degree is half an equatorial one (cos 60 = 0.5);
  // the chord-vs-arc difference at 1 degree is under a metre.
  EXPECT_NEAR(haversineMeters(60.0, 0.0, 60.0, 1.0), kMetersPerDegree / 2.0, 1.0);
}

TEST(HaversineMeters, Symmetric)
{
  EXPECT_DOUBLE_EQ(
    haversineMeters(43.02, -71.36, 43.03, -71.35),
    haversineMeters(43.03, -71.35, 43.02, -71.36));
}

TEST(NavDecimator, FirstPointAlwaysAccepted)
{
  NavDecimator gate(10.0);
  EXPECT_TRUE(gate.accept(43.02, -71.36));
}

TEST(NavDecimator, BelowStrideRejectedAtOrAboveAccepted)
{
  NavDecimator gate(10.0);
  ASSERT_TRUE(gate.accept(0.0, 0.0));
  EXPECT_FALSE(gate.accept(0.0, kUnderStrideDeg));
  EXPECT_TRUE(gate.accept(0.0, kOverStrideDeg));
}

TEST(NavDecimator, ReferenceIsLastAcceptedNotLastSeen)
{
  // Two sub-stride steps must not accumulate into an acceptance: after the
  // rejection at 5.56 m, the reference is still the origin, so a second
  // point 11.12 m from the origin is what finally passes.
  NavDecimator gate(10.0);
  ASSERT_TRUE(gate.accept(0.0, 0.0));
  ASSERT_FALSE(gate.accept(0.0, kUnderStrideDeg));
  EXPECT_TRUE(gate.accept(0.0, 2.0 * kUnderStrideDeg));
}

TEST(NavDecimator, StationKeepingAddsNoPoints)
{
  NavDecimator gate(10.0);
  ASSERT_TRUE(gate.accept(43.02, -71.36));
  for (int i = 0; i < 100; ++i) {
    EXPECT_FALSE(gate.accept(43.02, -71.36));
  }
}

TEST(NavDecimator, NonFiniteInputRejectedWithoutPoisoningReference)
{
  const double nan = std::nan("");
  NavDecimator gate(10.0);
  // A leading NaN must not become the reference.
  EXPECT_FALSE(gate.accept(nan, 0.0));
  ASSERT_TRUE(gate.accept(0.0, 0.0));
  // A mid-stream NaN is dropped and the finite reference survives: the
  // sub-stride point after it is still rejected, the over-stride accepted.
  EXPECT_FALSE(gate.accept(0.0, nan));
  EXPECT_FALSE(gate.accept(nan, nan));
  EXPECT_FALSE(gate.accept(0.0, kUnderStrideDeg));
  EXPECT_TRUE(gate.accept(0.0, kOverStrideDeg));
}

TEST(NavDecimator, StrideIsConfigurable)
{
  NavDecimator fine(1.0);
  ASSERT_TRUE(fine.accept(0.0, 0.0));
  EXPECT_TRUE(fine.accept(0.0, kUnderStrideDeg));  // 5.56 m >= 1 m
}

}  // namespace
