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

#include <array>
#include <cmath>

#include "geodesy/ecef.h"
#include "geographic_msgs/msg/geo_point.hpp"
#include "marine_autonomy/gggs.h"
#include "marine_sidescan_mosaic/projection.hpp"

namespace msm = marine_sidescan_mosaic;

namespace
{
using Mat3 = std::array<double, 9>;

Mat3 matmul(const Mat3 & a, const Mat3 & b)
{
  Mat3 r{};
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      double s = 0.0;
      for (int k = 0; k < 3; ++k) {
        s += a[i * 3 + k] * b[k * 3 + j];
      }
      r[i * 3 + j] = s;
    }
  }
  return r;
}

Mat3 transpose(const Mat3 & m)
{
  return Mat3{m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]};
}

// Quaternion (x,y,z,w) from a proper rotation matrix (row-major), p_parent = R p_child.
std::array<double, 4> quatFromMatrix(const Mat3 & m)
{
  const double t = m[0] + m[4] + m[8];
  double x, y, z, w;
  if (t > 0.0) {
    double s = 0.5 / std::sqrt(t + 1.0);
    w = 0.25 / s;
    x = (m[7] - m[5]) * s;
    y = (m[2] - m[6]) * s;
    z = (m[3] - m[1]) * s;
  } else if (m[0] > m[4] && m[0] > m[8]) {
    double s = 2.0 * std::sqrt(1.0 + m[0] - m[4] - m[8]);
    w = (m[7] - m[5]) / s;
    x = 0.25 * s;
    y = (m[1] + m[3]) / s;
    z = (m[2] + m[6]) / s;
  } else if (m[4] > m[8]) {
    double s = 2.0 * std::sqrt(1.0 + m[4] - m[0] - m[8]);
    w = (m[2] - m[6]) / s;
    x = (m[1] + m[3]) / s;
    y = 0.25 * s;
    z = (m[5] + m[7]) / s;
  } else {
    double s = 2.0 * std::sqrt(1.0 + m[8] - m[0] - m[4]);
    w = (m[3] - m[1]) / s;
    x = (m[2] + m[6]) / s;
    y = (m[5] + m[7]) / s;
    z = 0.25 * s;
  }
  return {x, y, z, w};
}

// Build the body->ECEF quaternion for a vessel at (lat,lon) with the given level
// heading (radians, CW from north), so ecefPoseToGeoHeading should recover it.
std::array<double, 4> bodyEcefQuatForHeading(double lat_deg, double lon_deg, double heading)
{
  const double lat = lat_deg * M_PI / 180.0;
  const double lon = lon_deg * M_PI / 180.0;
  const double sla = std::sin(lat), cla = std::cos(lat);
  const double slo = std::sin(lon), clo = std::cos(lon);
  const Mat3 r_ecef_enu = {
    -slo, clo, 0.0, -sla * clo, -sla * slo, cla, cla * clo, cla * slo, sla};
  const Mat3 r_enu_ned = {0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, -1.0};
  const Mat3 r_ecef_ned = matmul(r_enu_ned, r_ecef_enu);
  // body->NED: yaw `heading` about Down (body x at azimuth heading).
  const double ch = std::cos(heading), sh = std::sin(heading);
  const Mat3 r_body_ned = {ch, -sh, 0.0, sh, ch, 0.0, 0.0, 0.0, 1.0};
  const Mat3 r_body_ecef = matmul(transpose(r_ecef_ned), r_body_ned);
  return quatFromMatrix(r_body_ecef);
}

// body->ECEF quaternion for an arbitrary body->NED rotation at (lat,lon).
std::array<double, 4> bodyEcefQuatForBodyNed(
  double lat_deg, double lon_deg, const Mat3 & r_body_ned)
{
  const double lat = lat_deg * M_PI / 180.0;
  const double lon = lon_deg * M_PI / 180.0;
  const double sla = std::sin(lat), cla = std::cos(lat);
  const double slo = std::sin(lon), clo = std::cos(lon);
  const Mat3 r_ecef_enu = {
    -slo, clo, 0.0, -sla * clo, -sla * slo, cla, cla * clo, cla * slo, sla};
  const Mat3 r_enu_ned = {0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, -1.0};
  const Mat3 r_ecef_ned = matmul(r_enu_ned, r_ecef_enu);
  return quatFromMatrix(matmul(transpose(r_ecef_ned), r_body_ned));
}

geographic_msgs::msg::GeoPoint geoPoint(double lat, double lon, double alt = 0.0)
{
  geographic_msgs::msg::GeoPoint p;
  p.latitude = lat;
  p.longitude = lon;
  p.altitude = alt;
  return p;
}
}  // namespace

TEST(Projection, SlantGroundAzimuth)
{
  // sample_rate 10 kHz, sound speed 1500 m/s, near-field gate 100.
  EXPECT_DOUBLE_EQ(msm::slantRange(0, 100, 1500.0, 10000.0), 100.0 * 1500.0 / 20000.0);
  EXPECT_DOUBLE_EQ(msm::slantRange(900, 100, 1500.0, 10000.0), 1000.0 * 1500.0 / 20000.0);
  EXPECT_NEAR(msm::groundRange(15.0, 10.0), std::sqrt(125.0), 1e-9);
  EXPECT_DOUBLE_EQ(msm::groundRange(5.0, 10.0), 0.0);   // slant within altitude
  EXPECT_DOUBLE_EQ(msm::acrossTrackAzimuth(0.0, msm::Side::Starboard), M_PI / 2.0);
  EXPECT_DOUBLE_EQ(msm::acrossTrackAzimuth(0.0, msm::Side::Port), -M_PI / 2.0);
}

