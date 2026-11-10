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
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

#include "marine_autonomy/gz4d_geo.h"
#include "marine_backscatter/seabed_geometry.hpp"

// [#304] Seabed geometry against synthetic surfaces. The test sampler maps
// lat/lon to local ENU with gz4d's LocalENU (geodetic -> ECEF -> ENU), NOT with
// the module's metresPerDegree, so the plane tests check the module's radius
// conversion against an independent one rather than against itself.

namespace
{

namespace sb = marine_backscatter::seabed;

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
const double kNaN = std::numeric_limits<double>::quiet_NaN();

// A surface h(e, n) about a reference point, with knobs for every sampler
// behaviour seabedNormal must handle. Predicates are indexed by the probe's ENU
// offset from the reference point, so "the east probe" is simply e > 0.
class TestSampler : public sb::SeabedHeightSampler
{
public:
  TestSampler(double ref_lat, double ref_lon)
  : enu_(gz4d::GeoPointLatLongDegrees(ref_lat, ref_lon, 0.0)) {}

  std::function<double(double, double)> surface = [](double, double) {return -10.0;};
  std::function<sb::SampleStatus(double, double)> status =
    [](double, double) {return sb::SampleStatus::kValid;};
  std::function<sb::SourceId(double, double)> source =
    [](double, double) {return sb::SourceId{};};
  std::function<void(double, double)> before = [](double, double) {};
  double span_north_m = 0.5;
  double span_east_m = 0.5;

  int calls = 0;
  double max_abs_lon = 0.0;
  double max_lon = -1000.0;

  sb::HeightSample heightAt(double lat_deg, double lon_deg) override
  {
    ++calls;
    max_abs_lon = std::max(max_abs_lon, std::abs(lon_deg));
    max_lon = std::max(max_lon, lon_deg);
    const auto local = enu_.toLocal(gz4d::GeoPointLatLongDegrees(lat_deg, lon_deg, 0.0));
    const double e = local[0];
    const double n = local[1];
    before(e, n);
    sb::HeightSample sample;
    sample.status = status(e, n);
    sample.height_m = surface(e, n);
    sample.span_north_m = span_north_m;
    sample.span_east_m = span_east_m;
    sample.source = source(e, n);
    return sample;
  }

private:
  gz4d::LocalENU enu_;
};

// Angle between two vectors, robust near zero (atan2 of |cross| and dot).
double angleBetween(const sb::Enu & a, const sb::Enu & b)
{
  const double cx = a.n * b.u - a.u * b.n;
  const double cy = a.u * b.e - a.e * b.u;
  const double cz = a.e * b.n - a.n * b.e;
  const double dot = a.e * b.e + a.n * b.n + a.u * b.u;
  return std::atan2(std::sqrt(cx * cx + cy * cy + cz * cz), dot);
}

sb::Enu unit(double e, double n, double u)
{
  const double norm = std::sqrt(e * e + n * n + u * u);
  return sb::Enu{e / norm, n / norm, u / norm};
}

// The upslope gradient of a plane tilted by @p slope_deg rising toward
// @p upslope_az_deg (clockwise from north).
struct Gradient
{
  double e, n;
};

Gradient planeGradient(double slope_deg, double upslope_az_deg)
{
  const double g = std::tan(slope_deg * kDeg);
  return Gradient{g * std::sin(upslope_az_deg * kDeg), g * std::cos(upslope_az_deg * kDeg)};
}

}  // namespace

// ---------------------------------------------------------------------------
// metresPerDegree
// ---------------------------------------------------------------------------

// Against the closed forms at the equator and pole (a·π/180 and the WGS84 M at
// 0 and 90 deg) and the published series approximations at 45 deg
// (111132.954 − 559.822 cos2φ + 1.175 cos4φ; 111412.84 cosφ − 93.5 cos3φ + 0.118 cos5φ).
TEST(SeabedGeometry, MetresPerDegreeMatchesWgs84)
{
  const auto eq = sb::metresPerDegree(0.0);
  EXPECT_NEAR(eq.east, 6378137.0 * kDeg, 1e-6);
  EXPECT_NEAR(eq.north, 110574.2758, 1e-3);

  const auto mid = sb::metresPerDegree(45.0);
  EXPECT_NEAR(mid.north,
    111132.954 - 559.822 * std::cos(90.0 * kDeg) + 1.175 * std::cos(180.0 * kDeg),
    0.1);
  EXPECT_NEAR(
    mid.east,
    111412.84 * std::cos(45.0 * kDeg) - 93.5 * std::cos(135.0 * kDeg) +
    0.118 * std::cos(225.0 * kDeg), 0.5);

  const auto pole = sb::metresPerDegree(90.0);
  EXPECT_NEAR(pole.north, 111693.9796, 1e-3);
  EXPECT_EQ(pole.east, 0.0);

  // Hemispheres are symmetric.
  const auto south = sb::metresPerDegree(-45.0);
  EXPECT_DOUBLE_EQ(south.north, mid.north);
  EXPECT_DOUBLE_EQ(south.east, mid.east);
}

