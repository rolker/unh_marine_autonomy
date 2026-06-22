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

/// @file
/// @brief Flat-bottom Tier-2 projector (ADR-0006 D1/D10, issue #184).
///
/// Reads a Tier-1 `.sst1` archive (NOT the bags) and projects each ping to
/// GGGS-tiled `uint16` GeoTIFF tiles — proving the headline two-tier claim: the
/// mosaic is re-derivable from Tier-1 without re-reading the bags. This is the
/// **flat-bottom** projection (altitude = the ping's held nadir height); the
/// bathy-coupled slope/footprint/incidence corrections (ADR-0006 D4/D9) are the
/// next phase. All geometry is reused verbatim from `projection.hpp` + the
/// accumulator — no new math here.
///
/// Per ADR-0006 D7 the stored value is the decoded magnitude (clamped to
/// `uint16`), not the live node's display-normalized value.
///
/// The splat policy is selectable with `--policy {mean,newest,maxhold}` (default
/// `mean`). `newest` reproduces the **live operator `draft` layer** (newest-valid
/// wins) offline from Tier-1, enabling a faithful draft-vs-`processed` compositing
/// comparison without replaying the bag (#195); `maxhold` keeps the brightest look.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

#include "geographic_msgs/msg/geo_point.hpp"
#include "marine_autonomy/gggs.h"
#include "marine_tiled_raster_store/tile_io.hpp"

#include "marine_sidescan_mosaic/accumulator.hpp"
#include "marine_sidescan_mosaic/projection.hpp"
#include "marine_sidescan_mosaic/tier1.hpp"

using namespace marine_sidescan_mosaic;  // NOLINT(build/namespaces) — local tool.

namespace
{
std::string argValue(int argc, char ** argv, const std::string & flag, const std::string & dflt)
{
  for (int i = 1; i + 1 < argc; ++i) {
    if (flag == argv[i]) {
      return argv[i + 1];
    }
  }
  return dflt;
}

int toInt(const std::string & s, const std::string & flag)
{
  try {
    return std::stoi(s);
  } catch (const std::exception &) {
    std::cerr << "error: expected an integer for " << flag << ", got '" << s << "'\n";
    std::exit(2);
  }
}

double toDouble(const std::string & s, const std::string & flag)
{
  try {
    return std::stod(s);
  } catch (const std::exception &) {
    std::cerr << "error: expected a number for " << flag << ", got '" << s << "'\n";
    std::exit(2);
  }
}

SplatPolicy parsePolicy(const std::string & s)
{
  if (s == "mean") {return SplatPolicy::Mean;}
  if (s == "newest") {return SplatPolicy::Newest;}
  if (s == "maxhold") {return SplatPolicy::MaxHold;}
  std::cerr << "error: --policy must be one of mean|newest|maxhold, got '" << s << "'\n";
  std::exit(2);
}
}  // namespace

