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
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "geographic_msgs/msg/geo_point.hpp"
#include "marine_autonomy/gggs.h"
#include "marine_tiled_raster_store/tile_io.hpp"

#include "marine_backscatter/processed_accumulator.hpp"
#include "marine_backscatter/quality.hpp"
#include "marine_backscatter/registry.hpp"
#include "marine_sidescan_mosaic/projection.hpp"
#include "marine_sidescan_mosaic/tier1.hpp"

using namespace marine_backscatter;       // NOLINT(build/namespaces) — shared engine.
using namespace marine_sidescan_mosaic;   // NOLINT(build/namespaces) — local tool.

namespace
{
// Presence test for a valueless boolean flag (e.g. --accumulate).
bool hasFlag(int argc, char ** argv, const std::string & flag)
{
  for (int i = 1; i < argc; ++i) {
    if (flag == argv[i]) {
      return true;
    }
  }
  return false;
}

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

}  // namespace

int main(int argc, char ** argv)
{
  if (argc < 3) {
    std::cerr <<
      "usage: sidescan_tier2_processed <tier1.sst1> <out_dir>\n"
      "       [--level N] [--no-nadir-policy drop|assume_zero]\n"
      "       [--tx-beamwidth-fallback-rad R]   (along-track footprint when the ping\n"
      "                                          lacks tx_beamwidths; 0=point-deposit)\n"
      "       [--source-id N] [--platform P] [--sensor S] [--sensor-class C] [--campaign X]\n"
      "       [--accumulate]   # composite INTO an existing store (reload+fold each\n"
      "                        # touched tile) instead of overwriting it\n";
    return 2;
  }
  const std::string tier1_path = argv[1];
  const std::string out_dir = argv[2];
  const int level_n = toInt(argValue(argc, argv, "--level", "13"), "--level");
  const std::string no_nadir = argValue(argc, argv, "--no-nadir-policy", "drop");
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
  const int source_id_arg = toInt(argValue(argc, argv, "--source-id", "1"), "--source-id");
  if (source_id_arg < 1 || source_id_arg > 65535) {
    std::cerr << "error: --source-id must be in [1, 65535] (the per-cell band is the uint16 "
                 "local index; the wide global id lives in the registry, ADR-0005 D4)\n";
    return 2;
  }
  const auto source_id = static_cast<std::uint16_t>(source_id_arg);
  if (no_nadir == "assume_zero") {
    std::cerr << "warning: --no-nadir-policy assume_zero collapses grazing (altitude 0 -> "
                 "grazing 0), so quality floors and best-source degenerates to first-touch; "
                 "prefer 'drop' for the processed layer.\n";
  }
  const std::string platform = argValue(argc, argv, "--platform", "bizzyboat");
  const std::string sensor = argValue(argc, argv, "--sensor", "garmin-gcv20");
  const std::string sensor_class = argValue(argc, argv, "--sensor-class", "sidescan");
  const std::string campaign = argValue(argc, argv, "--campaign", "unknown");
  const bool accumulate = hasFlag(argc, argv, "--accumulate");

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
  ProcessedAccumulator acc(level);

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

    const GeoBeam gb = ecefPoseToGeoBeam(p.tx, p.ty, p.tz, p.qx, p.qy, p.qz, p.qw);
    if (!gb.valid) {
      ++n_bad_pose;   // degenerate quaternion: don't project the ping due north.
      continue;
    }
    geographic_msgs::msg::GeoPoint origin;
    origin.latitude = gb.latitude_deg;
    origin.longitude = gb.longitude_deg;
    origin.altitude = 0.0;
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
        continue;   // nadir cone.
      }
      const auto intensity =
        static_cast<std::uint16_t>(std::clamp(static_cast<double>(p.samples[j]), 0.0, 65535.0));
      const std::uint16_t quality = grazingQuality(altitude, ground);
      const double footprint_m = footprintAlongTrack(slant, bw);
      splatAlongTrack(
        origin, gb.heading_rad, footprint_m, azimuth, ground, level,
        [&acc, intensity, quality, source_id](const gggs::CellIndex & cell) {
          acc.add(cell, intensity, quality, source_id);
        });
      ++n_placed;
    }
    ++n_proj;
  }

  // Accumulate into an existing store: reload each tile this run touched and fold
  // it back in (best-source) before saving, so bag-by-bag ingestion composites
  // instead of overwriting (issue #253) — bounded RAM/disk vs one whole-campaign
  // pass. Without --accumulate, saveTiles overwrites (the prior behavior).
  if (accumulate) {
    std::vector<gggs::GridIndex> grids;
    grids.reserve(acc.tiles().size());
    for (const auto & kv : acc.tiles()) {
      grids.push_back(kv.first);
    }
    std::size_t folded = 0;
    for (const auto & grid : grids) {
      const std::string path =
        (std::filesystem::path(out_dir) /
        marine_tiled_raster_store::tileFilename(grid)).string();
      if (!std::filesystem::exists(path)) {
        continue;   // new coverage — nothing on disk to merge.
      }
      try {
        const auto existing = marine_tiled_raster_store::loadTile<std::uint16_t>(
          path, level, ProcessedAccumulator::kBands);
        acc.foldTile(existing);
        ++folded;
      } catch (const std::exception & e) {
        std::cerr << "warning: --accumulate could not reload " << path << ": "
                  << e.what() << " -- this tile will be OVERWRITTEN, not merged\n";
      }
    }
    std::cerr << "accumulate: folded " << folded << " existing tile(s) from " << out_dir << "\n";
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
            << n_placed << " samples; dropped no-nadir=" << n_no_nadir
            << " bad-pose=" << n_bad_pose << "), best-source into "
            << written << " 3-band tiles + registry.json in " << out_dir << "\n";
  return 0;
}
