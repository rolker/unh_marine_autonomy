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

#include <algorithm>
#include <cstdint>
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
}  // namespace

int main(int argc, char ** argv)
{
  if (argc < 3) {
    std::cerr <<
      "usage: sidescan_tier2_flat <tier1.sst1> <out_dir>\n"
      "       [--level N] [--no-nadir-policy drop|assume_zero]\n";
    return 2;
  }
  const std::string tier1_path = argv[1];
  const std::string out_dir = argv[2];
  const int level_n = std::stoi(argValue(argc, argv, "--level", "13"));
  const std::string no_nadir = argValue(argc, argv, "--no-nadir-policy", "drop");

  std::ifstream in(tier1_path, std::ios::binary);
  if (!in) {
    std::cerr << "error: cannot open " << tier1_path << "\n";
    return 1;
  }
  if (!readTier1Header(in)) {
    std::cerr << "error: " << tier1_path << " is not a Tier-1 stream\n";
    return 1;
  }

  const gggs::Level level(level_n);
  MosaicAccumulator acc(level, SplatPolicy::Mean);

  Tier1Ping p;
  std::size_t n_in = 0, n_proj = 0, n_no_nadir = 0, n_placed = 0;
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

    const GeoHeading gh = ecefPoseToGeoHeading(p.tx, p.ty, p.tz, p.qx, p.qy, p.qz, p.qw);
    geographic_msgs::msg::GeoPoint origin;
    origin.latitude = gh.latitude_deg;
    origin.longitude = gh.longitude_deg;
    origin.altitude = 0.0;   // projectSample precondition.
    const Side side = (p.channel == Tier1Channel::Port) ? Side::Port : Side::Starboard;
    const double azimuth = acrossTrackAzimuth(gh.heading_rad, side);

    for (std::size_t j = 0; j < p.samples.size(); ++j) {
      const double slant = slantRange(static_cast<int>(j), p.sample0, p.sound_speed, p.sample_rate);
      const double ground = groundRange(slant, altitude);
      if (ground <= 0.0) {
        continue;   // inside the nadir cone.
      }
      const double v = std::clamp(static_cast<double>(p.samples[j]), 0.0, 65535.0);
      acc.add(projectSample(origin, azimuth, ground, level), static_cast<std::uint16_t>(v));
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

  std::cerr << "tier2: projected " << n_proj << "/" << n_in << " pings ("
            << n_placed << " samples placed; no-nadir dropped " << n_no_nadir
            << "), flushed " << written << " tiles to " << out_dir << "\n";
  return 0;
}
