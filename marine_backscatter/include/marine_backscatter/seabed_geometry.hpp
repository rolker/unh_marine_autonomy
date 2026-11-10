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

#ifndef MARINE_BACKSCATTER__SEABED_GEOMETRY_HPP_
#define MARINE_BACKSCATTER__SEABED_GEOMETRY_HPP_

#include <cstdint>
#include <limits>

/// @file
/// @brief Slope-aware seabed geometry for backscatter radiometry: the seabed
///   normal from a height sampler, and the incidence / grazing angle of a beam
///   against it (ADR-0006 D4, ADR-0007 D6; unh_marine_autonomy#304, first slice).
///
/// One implementation serves both call sites: the offline sidescan Tier-2 build
/// (pass two of the two-pass import, against the finished own-survey surface)
/// and the MBES path in `cube_bathymetry` (cube#173). The module does not care
/// where heights come from: a caller hands it a @ref
/// marine_backscatter::seabed::SeabedHeightSampler.
///
/// This header includes only the standard library — no GGGS, no ROS — so a
/// consumer of the geometry alone pulls nothing else in through it. (The
/// `marine_backscatter` library it ships in also holds the store reader; see the
/// package README.)
///
/// ### Conventions
/// - **ENU** is the local east-north-up frame at the point of interest; `u` is up.
/// - **Heights are WGS84 ellipsoidal, up-positive.** An adapter over a
///   positive-down grid (CUBE depths) must negate, otherwise every normal points
///   the wrong way.
/// - A **beam vector** is the direction of arrival at the seabed point: a unit
///   vector in local ENU at that point, pointing from the sensor toward the
///   seabed. For a straight-ray caller it is the sensor → touchdown chord; a
///   caller with a sound-speed ray tracer passes the ray's final direction.
///
/// ### Planned (second half of #304, not implemented here)
/// - `double footprintArea(double slant_m, double incidence_rad, const Pulse &)` —
///   ensonified area for the GeoCoder footprint correction.
/// - `RayHit rayMarch(SeabedHeightSampler &, double lat0, double lon0, double h0,
///   const Enu & dir, ...)` — acoustic shadow and multi-valued intersection,
///   taking the same sampler and `Enu` types as @ref seabedNormal.

namespace marine_backscatter
{
namespace seabed
{

/// @brief A vector in the local east-north-up frame at a point (`u` = up).
struct Enu
{
  double e = 0.0;
  double n = 0.0;
  double u = 0.0;
};

/// @brief Metres per degree of latitude (`north`) and of longitude (`east`).
struct MetresPerDegree
{
  double north = 0.0;
  double east = 0.0;
};

/// @brief Metres per degree at @p lat_deg on the WGS84 ellipsoid.
///
/// `north = M(lat)·π/180` with the meridional radius of curvature `M`, and
/// `east = N(lat)·cos(lat)·π/180` with the prime-vertical radius `N`. This is
/// the ONE conversion the module and the store-backed sampler share: the normal
/// uses it to turn a step in metres into degrees, `BathyDem` uses it to turn a
/// cell's angular spans into metres, so the two can never disagree. `east` is 0
/// at the poles.
MetresPerDegree metresPerDegree(double lat_deg);

/// @brief Which surface answered a height query.
///
/// Sampler-defined keys: for `BathyDem` the layer index in the search order, the
/// GGGS level, and the tile's (row, column) packed into `tile`. A sampler with a
/// single surface returns the same value (the default one is fine) for every
/// sample. @ref seabedNormal requires **exact** equality across its stencil.
struct SourceId
{
  std::int64_t layer = 0;
  std::int32_t level = 0;
  std::uint64_t tile = 0;

  bool operator==(const SourceId & other) const
  {
    return layer == other.layer && level == other.level && tile == other.tile;
  }
  bool operator!=(const SourceId & other) const {return !(*this == other);}
};

/// @brief Outcome of one height query.
enum class SampleStatus
{
  kValid,              ///< height, spans and source are meaningful.
  kNoData,             ///< nothing at the probe's own cell, in any source.
  kStencilIncomplete,  ///< the probe's cell has data but a cell it would
                       ///< interpolate from does not, so no exact value exists.
};

/// @brief One height query's answer: height, per-axis cell spans and the source
///   identity, from one call.
struct HeightSample
{
  SampleStatus status = SampleStatus::kNoData;
  /// WGS84 ellipsoidal height, metres, **up-positive**.
  double height_m = std::numeric_limits<double>::quiet_NaN();
  /// Native north-south cell span at the resolved source, metres (NaN = unknown).
  double span_north_m = std::numeric_limits<double>::quiet_NaN();
  /// Native east-west cell span at the resolved source, metres (NaN = unknown).
  double span_east_m = std::numeric_limits<double>::quiet_NaN();
  SourceId source;

