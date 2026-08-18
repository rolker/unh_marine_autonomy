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

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include "geodesy/geodesics.h"
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
  double heading_rad = 0.0;      ///< Sensor body +x azimuth (vessel ground track),
                                 ///<   clockwise from north — the along-track splat axis.
  bool valid = true;             ///< false if the quaternion was near-zero/degenerate
                                 ///<   (azimuth/depression/heading are 0); callers should skip.
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

/// @brief Outcome of a DEM-based across-track range correction (@ref correctedGroundRange).
struct DemCorrection
{
  /// @brief Which fixed point (if any) the iteration reached.
  enum class Status
  {
    kConverged,      ///< Settled within tolerance; @ref ground_range is DEM-corrected.
    kNoCoverage,     ///< The DEM had no data at a probed position; flat-bottom result.
    kDegenerate,     ///< Bottom at/above the sensor, or the sample inside the nadir
                     ///<   cone for the probed depth; flat-bottom result.
    kNotConverged,   ///< Hit the iteration cap without meeting tolerance; flat-bottom
                     ///<   result (the non-converged iterate is never emitted).
  };

  /// @brief Corrected horizontal range (m) when @ref status is `kConverged`,
  ///   otherwise the caller's flat-bottom seed, unchanged.
  double ground_range = 0.0;

  /// @brief Sensor ellipsoidal height − bottom ellipsoidal height (m, positive
  ///   down to the seafloor) at the converged sample position. **Only meaningful
  ///   when @ref status is `kConverged`**; NaN otherwise — a caller taking the
  ///   flat-bottom fallback keeps its own `(nadir_altitude_m, flat_ground)` pair.
  double vertical_offset = std::numeric_limits<double>::quiet_NaN();

  Status status = Status::kNoCoverage;
};

/// @brief Convergence tolerance (m) for @ref correctedGroundRange — a tenth of the
///   L13 cell size (~0.11 m), so the iteration stops well inside one output cell.
constexpr double kDemConvergenceToleranceM = 0.01;

/// @brief Hard cap on @ref correctedGroundRange fixed-point iterations. Bounds the
///   per-sample DEM cost at `kMaxDemIterations × 4` cell reads (four per bilinear
///   stencil); a seed already within tolerance costs one stencil, not five.
constexpr int kMaxDemIterations = 5;