int main(int argc, char ** argv)
{
  if (argc < 3) {
    std::cerr <<
      "usage: sidescan_tier2_flat <tier1.sst1> <out_dir>\n"
      "       [--level N] [--no-nadir-policy drop|assume_zero]\n"
      "       [--policy mean|newest|maxhold]   (mean=default; newest=live draft layer)\n"
      "       [--tx-beamwidth-fallback-rad R]  (along-track footprint when the ping\n"
      "                                         lacks tx_beamwidths; 0=point-deposit)\n";
    return 2;
  }
  const std::string tier1_path = argv[1];
  const std::string out_dir = argv[2];
  const int level_n = toInt(argValue(argc, argv, "--level", "13"), "--level");
  const std::string no_nadir = argValue(argc, argv, "--no-nadir-policy", "drop");
  const std::string policy_s = argValue(argc, argv, "--policy", "mean");
  const SplatPolicy policy = parsePolicy(policy_s);
  const double bw_fallback_raw = toDouble(
    argValue(argc, argv, "--tx-beamwidth-fallback-rad", "0.0"), "--tx-beamwidth-fallback-rad");
  // Same validation the live node applies to its fallback (shared helper): drop a
  // non-finite/negative value to 0 (point-deposit), warn on a degrees-for-radians slip.
  bool bw_fallback_suspicious = false;
  const double bw_fallback = sanitizeBeamwidthRad(bw_fallback_raw, &bw_fallback_suspicious);
  if (bw_fallback == 0.0 && bw_fallback_raw != 0.0) {
    std::cerr << "warning: --tx-beamwidth-fallback-rad " << bw_fallback_raw
              << " is not a finite non-negative radian value; ignoring it\n";
  } else if (bw_fallback_suspicious) {
    std::cerr << "warning: --tx-beamwidth-fallback-rad " << bw_fallback
              << " rad (~" << bw_fallback * 180.0 / M_PI << " deg) is implausibly wide for an "
              << "along-track beamwidth; did you pass degrees? expected radians (e.g. 0.00768)\n";
  }

  std::ifstream in(tier1_path, std::ios::binary);
  if (!in) {
    std::cerr << "error: cannot open " << tier1_path << "\n";
    return 1;
  }
  std::uint32_t found_version = 0;
  switch (checkTier1Header(in, &found_version)) {
    case Tier1HeaderStatus::Ok:
      break;
    case Tier1HeaderStatus::BadVersion:
      std::cerr << "error: " << tier1_path << " is a Tier-1 stream but version "
                << found_version << " (this build expects v" << kTier1Version
                << "); re-run the importer to regenerate it\n";
      return 1;
    case Tier1HeaderStatus::BadMagic:
      std::cerr << "error: " << tier1_path << " is not a Tier-1 stream\n";
      return 1;
  }

  const gggs::Level level(level_n);
  MosaicAccumulator acc(level, policy);

  Tier1Ping p;
  std::size_t n_in = 0, n_proj = 0, n_no_nadir = 0, n_placed = 0, n_bad_pose = 0;
  bool warned_wide_per_ping = false;   // throttle the degrees-slip warning to once.
  while (readTier1Ping(in, p)) {
    ++n_in;
    if (p.sample_rate <= 0.0) {
      continue;
    }
    double altitude = 0.0;
    if (p.nadir_altitude_m > 0.0F) {
      altitude = p.nadir_altitude_m;
    } else if (no_nadir != "assume_zero") {
      ++n_no_nadir;
      continue;
    }

    // Full attitude (#185 Stage 2): the per-channel frame's +Z encodes the look
    // side (port/stbd) plus static mounting tilt and dynamic roll, so the
    // across-track azimuth composes them — no Side / yaw-only heading needed.
    const GeoBeam gb = ecefPoseToGeoBeam(p.tx, p.ty, p.tz, p.qx, p.qy, p.qz, p.qw);
    if (!gb.valid) {
      ++n_bad_pose;   // degenerate quaternion: don't project the ping due north.
      continue;
    }
    geographic_msgs::msg::GeoPoint origin;
    origin.latitude = gb.latitude_deg;
    origin.longitude = gb.longitude_deg;
    origin.altitude = 0.0;   // projectSample precondition.
    const double azimuth = gb.azimuth_rad;
    // Along-track footprint splat (#208): per-sample beamwidth from Tier-1 v2, else
    // the CLI fallback (0 → point-deposit, unchanged). Run the stored width through
    // the shared validator too (non-finite/negative falls back; a degrees slip is
    // flagged once before the splat hard-caps it).
    bool per_ping_suspicious = false;
    const double per_ping_bw =
      sanitizeBeamwidthRad(static_cast<double>(p.tx_beamwidth_rad), &per_ping_suspicious);
    const double bw = per_ping_bw > 0.0 ? per_ping_bw : bw_fallback;
    if (per_ping_bw > 0.0 && per_ping_suspicious && !warned_wide_per_ping) {
      warned_wide_per_ping = true;
      std::cerr << "warning: stored per-ping tx_beamwidth " << per_ping_bw
                << " rad (~" << per_ping_bw * 180.0 / M_PI << " deg) is implausibly wide; "
                << "capping the splat (further such pings not warned)\n";
    }

    for (std::size_t j = 0; j < p.samples.size(); ++j) {
      const double slant = slantRange(static_cast<int>(j), p.sample0, p.sound_speed, p.sample_rate);
      const double ground = groundRange(slant, altitude);
      if (ground <= 0.0) {
        continue;   // inside the nadir cone.
      }
      const auto v = static_cast<std::uint16_t>(
        std::clamp(static_cast<double>(p.samples[j]), 0.0, 65535.0));
      const double footprint_m = footprintAlongTrack(slant, bw);
      splatAlongTrack(
        origin, gb.heading_rad, footprint_m, azimuth, ground, level,
        [&acc, v](const gggs::CellIndex & cell) {acc.add(cell, v);});
      ++n_placed;
    }
    ++n_proj;
  }

  std::size_t written = 0;
  try {
    written = marine_tiled_raster_store::saveTiles<std::uint16_t>(
      acc.tiles(), out_dir, {std::optional<std::uint16_t>(0)});
  } catch (const std::exception & e) {
    std::cerr << "saveTiles failed: " << e.what() << "\n";
    return 1;
  }

  std::cerr << "tier2: policy=" << policy_s << ", projected " << n_proj << "/" << n_in
            << " pings (" << n_placed << " samples placed; dropped no-nadir=" << n_no_nadir
            << " bad-pose=" << n_bad_pose << "), flushed " << written << " tiles to "
            << out_dir << "\n";
  return 0;
}
