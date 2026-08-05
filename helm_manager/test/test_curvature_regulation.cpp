// Copyright 2026 University of New Hampshire
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
//    * Neither the name of the University of New Hampshire nor the names of its
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

/// Pure-logic tests for curvature-preserving speed regulation (ADR-0012,
/// #292). No ROS spin — the algorithm is exercised directly through the
/// header the HelmManager node calls.

#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "../src/curvature_regulation.h"

using helm_manager::CurvatureRegulationConfig;
using helm_manager::applyCurvatureRegulation;
using helm_manager::curvatureCapabilityAt;
using helm_manager::validateCurvatureConfig;

namespace
{

// A non-monotonic envelope shaped like BizzyBoat's measured differential
// curve: weaker at rest, peak at mid-speed, degrading at high speed.
// margin 1.0 so expected values read directly off the table.
CurvatureRegulationConfig bizzyLikeConfig()
{
  CurvatureRegulationConfig config;
  config.enabled = true;
  config.curve = {0.0, 0.26, 0.5, 0.40, 1.3, 0.40, 1.8, 0.20, 2.5, 0.20};
  config.margin = 1.0;
  config.pivot_speed = 0.05;
  return config;
}

}  // namespace

TEST(CurvatureRegulation, DisabledByDefault)
{
  CurvatureRegulationConfig config;  // defaults: enabled=false, empty curve
  auto [v, w] = applyCurvatureRegulation(2.0, 5.0, config);
  EXPECT_DOUBLE_EQ(v, 2.0);
  EXPECT_DOUBLE_EQ(w, 5.0);
}

TEST(CurvatureRegulation, EmptyCurvePassthrough)
{
  CurvatureRegulationConfig config;
  config.enabled = true;  // enabled but no curve: nothing to regulate with
  auto [v, w] = applyCurvatureRegulation(2.0, 5.0, config);
  EXPECT_DOUBLE_EQ(v, 2.0);
  EXPECT_DOUBLE_EQ(w, 5.0);
}

TEST(CurvatureRegulation, WithinEnvelopePassthrough)
{
  auto config = bizzyLikeConfig();
  auto [v, w] = applyCurvatureRegulation(1.0, 0.3, config);  // λ(1.0) = 0.40
  EXPECT_DOUBLE_EQ(v, 1.0);
  EXPECT_DOUBLE_EQ(w, 0.3);
}

TEST(CurvatureRegulation, ZeroYawPassthrough)
{
  auto config = bizzyLikeConfig();
  auto [v, w] = applyCurvatureRegulation(3.0, 0.0, config);
  EXPECT_DOUBLE_EQ(v, 3.0);
  EXPECT_DOUBLE_EQ(w, 0.0);
}

TEST(CurvatureRegulation, ScalesBothPreservingCurvature)
{
  auto config = bizzyLikeConfig();
  // Commanded radius v/w = 2.0/0.5 = 4 m; λ(2.0) = 0.20 < 0.5 → regulate.
  const double v_cmd = 2.0, w_cmd = 0.5;
  auto [v, w] = applyCurvatureRegulation(v_cmd, w_cmd, config);
  ASSERT_GT(v, 0.0);
  ASSERT_GT(w, 0.0);
  EXPECT_LT(v, v_cmd);
  EXPECT_LT(w, w_cmd);
  // Curvature preserved exactly.
  EXPECT_NEAR(v / w, v_cmd / w_cmd, 1e-9);
  // Result is feasible under the envelope.
  EXPECT_LE(w, curvatureCapabilityAt(config, v) + 1e-9);
  // Same scale factor on both.
  EXPECT_NEAR(v / v_cmd, w / w_cmd, 1e-9);
}

TEST(CurvatureRegulation, NonMonotonicLookupFindsGlobalMax)
{
  auto config = bizzyLikeConfig();
  // k = w/v = 0.35/2.0 = 0.175 per unit. At the high band λ = 0.20:
  // f(2.0) = 0.20 - 0.35 < 0, infeasible at commanded speed. Walking down,
  // the crossing is where λ(x) = 0.175·x. In the 1.3→1.8 segment λ falls
  // 0.40→0.20 (slope -0.4): 0.40 - 0.4(x - 1.3) = 0.175x → x = 0.92/0.575
  // = 1.6 — well above what a monotonic assumption stopping at the first
  // low-band failure would give.
  auto [v, w] = applyCurvatureRegulation(2.0, 0.35, config);
  EXPECT_NEAR(v, 1.6, 1e-9);
  EXPECT_NEAR(w, 0.35 * v / 2.0, 1e-9);  // curvature preserved
  EXPECT_LE(w, curvatureCapabilityAt(config, v) + 1e-9);
}