// ---------------------------------------------------------------------------
// seabedNormal: surfaces
// ---------------------------------------------------------------------------

// A plane is recovered exactly (to 1e-6 rad) at every slope, upslope azimuth,
// step scale and latitude, including 60N and the southern hemisphere where a
// missing cos(lat) or a sign slip would show.
TEST(SeabedGeometry, PlanarSlopesAreRecovered)
{
  const double lats[] = {43.07, 60.0, -33.9};
  const double slopes[] = {0.0, 1.0, 5.0, 15.0, 30.0, 60.0};
  const double azimuths[] = {0.0, 45.0, 90.0, 180.0, 270.0};
  const double step_cells[] = {1.0, 3.0, 10.0};
  for (const double lat : lats) {
    for (const double slope : slopes) {
      for (const double az : azimuths) {
        for (const double cells : step_cells) {
          TestSampler sampler(lat, -71.42);
          const Gradient g = planeGradient(slope, az);
          sampler.surface = [g](double e, double n) {return -25.0 + g.e * e + g.n * n;};
          sb::NormalOptions options;
          options.auto_step_cells = cells;
          const sb::Normal result = sb::seabedNormal(sampler, lat, -71.42, options);
          ASSERT_TRUE(result.from_surface) << sb::toString(result.reason);
          EXPECT_EQ(result.reason, sb::FallbackReason::kNone);
          EXPECT_NEAR(result.step_used_m, cells * 0.5, 1e-12);
          const sb::Enu expected = unit(-g.e, -g.n, 1.0);
          EXPECT_LT(angleBetween(result.normal, expected), 1e-6)
            << "lat " << lat << " slope " << slope << " az " << az << " cells " << cells;
          EXPECT_NEAR(
            std::sqrt(
              result.normal.e * result.normal.e + result.normal.n * result.normal.n +
              result.normal.u * result.normal.u), 1.0, 1e-12);
          EXPECT_GT(result.normal.u, 0.0);   // up-pointing
        }
      }
    }
  }
}

TEST(SeabedGeometry, FlatBottomGivesVerticalNormalFromTheSurface)
{
  TestSampler sampler(43.07, -71.42);
  const sb::Normal result = sb::seabedNormal(sampler, 43.07, -71.42);
  EXPECT_TRUE(result.from_surface);
  EXPECT_EQ(result.reason, sb::FallbackReason::kNone);
  EXPECT_NEAR(result.normal.e, 0.0, 1e-12);
  EXPECT_NEAR(result.normal.n, 0.0, 1e-12);
  EXPECT_NEAR(result.normal.u, 1.0, 1e-12);
  EXPECT_EQ(sampler.calls, 5);   // centre + four probes
}

// On a non-planar bottom the step is a real scale choice: the central difference
// of h = c·(e + x0)^3 is c·(3·x0² + s²), so the recovered gradient moves with s.
TEST(SeabedGeometry, StepScaleMattersOnACurvedBottom)
{
  constexpr double c = 0.01;
  constexpr double x0 = 10.0;
  double previous = 0.0;
  for (const double step : {1.0, 2.0, 5.0}) {
    TestSampler sampler(43.07, -71.42);
    sampler.surface = [](double e, double) {return c * std::pow(e + x0, 3);};
    sb::NormalOptions options;
    options.step_m = step;
    const sb::Normal result = sb::seabedNormal(sampler, 43.07, -71.42, options);
    ASSERT_TRUE(result.from_surface);
    const double dh_de = -result.normal.e / result.normal.u;
    EXPECT_NEAR(dh_de, c * (3.0 * x0 * x0 + step * step), 1e-6) << "step " << step;
    EXPECT_GT(dh_de, previous);
    previous = dh_de;
  }
}