/// @brief DEM-corrected (orthorectified) ground range for one sidescan sample.
///
/// Replaces the flat-bottom `groundRange(slant, held_nadir_altitude)` placement,
/// which mis-places every off-nadir sample on a sloping or irregular bottom
/// (issue #297; the geometric correction ADR-0006 D4 defers to this phase).
///
/// Fixed-point iteration seeded at the flat-bottom range: step to the candidate
/// ground position, read the DEM there, recompute the vertical offset, and take
/// the flat formula's range for **that** offset — which is exact for the local
/// tangent-plane distance at the candidate point.
///
/// **Convergence.** With bottom height `z(r)` along the beam azimuth and
/// `v(r) = sensor_height − z(r)`, the map is `f(r) = sqrt(R² − v(r)²)`, so
/// `f'(r) = −tan(θ)·tan(β)` where `θ` is the ray's local grazing angle and `β` the
/// bottom slope along the beam. The iteration contracts iff `θ + β < 90°`: it
/// degrades for steep slopes and (because `tan θ → ∞`) near nadir. The near-nadir
/// regime is excluded by the caller's nadir-cone gate plus the degeneracy guard
/// below; anything still unsettled at the cap returns `kNotConverged`.
///
/// **Multi-valued intersections / acoustic shadow (v1 scope).** On a steep slope
/// the ray can meet the bottom at more than one range, or at none (the sample lies
/// in shadow). The iteration converges to whichever fixed point its seed leads to
/// — seeded flat, that is the one nearest the flat solution. v1 accepts the
/// nearest converged solution; shadow detection (a ray-march along the DEM
/// profile) is out of scope. That regime coincides with the `θ + β < 90°`
/// contraction failure, so those samples surface as `kNotConverged`.
///
/// @param slant_range Slant range to the sample (m).
/// @param sensor_height_m **WGS84 ellipsoidal height of the sensor** (m,
///   up-positive) — `GeoBeam::altitude_m`, from the Tier-1 baked `earth`→transducer
///   pose (ADR-0006 D2). Never `nadir_altitude_m`, which is a height above bottom;
///   the DEM's `depth` is an ellipsoidal height, so both sides of the subtraction
///   must be the same datum.
/// @param origin Sensor ground position — **altitude must be 0** (the
///   `geodesy::wgs84::direct` precondition).
/// @param azimuth_rad Across-track azimuth to the sample (clockwise from north).
/// @param flat_ground_range Flat-bottom seed, i.e. `groundRange(slant,
///   nadir_altitude_m)`; also the value returned on every degraded status.
/// @param depth_at Callback `(lat_deg, lon_deg) -> std::optional<double>` returning
///   the bottom's **ellipsoidal height** (up-positive), `nullopt` where the DEM has
///   no data. Templated (like @ref splatAlongTrack's `Deposit`) so this header
///   stays free of file I/O.
template<typename DepthLookup>
DemCorrection correctedGroundRange(
  double slant_range,
  double sensor_height_m,
  const geographic_msgs::msg::GeoPoint & origin,
  double azimuth_rad,
  double flat_ground_range,
  DepthLookup && depth_at)
{
  DemCorrection result;
  result.ground_range = flat_ground_range;

  double range = flat_ground_range;
  for (int i = 0; i < kMaxDemIterations; ++i) {
    const auto candidate = geodesy::wgs84::direct(origin, azimuth_rad, range);
    const std::optional<double> bottom = depth_at(candidate.latitude, candidate.longitude);
    if (!bottom) {
      result.status = DemCorrection::Status::kNoCoverage;
      return result;   // D4-documented degenerate fallback: flat-bottom placement.
    }
    // Both terms are up-positive WGS84 ellipsoidal heights, so v > 0 means the
    // seafloor lies below the sensor.
    const double vertical = sensor_height_m - *bottom;
    // Degeneracy guard: a bottom at or above the sensor (bad DEM cell or bad pose)
    // or a sample inside the nadir cone for this depth would drive the next iterate
    // to 0 — which re-probes the DEM at the sensor's own nadir point and oscillates.
    if (!std::isfinite(vertical) || vertical <= 0.0 || vertical >= slant_range) {
      result.status = DemCorrection::Status::kDegenerate;
      result.ground_range = flat_ground_range;
      return result;
    }
    const double next = groundRange(slant_range, vertical);
    const double delta = std::abs(next - range);
    range = next;
    if (delta < kDemConvergenceToleranceM) {
      result.ground_range = range;
      result.vertical_offset = vertical;
      result.status = DemCorrection::Status::kConverged;
      return result;
    }
  }
  // Cap reached. The non-converged iterate's error is unbounded, so it is never
  // emitted — fall back to flat and let the caller count the condition.
  result.ground_range = flat_ground_range;
  result.status = DemCorrection::Status::kNotConverged;
  return result;
}

/// @brief Across-track azimuth (radians, clockwise from north) for a @p side.
///   Starboard is heading + 90°, port is heading − 90°.
///
/// @note No longer used by the live node path, which now reads the full-attitude
///   beam azimuth from @ref ecefPoseToGeoBeam (the sensor +Z direction) so static
///   mount tilt and dynamic roll compose in. Kept for callers that still want the
///   yaw-only heading-± convention (e.g. offline bag tools) and as the regression
///   reference in `test_projection.cpp`.
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

/// @brief Along-track footprint length (m) for one sample: `slant_range ·
///   tx_beamwidth_rad` (small-angle arc at the sample's own slant range).
///
/// Uses the **per-sample** slant range, so near-range samples get a shorter
/// footprint than far-range ones. Returns 0 when either input is 0 — the
/// caller then collapses to a single point-deposit (@ref splatAlongTrack).
inline double footprintAlongTrack(double slant_range, double tx_beamwidth_rad)
{
  return slant_range * tx_beamwidth_rad;
}

/// @brief Plausibility ceiling (rad) for an along-track transmit beamwidth.
///   A real SideVü-class along-track beamwidth is well under this (~5.7°); a
///   wider value is almost certainly degrees mistaken for radians (#208 review).
constexpr double kMaxPlausibleBeamwidthRad = 0.1;

/// @brief Validate a transmit beamwidth (rad) before the along-track splat.
///
/// One definition shared by every beamwidth input path — the live
/// `tx_beamwidths[0]`, the node's configured fallback, and the Tier-2
/// stored/CLI beamwidth — so they agree on what is usable. A non-finite or
/// negative value is unusable and collapses to 0 (the legacy point-deposit,
/// `n_steps = 1`). A finite value wider than @ref kMaxPlausibleBeamwidthRad is
/// returned unchanged but flagged via @p suspicious, so the caller can warn in
/// its own logging idiom (RCLCPP in the node, stderr in the CLI tools); it is
/// still hard-capped per-sample by `kMaxSplatSteps` in @ref splatAlongTrack.
inline double sanitizeBeamwidthRad(double bw_rad, bool * suspicious = nullptr)
{
  if (suspicious) {
    *suspicious = false;
  }
  if (!std::isfinite(bw_rad) || bw_rad < 0.0) {
    return 0.0;
  }
  if (suspicious && bw_rad > kMaxPlausibleBeamwidthRad) {
    *suspicious = true;
  }
  return bw_rad;
}