TEST(CurvatureRegulation, FloorBehaviorNeverStops)
{
  auto config = bizzyLikeConfig();
  // Absurd yaw demand: infeasible at any speed above a crawl.
  auto [v, w] = applyCurvatureRegulation(1.5, 50.0, config);
  EXPECT_DOUBLE_EQ(v, config.pivot_speed);  // floor speed, not zero
  EXPECT_GT(w, 0.0);                        // maximum achievable yaw there
  EXPECT_NEAR(w, curvatureCapabilityAt(config, config.pivot_speed), 1e-9);
}

TEST(CurvatureRegulation, PivotCommandClampsYawAtRestCapability)
{
  auto config = bizzyLikeConfig();
  auto [v, w] = applyCurvatureRegulation(0.0, 1.0, config);
  EXPECT_DOUBLE_EQ(v, 0.0);            // pivot stays a pivot
  EXPECT_DOUBLE_EQ(w, 0.26);           // λ(0) with margin 1.0
  // Within rest capability: untouched.
  auto [v2, w2] = applyCurvatureRegulation(0.0, 0.2, config);
  EXPECT_DOUBLE_EQ(v2, 0.0);
  EXPECT_DOUBLE_EQ(w2, 0.2);
}

TEST(CurvatureRegulation, NeverSpeedsUp)
{
  auto config = bizzyLikeConfig();
  // At 0.2 m/s the interpolated λ ≈ 0.316; demand 0.35 rad/s. The peak band
  // (0.5-1.3 m/s, λ = 0.40) could satisfy it — but regulation must never
  // exceed the commanded speed.
  auto [v, w] = applyCurvatureRegulation(0.2, 0.35, config);
  EXPECT_LE(std::abs(v), 0.2 + 1e-12);
  EXPECT_LE(std::abs(w), 0.35 + 1e-12);
  EXPECT_LE(w, curvatureCapabilityAt(config, v) + 1e-9);
}

TEST(CurvatureRegulation, MarginApplied)
{
  auto config = bizzyLikeConfig();
  config.margin = 0.5;
  // λ(1.0) = 0.40 * 0.5 = 0.20; a command inside the unmargined envelope
  // but outside the margined one must be regulated.
  auto [v, w] = applyCurvatureRegulation(1.0, 0.3, config);
  EXPECT_LT(v, 1.0);
  EXPECT_NEAR(v / w, 1.0 / 0.3, 1e-9);
  EXPECT_LE(w, curvatureCapabilityAt(config, v) + 1e-9);
}

TEST(CurvatureRegulation, PreservesDirection)
{
  auto config = bizzyLikeConfig();
  for(const double sv : {1.0, -1.0}) {
    for(const double sw : {1.0, -1.0}) {
      auto [v, w] = applyCurvatureRegulation(sv * 2.0, sw * 0.5, config);
      EXPECT_GT(v * sv, 0.0) << "sv=" << sv << " sw=" << sw;
      EXPECT_GT(w * sw, 0.0) << "sv=" << sv << " sw=" << sw;
      EXPECT_NEAR(std::abs(v / w), 4.0, 1e-9);  // |radius| preserved
    }
  }
}

TEST(CurvatureRegulation, NonFiniteInputPassthrough)
{
  auto config = bizzyLikeConfig();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  auto [v, w] = applyCurvatureRegulation(nan, 0.5, config);
  EXPECT_TRUE(std::isnan(v));
  EXPECT_DOUBLE_EQ(w, 0.5);
}

TEST(CurvatureRegulationConfigTest, ValidConfigAccepted)
{
  auto config = bizzyLikeConfig();
  std::string error;
  EXPECT_TRUE(validateCurvatureConfig(config, error)) << error;
}

TEST(CurvatureRegulationConfigTest, DisabledAlwaysValid)
{
  CurvatureRegulationConfig config;  // empty curve but disabled
  std::string error;
  EXPECT_TRUE(validateCurvatureConfig(config, error));
}

TEST(CurvatureRegulationConfigTest, InvalidCurvesRejected)
{
  std::string error;

  auto odd = bizzyLikeConfig();
  odd.curve = {0.0, 0.26, 0.5};  // odd length
  EXPECT_FALSE(validateCurvatureConfig(odd, error));

  auto unsorted = bizzyLikeConfig();
  unsorted.curve = {0.5, 0.4, 0.5, 0.3};  // speeds not strictly ascending
  EXPECT_FALSE(validateCurvatureConfig(unsorted, error));

  auto negative = bizzyLikeConfig();
  negative.curve = {0.0, -0.26, 0.5, 0.4};  // negative capability
  EXPECT_FALSE(validateCurvatureConfig(negative, error));

  auto empty_enabled = bizzyLikeConfig();
  empty_enabled.curve.clear();  // enabled with no curve
  EXPECT_FALSE(validateCurvatureConfig(empty_enabled, error));

  auto bad_margin = bizzyLikeConfig();
  bad_margin.margin = 1.5;
  EXPECT_FALSE(validateCurvatureConfig(bad_margin, error));

  auto bad_pivot = bizzyLikeConfig();
  bad_pivot.pivot_speed = -0.1;
  EXPECT_FALSE(validateCurvatureConfig(bad_pivot, error));
}