// ---------------------------------------------------------------------------
// seabedNormal: fallbacks
// ---------------------------------------------------------------------------

// A missing sample at ANY one of the four probes gives the flat fallback.
TEST(SeabedGeometry, MissingProbeFallsBackFlat)
{
  const std::function<bool(double, double)> at_probe[] = {
    [](double e, double) {return e > 0.25;},     // east
    [](double e, double) {return e < -0.25;},    // west
    [](double, double n) {return n > 0.25;},     // north
    [](double, double n) {return n < -0.25;},    // south
  };
  for (const auto & hole : at_probe) {
    TestSampler sampler(43.07, -71.42);
    sampler.status = [hole](double e, double n) {
        return hole(e, n) ? sb::SampleStatus::kNoData : sb::SampleStatus::kValid;
      };
    const sb::Normal result = sb::seabedNormal(sampler, 43.07, -71.42);
    EXPECT_FALSE(result.from_surface);
    EXPECT_EQ(result.reason, sb::FallbackReason::kMissingSample);
    EXPECT_EQ(result.normal.u, 1.0);
    EXPECT_EQ(result.normal.e, 0.0);
    EXPECT_EQ(result.normal.n, 0.0);
    EXPECT_EQ(sampler.calls, 5);
  }
}

// A probe on a partial interpolation stencil (survey edge) has its own reason.
TEST(SeabedGeometry, IncompleteStencilAtAProbeFallsBackFlat)
{
  TestSampler sampler(43.07, -71.42);
  sampler.status = [](double, double n) {
      return n > 0.25 ? sb::SampleStatus::kStencilIncomplete : sb::SampleStatus::kValid;
    };
  const sb::Normal result = sb::seabedNormal(sampler, 43.07, -71.42);
  EXPECT_FALSE(result.from_surface);
  EXPECT_EQ(result.reason, sb::FallbackReason::kStencilIncomplete);
}

// A probe answered by a different source (layer, level or tile) is a seam: a
// vertical offset between two surfaces would otherwise read as slope.
TEST(SeabedGeometry, SourceChangeAcrossTheStencilIsALayerSeam)
{
  const sb::SourceId other_layer{1, 0, 0};
  const sb::SourceId other_level{0, 13, 0};
  const sb::SourceId other_tile{0, 0, 7};
  for (const auto & other : {other_layer, other_level, other_tile}) {
    TestSampler sampler(43.07, -71.42);
    // 1 m offset east of the centre: about 18 deg of invented slope if unguarded.
    sampler.surface = [](double e, double) {return e > 0.25 ? -9.0 : -10.0;};
    sampler.source = [other](double e, double) {return e > 0.25 ? other : sb::SourceId{};};
    const sb::Normal result = sb::seabedNormal(sampler, 43.07, -71.42);
    EXPECT_FALSE(result.from_surface);
    EXPECT_EQ(result.reason, sb::FallbackReason::kLayerSeam);
  }
}

// The reason does not depend on probe order: missing beats incomplete beats seam.
TEST(SeabedGeometry, FallbackReasonPriority)
{
  {
    TestSampler sampler(43.07, -71.42);
    sampler.status = [](double e, double n) {
        if (n > 0.25) {return sb::SampleStatus::kStencilIncomplete;}
        if (e < -0.25) {return sb::SampleStatus::kNoData;}
        return sb::SampleStatus::kValid;
      };
    EXPECT_EQ(
      sb::seabedNormal(sampler, 43.07, -71.42).reason, sb::FallbackReason::kMissingSample);
  }
  {
    TestSampler sampler(43.07, -71.42);
    sampler.status = [](double e, double) {
        return e < -0.25 ? sb::SampleStatus::kStencilIncomplete : sb::SampleStatus::kValid;
      };
    sampler.source = [](double e, double) {
        return e > 0.25 ? sb::SourceId{2, 0, 0} : sb::SourceId{};
      };
    EXPECT_EQ(
      sb::seabedNormal(sampler, 43.07, -71.42).reason, sb::FallbackReason::kStencilIncomplete);
  }
}