/// @brief Deposit one across-track sample across its along-track footprint.
///
/// Splats the sample over `n_steps = max(1, ceil(footprint_m / cell_size))` cells
/// centred on @p origin and stepped along @p heading_rad (the vessel ground track),
/// invoking @p deposit once per covered @ref gggs::CellIndex. The steps are spaced
/// `cell_size` apart and centred **symmetrically** about the ping, so an even
/// `n_steps` straddles the origin (±half-cell) instead of under-covering by one.
/// With `footprint_m < cell_size` (or 0) this reduces to a single deposit at the
/// across-track sample position — bit-for-bit the legacy point-deposit.
///
/// @param origin Sensor ground position — **altitude must be 0** (the
///   `geodesy::wgs84::direct` precondition; the along-track step preserves it).
/// @param heading_rad Along-track azimuth (vessel ground track, clockwise from north).
/// @param footprint_m Along-track footprint length (m); see @ref footprintAlongTrack.
/// @param azimuth_rad Across-track azimuth to the sample (clockwise from north).
/// @param ground_range Horizontal range to the sample (m).
/// @param level GGGS level (supplies the cell size and resolves each cell).
/// @param deposit Callback `void(const gggs::CellIndex &)` run once per covered cell.
template<typename Deposit>
void splatAlongTrack(
  const geographic_msgs::msg::GeoPoint & origin,
  double heading_rad, double footprint_m,
  double azimuth_rad, double ground_range,
  const gggs::Level & level, Deposit && deposit)
{
  const double cell_size = level.cellSize();
  // Safety backstop (#208 review): a non-finite footprint_m (NaN/inf slant or a
  // garbage beamwidth) would make the static_cast<int> below UB, and an
  // absurd-but-finite footprint (e.g. a degrees-for-radians beamwidth) would
  // deposit thousands of cells per sample in the live real-time onPing callback.
  // Collapse a non-finite/zero footprint to the single point-deposit and hard-cap
  // the step count so one sample can never run away.
  constexpr int kMaxSplatSteps = 1024;
  int n_steps = 1;
  if (std::isfinite(footprint_m) && footprint_m > 0.0) {
    // ceil (not round): a 1.0–1.5-cell footprint must paint 2 cells, else round
    // collapses it to a single deposit and the along-track gap it was added to fill
    // survives. A sub-cell footprint still yields ceil→1 (the legacy point-deposit).
    //
    // Clamp the step count to [1, kMaxSplatSteps] *in double, before the cast*: a
    // finite-but-huge footprint (a degrees-for-radians beamwidth, or a corrupt
    // per-ping value) can drive footprint_m/cell_size past INT_MAX, and
    // static_cast<int> of an out-of-range double is UB — the isfinite guard above
    // only catches NaN/inf. Bounding in double keeps the cast operand in range.
    const double steps = std::ceil(footprint_m / cell_size);
    n_steps = static_cast<int>(
      std::min(static_cast<double>(kMaxSplatSteps), std::max(1.0, steps)));
  }
  for (int k = 0; k < n_steps; ++k) {
    // Symmetric centring: offsets are spaced cell_size apart about 0 (n_steps=1 →
    // offset 0 → origin untouched, so the single-deposit path is unchanged).
    const double offset = (static_cast<double>(k) - (n_steps - 1) / 2.0) * cell_size;
    geographic_msgs::msg::GeoPoint splat_origin = origin;
    if (offset != 0.0) {
      // wgs84::direct takes a non-negative distance; a negative offset is the
      // reciprocal heading. The step keeps altitude 0 (direct copies p1.altitude).
      const double along_az = offset >= 0.0 ? heading_rad : heading_rad + M_PI;
      splat_origin = geodesy::wgs84::direct(origin, along_az, std::abs(offset));
    }
    deposit(projectSample(splat_origin, azimuth_rad, ground_range, level));
  }
}

}  // namespace marine_sidescan_mosaic

#endif  // MARINE_SIDESCAN_MOSAIC__PROJECTION_HPP_
