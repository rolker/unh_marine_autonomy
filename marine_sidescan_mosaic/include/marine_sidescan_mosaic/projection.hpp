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

#ifndef MARINE_SIDESCAN_MOSAIC__PROJECTION_HPP_
#define MARINE_SIDESCAN_MOSAIC__PROJECTION_HPP_

#include <cmath>

#include "geographic_msgs/msg/geo_point.hpp"
#include "marine_autonomy/gggs.h"

/// @file
/// @brief Pure geometry for placing a sidescan sample on the ground.
///
/// No ROS/TF state — just the math the node feeds with a TF pose + ping. Ellipsoid
/// work goes through `geodesy` (ADR-0002 §D8): ECEF↔geodetic and the Vincenty
/// `wgs84::direct` for across-track placement. Heading extraction (ECEF
/// orientation → local-NED azimuth) is the body→NED yaw, ported from the verified
/// `bag_to_xtf` `geo.py` (`ecef_pose_to_geo` / `matrix_to_heading_pitch_roll`).

namespace marine_sidescan_mosaic
{

/// @brief Which side of the vessel a sidescan channel images.
enum class Side
{
  Port,
  Starboard
};

/// @brief Geographic origin + sensor heading from an `earth`→sensor TF pose.
struct GeoHeading
{
  double latitude_deg = 0.0;
  double longitude_deg = 0.0;
  double altitude_m = 0.0;     ///< Ellipsoidal height of the sensor origin (m).
  double heading_rad = 0.0;    ///< Sensor body +x azimuth, clockwise from north.
};

/// @brief Convert an `earth`(ECEF)→sensor TF pose to geographic origin + heading.
///
/// @param tx,ty,tz ECEF translation of the sensor origin (metres).
/// @param qx,qy,qz,qw Sensor orientation in ECEF (tf2 convention, `p_parent =
///   R p_child`). A near-zero quaternion yields heading 0.
/// @return Geographic position (via `geodesy`) plus the body +x axis azimuth in
///   the local-NED plane at the origin.
GeoHeading ecefPoseToGeoHeading(
  double tx, double ty, double tz,
  double qx, double qy, double qz, double qw);

/// @brief Geographic origin + **full-attitude** beam direction from an
///   `earth`(ECEF)→sensor pose.
struct GeoBeam
{
  double latitude_deg = 0.0;
  double longitude_deg = 0.0;
  double altitude_m = 0.0;       ///< Ellipsoidal height of the sensor origin (m).
  double azimuth_rad = 0.0;      ///< Across-track azimuth: the horizontal projection of
                                 ///<   the sensor's +Z (range) axis, clockwise from north.
                                 ///<   Side, mounting tilt, and dynamic roll all compose in.
  double depression_rad = 0.0;   ///< +Z axis depression below horizontal (>0 = down);
                                 ///<   the beam grazing seed for radiometry (#185).
};

/// @brief Full-attitude counterpart to @ref ecefPoseToGeoHeading: returns the
///   geographic origin plus the **beam (+Z range axis)** azimuth and depression,
///   so static mounting tilt and dynamic roll compose — unlike the yaw-only
///   heading. The per-channel frame's +Z already encodes the look side, so no
///   @ref Side is needed. Across-track placement on a flat bottom still uses
///   `groundRange(slant, altitude)` at this azimuth.
GeoBeam ecefPoseToGeoBeam(
  double tx, double ty, double tz,
  double qx, double qy, double qz, double qw);

/// @brief Slant range (m) to delivered sample @p j (0-based), from the near-field
///   gate @p sample0, sound speed (m/s) and `RawSonarImage` `sample_rate` (Hz).
inline double slantRange(int j, int sample0, double sound_speed, double sample_rate)
{
  return (sample0 + j) * sound_speed / (2.0 * sample_rate);
}

/// @brief Horizontal ground range (m) from slant range and altitude above bottom.
///   Returns 0 when the slant range is within the altitude (near-nadir cone).
inline double groundRange(double slant_range, double altitude)
{
  const double d2 = slant_range * slant_range - altitude * altitude;
  return d2 > 0.0 ? std::sqrt(d2) : 0.0;
}

/// @brief Across-track azimuth (radians, clockwise from north) for a @p side.
///   Starboard is heading + 90°, port is heading − 90°.
inline double acrossTrackAzimuth(double heading_rad, Side side)
{
  constexpr double half_pi = M_PI / 2.0;
  return heading_rad + (side == Side::Starboard ? half_pi : -half_pi);
}

/// @brief Project one sample to its GGGS `CellIndex` at @p level.
///
/// @param origin Sensor ground position — **altitude must be 0** (the
///   `geodesy::wgs84::direct` precondition); the caller zeroes it.
/// @param azimuth_rad Across-track azimuth (radians, clockwise from north).
/// @param ground_range Horizontal range to the sample (m).
/// The grid is resolved from the sample's **own** position (`Level::cellIndex`),
/// so a sample crossing into a neighbouring grid lands there rather than being
/// clamped to the origin grid's edge.
gggs::CellIndex projectSample(
  const geographic_msgs::msg::GeoPoint & origin,
  double azimuth_rad, double ground_range, const gggs::Level & level);

}  // namespace marine_sidescan_mosaic

#endif  // MARINE_SIDESCAN_MOSAIC__PROJECTION_HPP_