// The centre is checked before any probe is spent.
TEST(SeabedGeometry, BadCentreReturnsBeforeProbing)
{
  {
    TestSampler sampler(43.07, -71.42);
    sampler.status = [](double, double) {return sb::SampleStatus::kNoData;};
    const sb::Normal result = sb::seabedNormal(sampler, 43.07, -71.42);
    EXPECT_EQ(result.reason, sb::FallbackReason::kMissingSample);
    EXPECT_EQ(sampler.calls, 1);
  }
  {
    TestSampler sampler(43.07, -71.42);
    sampler.status = [](double, double) {return sb::SampleStatus::kStencilIncomplete;};
    const sb::Normal result = sb::seabedNormal(sampler, 43.07, -71.42);
    EXPECT_EQ(result.reason, sb::FallbackReason::kStencilIncomplete);
    EXPECT_EQ(sampler.calls, 1);
  }
}

// A sampler that says "valid" but hands back a non-finite height is missing,
// never a NaN normal.
TEST(SeabedGeometry, NonFiniteValidHeightIsMissing)
{
  TestSampler sampler(43.07, -71.42);
  sampler.surface = [](double e, double) {return e > 0.25 ? kNaN : -10.0;};
  const sb::Normal result = sb::seabedNormal(sampler, 43.07, -71.42);
  EXPECT_FALSE(result.from_surface);
  EXPECT_EQ(result.reason, sb::FallbackReason::kMissingSample);
}

// A hole strictly between the centre and the probes is never sampled, so it
// cannot cause a spurious fallback.
TEST(SeabedGeometry, HoleInsideTheStepIsNotHit)
{
  TestSampler sampler(43.07, -71.42);   // auto step = 3 x 0.5 m = 1.5 m
  sampler.status = [](double e, double) {
      return (e > 0.3 && e < 1.2) ? sb::SampleStatus::kNoData : sb::SampleStatus::kValid;
    };
  const sb::Normal result = sb::seabedNormal(sampler, 43.07, -71.42);
  EXPECT_TRUE(result.from_surface) << sb::toString(result.reason);
}

// A fault in the sampler (BathyDem: a corrupt tile) propagates; it is never
// turned into a flat fallback.
TEST(SeabedGeometry, SamplerExceptionPropagates)
{
  TestSampler sampler(43.07, -71.42);
  sampler.before = [](double e, double) {
      if (e > 0.25) {
        throw std::runtime_error("corrupt tile");
      }
    };
  EXPECT_THROW(sb::seabedNormal(sampler, 43.07, -71.42), std::runtime_error);
}

// ---------------------------------------------------------------------------
// seabedNormal: step and position rules
// ---------------------------------------------------------------------------

TEST(SeabedGeometry, AutoStepUsesTheLargerSpan)
{
  TestSampler sampler(43.07, -71.42);
  sampler.span_north_m = 0.6;
  sampler.span_east_m = 0.44;
  const sb::Normal result = sb::seabedNormal(sampler, 43.07, -71.42);
  EXPECT_TRUE(result.from_surface);
  EXPECT_NEAR(result.step_used_m, 1.8, 1e-12);
  EXPECT_FALSE(result.step_clamped);
}

// The step is floored at one native cell (the larger span), auto or explicit.
TEST(SeabedGeometry, StepIsFlooredAtOneCellAndFlagged)
{
  {
    TestSampler sampler(43.07, -71.42);
    sb::NormalOptions options;
    options.auto_step_cells = 0.5;
    const sb::Normal result = sb::seabedNormal(sampler, 43.07, -71.42, options);
    EXPECT_TRUE(result.from_surface);
    EXPECT_TRUE(result.step_clamped);
    EXPECT_NEAR(result.step_used_m, 0.5, 1e-12);
  }
  {
    TestSampler sampler(43.07, -71.42);
    sampler.span_north_m = 0.6;
    sampler.span_east_m = 0.44;
    sb::NormalOptions options;
    options.step_m = 0.1;
    const sb::Normal result = sb::seabedNormal(sampler, 43.07, -71.42, options);
    EXPECT_TRUE(result.step_clamped);
    EXPECT_NEAR(result.step_used_m, 0.6, 1e-12);
  }
  {
    TestSampler sampler(43.07, -71.42);
    sb::NormalOptions options;
    options.step_m = 2.0;
    const sb::Normal result = sb::seabedNormal(sampler, 43.07, -71.42, options);
    EXPECT_FALSE(result.step_clamped);
    EXPECT_NEAR(result.step_used_m, 2.0, 1e-12);
  }
}

