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

#ifndef HELM_MANAGER_CURVATURE_REGULATION_H
#define HELM_MANAGER_CURVATURE_REGULATION_H

/// Curvature-preserving speed regulation (ADR-0012, #292).
///
/// When a commanded (v, ω) pair exceeds the platform's speed-dependent yaw
/// capability, both components are scaled by the same factor s ∈ (0, 1] so
/// the yaw rate becomes achievable while the commanded curvature v/ω — the
/// turn radius — is preserved exactly. Clipping ω alone widens the executed
/// turn; reducing v alone tightens it; either way the boat leaves the planned
/// arc. Proportional scaling keeps it on the arc, traversed slower.
///
/// The capability envelope is a per-platform piecewise-linear table of
/// ω_max versus v. It is NOT assumed monotonic: measured envelopes
/// (BizzyBoat's differential drivetrain, 2026-08-04) peak at mid-speed and
/// are weaker at rest. Pure logic, no ROS dependencies — the HelmManager
/// node owns parameter plumbing and calls into this header.

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace helm_manager
{

  struct CurvatureRegulationConfig
  {
  /// Master gate; default off so existing platforms are unaffected.
    bool enabled = false;

  /// Flat [v0, w0, v1, w1, ...] pairs: achievable |yaw rate| w (rad/s) at
  /// forward |speed| v (m/s). v strictly ascending, all values finite and
  /// non-negative. Linear interpolation within segments, clamp-constant
  /// beyond both ends.
    std::vector < double > curve;

  /// Safety factor in (0, 1] applied to every ω_max value.
    double margin = 0.8;

  /// Floor speed (m/s): when the commanded yaw is unachievable at any speed,
  /// keep moving at this speed with the yaw capability it offers rather than
  /// slowing toward a stop mid-line.
    double pivot_speed = 0.05;
  };

/// Validate a config. Returns true when usable; on failure fills `error`
/// with a human-readable reason. An empty curve with enabled=false is valid
/// (the feature is simply off); enabled=true requires a well-formed curve.
  inline bool validateCurvatureConfig(
    const CurvatureRegulationConfig & config, std::string & error)
  {
    if (!config.enabled) {
      return true;
    }
    const auto & c = config.curve;
    if (c.empty() || c.size() % 2 != 0) {
      error = "capability_curve_v_omega_max must hold [v, omega_max] pairs "
        "(got " + std::to_string(c.size()) + " values)";
      return false;
    }
    for (size_t i = 0; i < c.size(); i += 2) {
      if (!std::isfinite(c[i]) || !std::isfinite(c[i + 1]) ||
        c[i] < 0.0 || c[i + 1] < 0.0)
      {
        error = "capability_curve_v_omega_max entries must be finite and "
          "non-negative";
        return false;
      }
      if (i >= 2 && c[i] <= c[i - 2]) {
        error = "capability_curve_v_omega_max speeds must be strictly "
          "ascending";
        return false;
      }
    }
    if (!std::isfinite(config.margin) ||
      config.margin <= 0.0 || config.margin > 1.0)
    {
      error = "capability_curve_margin must be in (0, 1]";
      return false;
    }
    if (!std::isfinite(config.pivot_speed) || config.pivot_speed < 0.0) {
      error = "capability_curve_pivot_speed must be non-negative";
      return false;
    }
    return true;
  }

/// Margin-scaled capability λ(x): achievable |yaw| at |speed| x, linearly
/// interpolated within the table and clamp-constant beyond both ends.
/// Precondition: curve validated non-empty.
  inline double curvatureCapabilityAt(
    const CurvatureRegulationConfig & config, double speed)
  {
    const auto & c = config.curve;
    const double x = std::abs(speed);
    if (x <= c.front()) {
      return c[1] * config.margin;
    }
    if (x >= c[c.size() - 2]) {
      return c.back() * config.margin;
    }
    for (size_t i = 2; i < c.size(); i += 2) {
      if (x <= c[i]) {
        const double va = c[i - 2], wa = c[i - 1];
        const double vb = c[i], wb = c[i + 1];
        const double t = (x - va) / (vb - va);
        return (wa + t * (wb - wa)) * config.margin;
      }
    }
    return c.back() * config.margin;  // unreachable; defensive
  }

/// Apply curvature-preserving regulation to a commanded (v, omega).
///
/// Returns the pair to command downstream. Contract (ADR-0012):
///  - Disabled, empty curve, omega == 0, or non-finite input: passthrough.
///  - Pivot command (v == 0): omega clamped to rest capability, v stays 0.
///  - Inside the envelope: passthrough.
///  - Over the envelope: both components scaled by the same s ∈ (0, 1] —
///    curvature preserved; the scan is top-down over the piecewise segments
///    (largest feasible speed wins; no monotonicity assumption).
///  - Never speeds up (s <= 1), never flips a sign, never returns v = 0 for
///    a moving command: if the feasible speed falls below pivot_speed, the
///    result is pivot_speed (or the smaller commanded |v|) with the maximum
///    achievable yaw at that speed — the only case curvature is sacrificed.
  inline std::pair < double, double > applyCurvatureRegulation(
  double v, double omega, const CurvatureRegulationConfig & config)
{
  if (!config.enabled || config.curve.empty() ||
      !std::isfinite(v) || !std::isfinite(omega) || omega == 0.0)
    {
      return {v, omega};
  }

  const double abs_v = std::abs(v);
  const double abs_w = std::abs(omega);
  const double sign_v = (v < 0.0) ? -1.0 : 1.0;
  const double sign_w = (omega < 0.0) ? -1.0 : 1.0;

  if (abs_v == 0.0) {
    // True pivot: no speed to trade, clamp yaw to rest capability.
      const double rest = curvatureCapabilityAt(config, 0.0);
      return {0.0, sign_w * std::min(abs_w, rest)};
  }

  if (abs_w <= curvatureCapabilityAt(config, abs_v)) {
      return {v, omega};
  }

  // Find the largest x ∈ (0, abs_v] with (x / abs_v) * abs_w <= λ(x),
  // i.e. f(x) = λ(x) - k·x >= 0 where k = abs_w / abs_v (the commanded
  // curvature, in per-unit form). λ is piecewise linear, so walk the
  // segments top-down; the first feasible point is the global maximum.
  const double k = abs_w / abs_v;

  // Segment boundaries within [0, abs_v]: 0, interior table speeds, abs_v.
  std::vector < double > xs;
  xs.push_back(0.0);
  for (size_t i = 0; i < config.curve.size(); i += 2) {
      const double bv = config.curve[i];
      if (bv > 0.0 && bv < abs_v) {
        xs.push_back(bv);
      }
  }
  xs.push_back(abs_v);

  auto f = [&](double x) {return curvatureCapabilityAt(config, x) - k * x;};

  double x_star = -1.0;
  for (size_t i = xs.size() - 1; i > 0; --i) {
      const double a = xs[i - 1], b = xs[i];
      const double fa = f(a), fb = f(b);
      if (fb >= 0.0) {
        x_star = b;
        break;
      }
      if (fa >= 0.0) {
      // f is linear on [a, b] with fa >= 0 > fb: largest feasible point is
      // the crossing.
        x_star = a + fa * (b - a) / (fa - fb);
        break;
      }
  }

  if (x_star <= 0.0 || x_star < config.pivot_speed) {
    // Floor: commanded yaw is beyond capability even at crawl. Keep moving
    // at the floor speed (never above the commanded speed) with the maximum
    // achievable yaw there. Curvature is knowingly sacrificed here only.
      const double floor_v = std::min(config.pivot_speed, abs_v);
      return {sign_v * floor_v,
                 sign_w * curvatureCapabilityAt(config, floor_v)};
  }

  const double s = x_star / abs_v;
    return {sign_v * x_star, sign_w * s * abs_w};
  }

}  // namespace helm_manager

#endif  // HELM_MANAGER_CURVATURE_REGULATION_H