TEST(Projection, EcefPositionRoundTrip)
{
  geodesy::ECEFPoint e;
  geodesy::fromMsg(geoPoint(43.07, -71.42, 5.0), e);
  const auto gh = msm::ecefPoseToGeoHeading(e.x, e.y, e.z, 0.0, 0.0, 0.0, 1.0);
  EXPECT_NEAR(gh.latitude_deg, 43.07, 1e-6);
  EXPECT_NEAR(gh.longitude_deg, -71.42, 1e-6);
  EXPECT_NEAR(gh.altitude_m, 5.0, 1e-3);
}

TEST(Projection, HeadingRecoveredFromPose)
{
  geodesy::ECEFPoint e;
  geodesy::fromMsg(geoPoint(43.07, -71.42, 0.0), e);
  for (double h_deg : {0.0, 90.0, 200.0, 315.0}) {
    const double h = h_deg * M_PI / 180.0;
    const auto q = bodyEcefQuatForHeading(43.07, -71.42, h);
    const auto gh = msm::ecefPoseToGeoHeading(e.x, e.y, e.z, q[0], q[1], q[2], q[3]);
    // Compare on the circle (wrap-safe).
    EXPECT_NEAR(std::cos(gh.heading_rad), std::cos(h), 1e-6) << "heading " << h_deg;
    EXPECT_NEAR(std::sin(gh.heading_rad), std::sin(h), 1e-6) << "heading " << h_deg;
  }
}

TEST(Projection, BeamAzimuthDepressionFromFullAttitude)
{
  geodesy::ECEFPoint e;
  geodesy::fromMsg(geoPoint(43.07, -71.42, 0.0), e);

  // Frame +Z (range axis) points due East, horizontal: column 2 of body->NED is
  // (N,E,D)=(0,1,0).  X=North, Y=Up, Z=East.
  const Mat3 east_horizontal = {1, 0, 0, 0, 0, 1, 0, -1, 0};
  const auto q = bodyEcefQuatForBodyNed(43.07, -71.42, east_horizontal);
  const auto gb = msm::ecefPoseToGeoBeam(e.x, e.y, e.z, q[0], q[1], q[2], q[3]);
  EXPECT_NEAR(gb.latitude_deg, 43.07, 1e-6);
  EXPECT_NEAR(std::cos(gb.azimuth_rad), 0.0, 1e-6);     // azimuth = +90 deg (east)
  EXPECT_NEAR(std::sin(gb.azimuth_rad), 1.0, 1e-6);
  EXPECT_NEAR(gb.depression_rad, 0.0, 1e-6);

  // Frame +Z points East and 30 deg down: column 2 = (0, cos30, sin30).
  const double c = std::cos(M_PI / 6.0), s = std::sin(M_PI / 6.0);
  const Mat3 east_down = {1, 0, 0, 0, s, c, 0, -c, s};   // X=N, Y=(0,s,-c), Z=(0,c,s)
  const auto q2 = bodyEcefQuatForBodyNed(43.07, -71.42, east_down);
  const auto gb2 = msm::ecefPoseToGeoBeam(e.x, e.y, e.z, q2[0], q2[1], q2[2], q2[3]);
  EXPECT_NEAR(std::sin(gb2.azimuth_rad), 1.0, 1e-6);     // still east
  EXPECT_NEAR(gb2.depression_rad, M_PI / 6.0, 1e-6);     // 30 deg depression
}

TEST(Projection, ProjectsEastward)
{
  const gggs::Level level(13);
  const auto origin = geoPoint(43.07, -71.42, 0.0);
  const auto cell = msm::projectSample(origin, M_PI / 2.0, 20.0, level);   // 20 m due east
  const auto pos = cell.position();
  EXPECT_GT(pos.longitude, origin.longitude);            // moved east
  EXPECT_NEAR(pos.latitude, origin.latitude, 1e-4);      // ~same latitude
}

TEST(Projection, SampleCrossesGridBoundary)
{
  // Place the origin just west of a grid's eastern edge, then project east past
  // it: the sample must land in the NEXT grid, not be clamped to this grid's edge.
  const gggs::Level level(13);
  const gggs::GridIndex grid = level.gridIndex(43.07, -71.42);
  const double east_edge = grid.westLongitude() + grid.longitudinalSpan();
  // ~0.5 m west of the edge (longitude degrees at ~43 N).
  const double half_m_deg = 0.5 / (111320.0 * std::cos(43.07 * M_PI / 180.0));
  const auto origin = geoPoint(43.07, east_edge - half_m_deg, 0.0);

  const gggs::GridIndex origin_grid = level.gridIndex(origin.latitude, origin.longitude);
  const auto cell = msm::projectSample(origin, M_PI / 2.0, 5.0, level);    // 5 m east
  EXPECT_NE(cell.grid(), origin_grid);
  EXPECT_EQ(cell.grid().column(), origin_grid.column() + 1);
}