// Without a cell span, auto step is impossible; an explicit step still works,
// unclamped.
TEST(SeabedGeometry, UnknownSpans)
{
  {
    TestSampler sampler(43.07, -71.42);
    sampler.span_north_m = kNaN;
    sampler.span_east_m = kNaN;
    const sb::Normal result = sb::seabedNormal(sampler, 43.07, -71.42);
    EXPECT_FALSE(result.from_surface);
    EXPECT_EQ(result.reason, sb::FallbackReason::kNoCellSize);
    EXPECT_EQ(sampler.calls, 1);
  }
  {
    TestSampler sampler(43.07, -71.42);
    sampler.span_north_m = kNaN;
    sampler.span_east_m = 0.5;
    sb::NormalOptions options;
    options.step_m = 0.2;
    const sb::Normal result = sb::seabedNormal(sampler, 43.07, -71.42, options);
    EXPECT_TRUE(result.from_surface);
    EXPECT_FALSE(result.step_clamped);
    EXPECT_NEAR(result.step_used_m, 0.2, 1e-12);
  }
}

TEST(SeabedGeometry, BadStepOptions)
{
  const auto reason = [](double step_m, double cells) {
      TestSampler sampler(43.07, -71.42);
      sb::NormalOptions options;
      options.step_m = step_m;
      options.auto_step_cells = cells;
      return sb::seabedNormal(sampler, 43.07, -71.42, options).reason;
    };
  EXPECT_EQ(reason(-1.0, 3.0), sb::FallbackReason::kBadStep);
  EXPECT_EQ(reason(kNaN, 3.0), sb::FallbackReason::kBadStep);
  EXPECT_EQ(reason(std::numeric_limits<double>::infinity(), 3.0), sb::FallbackReason::kBadStep);
  EXPECT_EQ(reason(0.0, 0.0), sb::FallbackReason::kBadStep);
  EXPECT_EQ(reason(0.0, -2.0), sb::FallbackReason::kBadStep);
  EXPECT_EQ(reason(0.0, kNaN), sb::FallbackReason::kBadStep);
  // auto_step_cells is irrelevant when an explicit step is given.
  EXPECT_EQ(reason(1.0, kNaN), sb::FallbackReason::kNone);
}

TEST(SeabedGeometry, BadPositions)
{
  const auto reason = [](double lat, double lon) {
      TestSampler sampler(43.07, -71.42);
      return sb::seabedNormal(sampler, lat, lon).reason;
    };
  EXPECT_EQ(reason(kNaN, -71.42), sb::FallbackReason::kBadPosition);
  EXPECT_EQ(reason(43.07, kNaN), sb::FallbackReason::kBadPosition);
  EXPECT_EQ(reason(90.5, -71.42), sb::FallbackReason::kBadPosition);
  EXPECT_EQ(reason(-91.0, -71.42), sb::FallbackReason::kBadPosition);
  EXPECT_EQ(reason(90.0, 0.0), sb::FallbackReason::kBadPosition);          // at the pole
  EXPECT_EQ(reason(89.99999, 0.0), sb::FallbackReason::kBadPosition);      // stencil past it
}

// Probe longitudes wrap into [-180, 180) and the plane is still recovered across
// the antimeridian.
TEST(SeabedGeometry, AntimeridianWraps)
{
  const double lat = -16.5;
  const double lon = 179.99999;
  TestSampler sampler(lat, lon);
  const Gradient g = planeGradient(10.0, 90.0);
  sampler.surface = [g](double e, double n) {return -40.0 + g.e * e + g.n * n;};
  const sb::Normal result = sb::seabedNormal(sampler, lat, lon);
  ASSERT_TRUE(result.from_surface);
  EXPECT_LT(angleBetween(result.normal, unit(-g.e, -g.n, 1.0)), 1e-6);
  EXPECT_LT(sampler.max_abs_lon, 180.0);
}