  bool valid() const {return status == SampleStatus::kValid;}
};

/// @brief Source of seabed heights for @ref seabedNormal.
///
/// Implementations: `marine_backscatter::BathyDem` (bathy store tiles), test
/// planes, and (cube#173) an adapter over CUBE's per-ping state.
///
/// Contract:
/// - Heights are up-positive WGS84 ellipsoidal (negate a positive-down grid).
/// - Report a partially supported interpolation as
///   @ref SampleStatus::kStencilIncomplete, never as a renormalised value: a
///   partial stencil biases a gradient at every survey edge.
/// - Always fill @ref HeightSample::source (see @ref SourceId).
/// - A position the sampler cannot place (|lat| > 90, say) is
///   @ref SampleStatus::kNoData, not an error.
/// - Throw on a genuine fault (a corrupt tile). @ref seabedNormal lets the
///   exception propagate; it never turns a fault into a flat fallback.
/// - **Not thread-safe** by contract: `heightAt` is non-const because
///   implementations cache. Use one sampler per thread.
class SeabedHeightSampler
{
public:
  virtual ~SeabedHeightSampler() = default;
  virtual HeightSample heightAt(double lat_deg, double lon_deg) = 0;
};

/// @brief Why @ref seabedNormal returned the flat normal instead of a surface one.
enum class FallbackReason
{
  kNone,               ///< not a fallback: the normal came from the surface.
  kMissingSample,      ///< centre or a probe had no data.
  kStencilIncomplete,  ///< centre or a probe sat on a partial interpolation stencil
                       ///< (typically within a cell of a survey edge).
  kLayerSeam,          ///< a probe resolved from a different source (layer, level
                       ///< or tile) than the centre.
  kNoCellSize,         ///< auto step requested but the sampler reported no span.
  kBadStep,            ///< negative / non-finite step, or bad auto_step_cells.
  kBadPosition,        ///< non-finite or out-of-range position, or the stencil
                       ///< would reach a pole.
};

/// @brief Stable lower-case name of @p reason, for callers' counters and logs.
const char * toString(FallbackReason reason);

/// @brief Scale of the finite-difference stencil.
struct NormalOptions
{
  /// Probe offset in metres; 0 = auto (@ref auto_step_cells native cells).
  double step_m = 0.0;
  /// Auto step in native cells (of the larger of the two cell spans).
  double auto_step_cells = 3.0;
};

/// @brief A seabed normal, or the flat fallback and why.
struct Normal
{
  /// Unit normal in local ENU, up-pointing; `{0,0,1}` on fallback.
  Enu normal{0.0, 0.0, 1.0};
  /// True when @ref normal came from the surface, false on a flat fallback.
  bool from_surface = false;
  FallbackReason reason = FallbackReason::kNone;
  /// True when the step was raised to the one-native-cell floor.
  bool step_clamped = false;
  /// The probe offset actually used, metres (0 if no step was established).
  double step_used_m = 0.0;
};

/// @brief Seabed normal at (@p lat_deg, @p lon_deg) by central differences.
///
/// Samples the centre (for the cell spans and the reference source identity),
/// then probes east/west and north/south at ±step. The step is
/// `options.step_m` if positive, else `options.auto_step_cells` × the larger cell
/// span; either way it is floored at one native cell (larger span) when the spans
/// are known, with @ref Normal::step_clamped set. The normal is
/// `normalize(-dh/de, -dh/dn, 1)`. Only height differences enter, so a locally
/// constant datum offset cancels.
///
/// Every failure is the flat normal with `from_surface = false` and a
/// @ref FallbackReason. A bad centre sample (missing, incomplete, no cell size) or
/// bad position/step returns before any probe is taken; otherwise, once all four
/// probes are in, the order is: missing, stencil incomplete, source seam. The
/// module holds no state; callers count fallbacks per reason.
///
/// @throws whatever the sampler throws (propagated, never swallowed).
Normal seabedNormal(
  SeabedHeightSampler & sampler, double lat_deg, double lon_deg,
  const NormalOptions & options = NormalOptions{});

/// @brief Tolerance on the length of a unit vector: `|‖v‖ − 1| <= kUnitTolerance`.
constexpr double kUnitTolerance = 1e-6;

/// @brief An angle, or a flagged invalid result the caller counts.
struct AngleResult
{
  double rad = std::numeric_limits<double>::quiet_NaN();
  bool valid = false;
};

/// @brief Incidence angle of @p beam_dir on a surface with unit normal @p normal.
///
/// `acos(clamp(dot(-beam_dir, normal), -1, 1))` in [0, π]; 0 = normal incidence.
/// A value above π/2 (the beam strikes the back of the surface, a steep reverse
/// slope) is returned **raw**, not clamped: callers treat it as unusable, and
/// the ray-march follow-up needs the real geometry.
///
/// @p beam_dir is the direction of arrival at the seabed point (see the file
/// conventions). Either vector non-finite, or off unit length by more than
/// @ref kUnitTolerance, gives `valid = false` — no exception, no silent
/// renormalisation; one bad nav sample must not take down a per-beam path.
AngleResult incidenceAngle(const Enu & beam_dir, const Enu & normal);

/// @brief Grazing angle = π/2 − @ref incidenceAngle; negative for a back-facing hit.
AngleResult grazingAngle(const Enu & beam_dir, const Enu & normal);

/// @brief Unit beam vector from the horizontal azimuth of travel (radians,
///   clockwise from north) and the depression angle below horizontal (radians):
///   `(sin az · cos dep, cos az · cos dep, −sin dep)`.
Enu beamFromAzimuthDepression(double azimuth_rad, double depression_rad);

/// @brief Re-express a north-east-down vector in east-north-up: `(east, north, −down)`.
Enu enuFromNed(double north, double east, double down);

}  // namespace seabed
}  // namespace marine_backscatter

#endif  // MARINE_BACKSCATTER__SEABED_GEOMETRY_HPP_
