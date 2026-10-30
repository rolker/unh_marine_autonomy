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

#include "marine_backscatter/seabed_geometry.hpp"

#include <algorithm>
#include <cmath>

namespace marine_backscatter
{
namespace seabed
{

namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;

// WGS84 defining constants (semi-major axis, flattening).
constexpr double kWgs84A = 6378137.0;
constexpr double kWgs84F = 1.0 / 298.257223563;
constexpr double kWgs84E2 = kWgs84F * (2.0 - kWgs84F);

bool positiveFinite(double v)
{
  return std::isfinite(v) && v > 0.0;
}

// Longitude wrapped into [-180, 180).
double wrapLongitude(double lon_deg)
{
  double wrapped = std::fmod(lon_deg + 180.0, 360.0);
  if (wrapped < 0.0) {
    wrapped += 360.0;
  }
  return wrapped - 180.0;
}

Normal flat(FallbackReason reason, double step_m = 0.0, bool clamped = false)
{
  Normal result;
  result.reason = reason;
  result.step_used_m = step_m;
  result.step_clamped = clamped;
  return result;
}

// A sample's own failure reason, or kNone when it is usable. A sampler that
// claims a valid sample but hands back a non-finite height is treated as
// missing rather than letting a NaN into the gradient.
FallbackReason sampleFailure(const HeightSample & sample)
{
  switch (sample.status) {
    case SampleStatus::kValid:
      return std::isfinite(sample.height_m) ? FallbackReason::kNone :
             FallbackReason::kMissingSample;
    case SampleStatus::kStencilIncomplete:
      return FallbackReason::kStencilIncomplete;
    case SampleStatus::kNoData:
    default:
      return FallbackReason::kMissingSample;
  }
}

bool isFiniteUnit(const Enu & v)
{
  if (!std::isfinite(v.e) || !std::isfinite(v.n) || !std::isfinite(v.u)) {
    return false;
  }
  const double norm = std::sqrt(v.e * v.e + v.n * v.n + v.u * v.u);
  return std::abs(norm - 1.0) <= kUnitTolerance;
}

}  // namespace

MetresPerDegree metresPerDegree(double lat_deg)
{
  const double phi = lat_deg * kDegToRad;
  const double s = std::sin(phi);
  const double w2 = 1.0 - kWgs84E2 * s * s;
  const double w = std::sqrt(w2);
  const double meridional = kWgs84A * (1.0 - kWgs84E2) / (w2 * w);
  const double prime_vertical = kWgs84A / w;
  MetresPerDegree out;
  out.north = meridional * kDegToRad;
  // cos(±90°) is ~6e-17 in floating point, not 0: report exactly 0 at the poles.
  out.east = std::abs(lat_deg) >= 90.0 ? 0.0 : prime_vertical * std::cos(phi) * kDegToRad;
  return out;
}

const char * toString(FallbackReason reason)
{
  switch (reason) {
    case FallbackReason::kNone: return "none";
    case FallbackReason::kMissingSample: return "missing_sample";
    case FallbackReason::kStencilIncomplete: return "stencil_incomplete";
    case FallbackReason::kLayerSeam: return "layer_seam";
    case FallbackReason::kNoCellSize: return "no_cell_size";
    case FallbackReason::kBadStep: return "bad_step";
    case FallbackReason::kBadPosition: return "bad_position";
  }
  return "unknown";
}

Normal seabedNormal(
  SeabedHeightSampler & sampler, double lat_deg, double lon_deg, const NormalOptions & options)
{
  if (!std::isfinite(lat_deg) || !std::isfinite(lon_deg) || std::abs(lat_deg) > 90.0) {
    return flat(FallbackReason::kBadPosition);
  }
  if (!std::isfinite(options.step_m) || options.step_m < 0.0) {
    return flat(FallbackReason::kBadStep);
  }
  if (options.step_m == 0.0 && !positiveFinite(options.auto_step_cells)) {
    return flat(FallbackReason::kBadStep);
  }

  const double lon = wrapLongitude(lon_deg);
  const HeightSample centre = sampler.heightAt(lat_deg, lon);
  const FallbackReason centre_failure = sampleFailure(centre);
  if (centre_failure != FallbackReason::kNone) {
    return flat(centre_failure);
  }

  // Step: explicit, or auto in native cells of the larger span (so it covers at
  // least one cell on both axes); floored at one native cell whenever the spans
  // are known — differencing below the native cell only differences the
  // interpolation inside one cell.
  const bool spans_known = positiveFinite(centre.span_north_m) &&
    positiveFinite(centre.span_east_m);
  const double cell_m = spans_known ? std::max(centre.span_north_m, centre.span_east_m) : 0.0;
  double step = options.step_m;
  if (step == 0.0) {
    if (!spans_known) {
      return flat(FallbackReason::kNoCellSize);
    }
    step = options.auto_step_cells * cell_m;
  }
  bool clamped = false;
  if (spans_known && step < cell_m) {
    step = cell_m;
    clamped = true;
  }
  if (!positiveFinite(step)) {
    return flat(FallbackReason::kBadStep, 0.0, clamped);
  }

  const MetresPerDegree mpd = metresPerDegree(lat_deg);
  if (!positiveFinite(mpd.east) || !positiveFinite(mpd.north)) {
    return flat(FallbackReason::kBadPosition, step, clamped);   // at a pole.
  }
  const double dlat = step / mpd.north;
  const double dlon = step / mpd.east;
  if (std::abs(lat_deg) + dlat >= 90.0 || !std::isfinite(dlon) || dlon >= 180.0) {
    return flat(FallbackReason::kBadPosition, step, clamped);
  }

  // All four probes are taken before any guard, so the reason reported does not
  // depend on probe order: missing beats incomplete beats seam.
  const HeightSample east = sampler.heightAt(lat_deg, wrapLongitude(lon + dlon));
  const HeightSample west = sampler.heightAt(lat_deg, wrapLongitude(lon - dlon));
  const HeightSample north = sampler.heightAt(lat_deg + dlat, lon);
  const HeightSample south = sampler.heightAt(lat_deg - dlat, lon);
  const HeightSample * probes[4] = {&east, &west, &north, &south};

  bool any_missing = false;
  bool any_incomplete = false;
  bool any_seam = false;
  for (const HeightSample * probe : probes) {
    const FallbackReason failure = sampleFailure(*probe);
    if (failure == FallbackReason::kMissingSample) {
      any_missing = true;
    } else if (failure == FallbackReason::kStencilIncomplete) {
      any_incomplete = true;
    } else if (probe->source != centre.source) {
      any_seam = true;
    }
  }
  if (any_missing) {
    return flat(FallbackReason::kMissingSample, step, clamped);
  }
  if (any_incomplete) {
    return flat(FallbackReason::kStencilIncomplete, step, clamped);
  }
  if (any_seam) {
    return flat(FallbackReason::kLayerSeam, step, clamped);
  }

  const double dh_de = (east.height_m - west.height_m) / (2.0 * step);
  const double dh_dn = (north.height_m - south.height_m) / (2.0 * step);
  const double norm = std::sqrt(dh_de * dh_de + dh_dn * dh_dn + 1.0);
  Normal result;
  result.normal = Enu{-dh_de / norm, -dh_dn / norm, 1.0 / norm};
  result.from_surface = true;
  result.reason = FallbackReason::kNone;
  result.step_clamped = clamped;
  result.step_used_m = step;
  return result;
}

AngleResult incidenceAngle(const Enu & beam_dir, const Enu & normal)
{
  AngleResult result;
  if (!isFiniteUnit(beam_dir) || !isFiniteUnit(normal)) {
    return result;
  }
  const double cos_incidence =
    -(beam_dir.e * normal.e + beam_dir.n * normal.n + beam_dir.u * normal.u);
  result.rad = std::acos(std::clamp(cos_incidence, -1.0, 1.0));
  result.valid = true;
  return result;
}

AngleResult grazingAngle(const Enu & beam_dir, const Enu & normal)
{
  AngleResult result = incidenceAngle(beam_dir, normal);
  if (result.valid) {
    result.rad = 0.5 * kPi - result.rad;
  }
  return result;
}

Enu beamFromAzimuthDepression(double azimuth_rad, double depression_rad)
{
  const double horizontal = std::cos(depression_rad);
  return Enu{
    std::sin(azimuth_rad) * horizontal,
    std::cos(azimuth_rad) * horizontal,
    -std::sin(depression_rad)};
}

Enu enuFromNed(double north, double east, double down)
{
  return Enu{east, north, -down};
}

}  // namespace seabed
}  // namespace marine_backscatter