// A longitude a hair west of -180 must wrap to just under +180, never to +180.0
// itself (fmod(-1e-14) + 360 rounds to exactly 360).
TEST(SeabedGeometry, LongitudeJustPastTheAntimeridianStaysHalfOpen)
{
  TestSampler sampler(10.0, -180.0);
  sampler.surface = [](double, double) {return -40.0;};
  const sb::Normal result = sb::seabedNormal(sampler, 10.0, -180.0 - 1e-14);
  EXPECT_TRUE(result.from_surface);
  EXPECT_LT(sampler.max_lon, 180.0);
}

// Finite heights whose differences overflow are an unusable sample, not a
// zero or NaN "surface" normal.
TEST(SeabedGeometry, OverflowingHeightsFallBackInsteadOfBadNormal)
{
  TestSampler sampler(43.0, -70.0);
  sampler.surface = [](double e, double) {return e > 0.0 ? 1e200 : (e < 0.0 ? -1e200 : 0.0);};
  const sb::Normal result = sb::seabedNormal(sampler, 43.0, -70.0);
  EXPECT_FALSE(result.from_surface);
  EXPECT_EQ(result.reason, sb::FallbackReason::kMissingSample);
  EXPECT_DOUBLE_EQ(result.normal.u, 1.0);
}

TEST(SeabedGeometry, ReasonNamesAreDistinct)
{
  const sb::FallbackReason all[] = {
    sb::FallbackReason::kNone, sb::FallbackReason::kMissingSample,
    sb::FallbackReason::kStencilIncomplete, sb::FallbackReason::kLayerSeam,
    sb::FallbackReason::kNoCellSize, sb::FallbackReason::kBadStep,
    sb::FallbackReason::kBadPosition};
  std::set<std::string> names;
  for (const auto reason : all) {
    names.insert(sb::toString(reason));
  }
  EXPECT_EQ(names.size(), 7u);
  EXPECT_STREQ(sb::toString(sb::FallbackReason::kLayerSeam), "layer_seam");
}

// ---------------------------------------------------------------------------
// incidence / grazing
// ---------------------------------------------------------------------------

TEST(SeabedGeometry, IncidenceOnAFlatBottom)
{
  const sb::Enu up{0.0, 0.0, 1.0};
  const auto nadir = sb::incidenceAngle(sb::Enu{0.0, 0.0, -1.0}, up);
  ASSERT_TRUE(nadir.valid);
  EXPECT_NEAR(nadir.rad, 0.0, 1e-7);

  const auto oblique = sb::incidenceAngle(sb::beamFromAzimuthDepression(kPi / 2, kPi / 4), up);
  ASSERT_TRUE(oblique.valid);
  EXPECT_NEAR(oblique.rad, kPi / 4, 1e-12);

  const auto grazing = sb::grazingAngle(sb::beamFromAzimuthDepression(kPi / 2, kPi / 4), up);
  ASSERT_TRUE(grazing.valid);
  EXPECT_NEAR(grazing.rad, kPi / 4, 1e-12);

  // A beam parallel to the surface: incidence 90 deg, grazing 0.
  const auto parallel = sb::incidenceAngle(sb::Enu{1.0, 0.0, 0.0}, up);
  ASSERT_TRUE(parallel.valid);
  EXPECT_NEAR(parallel.rad, kPi / 2, 1e-12);
  EXPECT_NEAR(sb::grazingAngle(sb::Enu{1.0, 0.0, 0.0}, up).rad, 0.0, 1e-12);
}

// The same 45 deg beam meets a 10 deg slope at 45 - 10 looking up-slope and
// 45 + 10 looking down-slope, with the normal taken from seabedNormal.
TEST(SeabedGeometry, IncidenceAgainstASlopedNormal)
{
  TestSampler sampler(43.07, -71.42);
  const Gradient g = planeGradient(10.0, 90.0);   // rises toward the east
  sampler.surface = [g](double e, double n) {return -20.0 + g.e * e + g.n * n;};
  const sb::Normal normal = sb::seabedNormal(sampler, 43.07, -71.42);
  ASSERT_TRUE(normal.from_surface);

  const auto upslope = sb::incidenceAngle(
    sb::beamFromAzimuthDepression(90.0 * kDeg, 45.0 * kDeg), normal.normal);
  const auto downslope = sb::incidenceAngle(
    sb::beamFromAzimuthDepression(270.0 * kDeg, 45.0 * kDeg), normal.normal);
  ASSERT_TRUE(upslope.valid);
  ASSERT_TRUE(downslope.valid);
  EXPECT_NEAR(upslope.rad, 35.0 * kDeg, 1e-6);
  EXPECT_NEAR(downslope.rad, 55.0 * kDeg, 1e-6);

  const auto grazing = sb::grazingAngle(
    sb::beamFromAzimuthDepression(90.0 * kDeg, 45.0 * kDeg), normal.normal);
  EXPECT_NEAR(grazing.rad, 55.0 * kDeg, 1e-6);
}

