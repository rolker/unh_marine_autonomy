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
/// @brief Durable `processed` Tier-2 build (ADR-0006 D4/D5/D7, issue #184).
///
/// Reads a Tier-1 `.sst1` archive and composites a **best-source** mosaic: per
/// GGGS cell it keeps the highest-quality look's `{intensity, quality,
/// source-id}` (3-band `uint16` GeoTIFF tiles), and writes a `registry.json`
/// sidecar resolving the per-cell source index to its provenance (ADR-0005). This
/// is the durable layer the live draft is eventually promoted into.
///
/// v1 quality is a **flat-bottom grazing-angle score** peaking mid-swath
/// (`sin(2·grazing)`) — nadir and far-range are low, so a neighbouring line's
/// mid-swath look wins and fills the nadir gap automatically (ADR-0006 D5). The
/// full GeoCoder radiometry (beam pattern, slope, EGN) is a later phase; this
/// establishes the compositing + provenance contract.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "geographic_msgs/msg/geo_point.hpp"
#include "marine_autonomy/gggs.h"
#include "marine_tiled_raster_store/tile_io.hpp"

#include "marine_sidescan_mosaic/processed_accumulator.hpp"
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

// Mid-swath grazing-angle quality in [1, 65535]; peaks at 45 deg, ~0 at nadir/far.
std::uint16_t gradingQuality(double altitude, double ground_range)
{
  const double grazing = std::atan2(altitude, ground_range);   // rad, 90deg at nadir.
  const double q = std::sin(2.0 * grazing);                    // peak at 45 deg.
  const double scaled = std::clamp(q, 0.0, 1.0) * 65535.0;
  return static_cast<std::uint16_t>(std::max(1.0, scaled));    // floor 1: real return.
}

void writeRegistry(
  const std::string & path, std::uint16_t source_id, const std::string & platform,
  const std::string & sensor, const std::string & sensor_class, const std::string & campaign)
{
  std::ofstream r(path);
  // ADR-0005: per-cell band is the compact LOCAL index; the registry resolves it
  // to the (eventually origin-namespaced, ADR-0005 D4) global source-id + record.
  r << "{\n  \"version\": 1,\n  \"sources\": {\n    \"" << source_id << "\": {\n"
    << "      \"source_id\": " << source_id << ",\n"
    << "      \"platform\": \"" << platform << "\",\n"
    << "      \"sensor\": \"" << sensor << "\",\n"
    << "      \"sensor_class\": \"" << sensor_class << "\",\n"
    << "      \"campaign\": \"" << campaign << "\"\n"
    << "    }\n  }\n}\n";
}
}  // namespace

int main(int argc, char ** argv)
{
  if (argc < 3) {
    std::cerr <<
      "usage: sidescan_tier2_processed <tier1.sst1> <out_dir>\n"
      "       [--level N] [--no-nadir-policy drop|assume_zero]\n"
      "       [--source-id N] [--platform P] [--sensor S] [--sensor-class C] [--campaign X]\n";
    return 2;
  }
  const std::string tier1_path = argv[1];
  const std::string out_dir = argv[2];
  const int level_n = std::stoi(argValue(argc, argv, "--level", "13"));
  const std::string no_nadir = argValue(argc, argv, "--no-nadir-policy", "drop");
  const auto source_id = static_cast<std::uint16_t>(std::stoi(argValue(argc, argv, "--source-id", "1")));
  const std::string platform = argValue(argc, argv, "--platform", "bizzyboat");
  const std::string sensor = argValue(argc, argv, "--sensor", "garmin-gcv20");
  const std::string sensor_class = argValue(argc, argv, "--sensor-class", "sidescan");
  const std::string campaign = argValue(argc, argv, "--campaign", "unknown");

  std::ifstream in(tier1_path, std::ios::binary);
  if (!in || !readTier1Header(in)) {
    std::cerr << "error: " << tier1_path << " is not a readable Tier-1 stream\n";
    return 1;
  }

  const gggs::Level level(level_n);
  ProcessedAccumulator acc(level);

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

    const GeoBeam gb = ecefPoseToGeoBeam(p.tx, p.ty, p.tz, p.qx, p.qy, p.qz, p.qw);
    geographic_msgs::msg::GeoPoint origin;
    origin.latitude = gb.latitude_deg;
    origin.longitude = gb.longitude_deg;
    origin.altitude = 0.0;
    const double azimuth = gb.azimuth_rad;

    for (std::size_t j = 0; j < p.samples.size(); ++j) {
      const double slant = slantRange(static_cast<int>(j), p.sample0, p.sound_speed, p.sample_rate);
      const double ground = groundRange(slant, altitude);
      if (ground <= 0.0) {
        continue;   // nadir cone.
      }
      const auto intensity =
        static_cast<std::uint16_t>(std::clamp(static_cast<double>(p.samples[j]), 0.0, 65535.0));
      const std::uint16_t quality = gradingQuality(altitude, ground);
      acc.add(projectSample(origin, azimuth, ground, level), intensity, quality, source_id);
      ++n_placed;
    }
    ++n_proj;
  }

  std::size_t written = 0;
  try {
    const std::vector<std::optional<std::uint16_t>> nodata(
      ProcessedAccumulator::kBands, std::optional<std::uint16_t>(0));
    written = marine_tiled_raster_store::saveTiles<std::uint16_t>(acc.tiles(), out_dir, nodata);
  } catch (const std::exception & e) {
    std::cerr << "saveTiles failed: " << e.what() << "\n";
    return 1;
  }
  writeRegistry(out_dir + "/registry.json", source_id, platform, sensor, sensor_class, campaign);

  std::cerr << "tier2-processed: projected " << n_proj << "/" << n_in << " pings ("
            << n_placed << " samples; no-nadir dropped " << n_no_nadir << "), best-source into "
            << written << " 3-band tiles + registry.json in " << out_dir << "\n";
  return 0;
}