// A beam hitting the back of a steep face returns the raw angle (> 90 deg) and a
// negative grazing angle, unclamped.
TEST(SeabedGeometry, BackFacingIncidenceIsReturnedRaw)
{
  const sb::Enu normal = unit(-std::sin(60.0 * kDeg), 0.0, std::cos(60.0 * kDeg));
  const sb::Enu beam = sb::beamFromAzimuthDepression(270.0 * kDeg, 10.0 * kDeg);
  const auto incidence = sb::incidenceAngle(beam, normal);
  ASSERT_TRUE(incidence.valid);
  EXPECT_NEAR(incidence.rad, 140.0 * kDeg, 1e-9);
  const auto grazing = sb::grazingAngle(beam, normal);
  ASSERT_TRUE(grazing.valid);
  EXPECT_NEAR(grazing.rad, -50.0 * kDeg, 1e-9);
}

// Bad vectors give a flagged result, never an exception or a silent renormalise.
TEST(SeabedGeometry, BadVectorsAreFlaggedNotThrown)
{
  const sb::Enu up{0.0, 0.0, 1.0};
  // Within tolerance (|norm - 1| <= 1e-6): accepted.
  EXPECT_TRUE(sb::incidenceAngle(sb::Enu{0.0, 0.0, -(1.0 + 5e-7)}, up).valid);
  // Beyond tolerance: flagged.
  const auto long_beam = sb::incidenceAngle(sb::Enu{0.0, 0.0, -(1.0 + 2e-6)}, up);
  EXPECT_FALSE(long_beam.valid);
  EXPECT_TRUE(std::isnan(long_beam.rad));
  EXPECT_FALSE(sb::incidenceAngle(sb::Enu{0.0, 0.0, -0.5}, up).valid);
  EXPECT_FALSE(sb::incidenceAngle(sb::Enu{0.0, 0.0, 0.0}, up).valid);
  EXPECT_FALSE(sb::incidenceAngle(sb::Enu{kNaN, 0.0, -1.0}, up).valid);
  EXPECT_FALSE(
    sb::incidenceAngle(sb::Enu{0.0, std::numeric_limits<double>::infinity(), -1.0}, up).valid);
  // A bad normal is flagged the same way.
  EXPECT_FALSE(sb::incidenceAngle(sb::Enu{0.0, 0.0, -1.0}, sb::Enu{0.0, 0.0, 2.0}).valid);
  EXPECT_FALSE(sb::grazingAngle(sb::Enu{0.0, 0.0, -1.0}, sb::Enu{kNaN, 0.0, 1.0}).valid);
  EXPECT_NO_THROW(sb::incidenceAngle(sb::Enu{kNaN, kNaN, kNaN}, sb::Enu{kNaN, kNaN, kNaN}));
}

TEST(SeabedGeometry, BeamHelpers)
{
  const sb::Enu north_level = sb::beamFromAzimuthDepression(0.0, 0.0);
  EXPECT_NEAR(north_level.e, 0.0, 1e-15);
  EXPECT_NEAR(north_level.n, 1.0, 1e-15);
  EXPECT_NEAR(north_level.u, 0.0, 1e-15);

  const sb::Enu b = sb::beamFromAzimuthDepression(30.0 * kDeg, 20.0 * kDeg);
  EXPECT_NEAR(std::sqrt(b.e * b.e + b.n * b.n + b.u * b.u), 1.0, 1e-15);
  EXPECT_LT(b.u, 0.0);    // pointing down
  EXPECT_GT(b.e, 0.0);    // east of north
  EXPECT_NEAR(std::atan2(b.e, b.n), 30.0 * kDeg, 1e-12);

  const sb::Enu converted = sb::enuFromNed(1.0, 2.0, 3.0);
  EXPECT_EQ(converted.e, 2.0);
  EXPECT_EQ(converted.n, 1.0);
  EXPECT_EQ(converted.u, -3.0);
}
