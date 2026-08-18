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
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "geographic_msgs/msg/geo_point.hpp"
#include "marine_autonomy/gggs.h"
#include "marine_tiled_raster_store/tile_io.hpp"

#include "marine_backscatter/processed_accumulator.hpp"
#include "marine_backscatter/quality.hpp"
#include "marine_backscatter/registry.hpp"
#include "marine_sidescan_mosaic/bathy_dem.hpp"
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

/// @brief The value following @p flag, or @p dflt when the flag is absent.
///
/// A flag in the **last** argv slot has no value: exit 2 rather than silently
/// falling through to the default. `... --bathy-store` with the path lost to a
/// shell slip must not run a full flat-bottom build and exit 0 (#297 review).
std::string argValue(int argc, char ** argv, const std::string & flag, const std::string & dflt)
{
  for (int i = 1; i < argc; ++i) {
    if (flag == argv[i]) {
      if (i + 1 >= argc) {
        std::cerr << "error: " << flag << " requires a value, but none followed it\n";
        std::exit(2);
      }
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

/// @brief Minimal JSON string escape for the values written into the sidecar
///   (store paths and the layer list — no control characters expected).
std::string jsonEscape(const std::string & s)
{
  std::string out;
  out.reserve(s.size());
  for (const char c : s) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

/// @brief Record this run's projection mode next to `registry.json` (#297).
///
/// The mode cannot live in `registry.json` yet: `writeRegistry` is a fixed-shape
/// single-source writer in `marine_backscatter`, and widening it is a package API
/// change that belongs with #179's append-only registry merge. Until then the tool
/// writes its own sidecar, which every run (flat included) emits.
void writeProjectionSidecar(
  const std::string & out_dir, const std::string & mode,
  const std::string & bathy_store, const std::string & bathy_layers)
{
  const std::string path = (std::filesystem::path(out_dir) / "projection.json").string();
  std::ofstream out(path);
  if (!out) {
    std::cerr << "warning: could not write " << path
              << "; a later --accumulate run cannot check the projection mode\n";
    return;
  }
  out << "{\n"
      << "  \"version\": 1,\n"
      << "  \"projection_mode\": \"" << mode << "\",\n"
      << "  \"bathy_store\": \"" << jsonEscape(bathy_store) << "\",\n"
      << "  \"bathy_layers\": \"" << jsonEscape(bathy_layers) << "\"\n"
      << "}\n";
}

/// @brief The `projection_mode` recorded in @p out_dir's sidecar, or "" if there
///   is no sidecar (or it carries no parseable mode).
std::string readProjectionMode(const std::string & out_dir)
{
  const std::string path = (std::filesystem::path(out_dir) / "projection.json").string();
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    const auto key = line.find("\"projection_mode\"");
    if (key == std::string::npos) {
      continue;
    }
    const auto colon = line.find(':', key);
    if (colon == std::string::npos) {
      continue;
    }
    const auto open = line.find('"', colon);
    if (open == std::string::npos) {
      continue;
    }
    const auto close = line.find('"', open + 1);
    if (close == std::string::npos) {
      continue;
    }
    return line.substr(open + 1, close - open - 1);
  }
  return "";
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
      "                        # touched tile) instead of overwriting it\n"
      "       [--bathy-store PATH]        # DEM-orthorectify against a bathy store\n"
      "                                   # (omitted = flat-bottom, unchanged)\n"
      "       [--bathy-layers CSV]        # layer search order (default survey,reference)\n"
      "       [--min-dem-coverage FRAC]   # abort (exit 3) below this DEM hit share\n"
      "                                   # (default 0.5; 0 = explicit opt-in, warns)\n"
      "       [--datum-check-warn-m M]    # warn above this mean nadir-vs-DEM offset\n"
      "                                   # (default 1.0)\n"
      "       [--allow-mixed-projection]  # --accumulate across projection modes anyway\n";
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

  // DEM orthorectification (#297). Omitting --bathy-store keeps the unchanged
  // flat-bottom code path; ADR-0006 D6/D9 keep the live draft node and
  // sidescan_tier2_flat flat-bottom by design, so only this tool gains the option.
  const std::string bathy_store = argValue(argc, argv, "--bathy-store", "");
  const std::string bathy_layers = argValue(argc, argv, "--bathy-layers", kDefaultBathyLayers);
  const double min_dem_coverage =
    toDouble(argValue(argc, argv, "--min-dem-coverage", "0.5"), "--min-dem-coverage");
  const double datum_check_warn_m =
    toDouble(argValue(argc, argv, "--datum-check-warn-m", "1.0"), "--datum-check-warn-m");
  const bool allow_mixed_projection = hasFlag(argc, argv, "--allow-mixed-projection");
  const bool dem_mode = !bathy_store.empty();
  const std::string projection_mode = dem_mode ? "dem" : "flat";
  if (!(min_dem_coverage >= 0.0 && min_dem_coverage <= 1.0)) {
    std::cerr << "error: --min-dem-coverage must be a fraction in [0, 1]\n";
    return 2;
  }
  if (!(datum_check_warn_m > 0.0)) {
    std::cerr << "error: --datum-check-warn-m must be a positive number of metres\n";
    return 2;
  }

  // Projection-mode guard (#297; the ADR-0005 D8 "no silent provenance corruption"
  // rule applied to placement). --accumulate folds this run into the tiles already
  // on disk, and its only provenance guard is the source_id match below — which a
  // DEM-corrected re-run with the same --source-id passes. Compositing corrected
  // and mis-placed samples into the same cells is unrecoverable: the source band
  // records the same source either way. So refuse the mix, fail-fast before
  // decoding. A store with no sidecar predates mode recording and is therefore
  // flat-built (every store built before this flag existed is).
  if (accumulate && std::filesystem::exists(std::filesystem::path(out_dir) / "registry.json")) {
    const std::string recorded = readProjectionMode(out_dir);
    const std::string existing_mode = recorded.empty() ? "flat" : recorded;
    if (existing_mode != projection_mode) {
      std::cerr << (allow_mixed_projection ? "warning" : "error")
                << ": --accumulate: the store in " << out_dir << " was built with "
                << "projection_mode '" << existing_mode << "'"
                << (recorded.empty() ? " (no projection.json — a pre-#297 flat build)" : "")
                << ", but this run uses '" << projection_mode << "'.\n"
                << "  Compositing flat-bottom and DEM-orthorectified samples into the same\n"
                << "  cells is unrecoverable: per-cell provenance records only the source id,\n"
                << "  which is identical either way. Regenerate into a fresh out_dir instead\n"
                << "  of accumulating, or pass --allow-mixed-projection to accept the mix.\n";
      if (!allow_mixed_projection) {
        return 2;
      }
    }
  }

  // Provenance guard (#253 review; ADR-0005 D8 / #179). foldTile preserves each
  // existing cell's original source band, but writeRegistry() is a write-once
  // single-source writer — so accumulating a DIFFERENT source-id into a store
  // leaves tiles carrying mixed source indices while registry.json names only
  // this run's source, silently corrupting provenance. Until the registry is an
  // append-only merge (#179), refuse the mismatch rather than corrupt it. Fail
  // fast, before decoding.
  if (accumulate) {
    const std::string reg_path =
      (std::filesystem::path(out_dir) / "registry.json").string();
    std::ifstream reg(reg_path);
    std::string line;
    while (std::getline(reg, line)) {
      const auto key = line.find("\"source_id\"");
      if (key == std::string::npos) {
        continue;
      }
      const auto colon = line.find(':', key);
      if (colon == std::string::npos) {
        continue;
      }
      const int existing_sid = std::atoi(line.c_str() + colon + 1);
      if (existing_sid > 0 && existing_sid != source_id_arg) {
        std::cerr << "error: --accumulate: existing store registry " << reg_path
                  << " has source_id " << existing_sid << ", but this run uses "
                  << source_id_arg << ".\n"
                  << "  A multi-source registry merge is not yet implemented "
                  << "(ADR-0005 D8 / #179); re-run with --source-id " << existing_sid
                  << " or use a fresh out_dir to avoid corrupting provenance.\n";
        return 2;
      }
      break;   // registry v1 is single-source: first source_id is authoritative.
    }
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

  // Hard-fail on an unusable bathy store (absent/renamed layer dirs, zero tiles)
  // rather than degrade every sample to flat while exiting 0.
  std::unique_ptr<BathyDem> dem;
  if (dem_mode) {
    try {
      dem = std::make_unique<BathyDem>(bathy_store, splitCsv(bathy_layers));
    } catch (const std::exception & e) {
      std::cerr << "error: " << e.what() << "\n";
      return 1;
    }
    std::cerr << "bathy DEM: " << dem->describe() << "\n";
  }

  const gggs::Level level(level_n);
  ProcessedAccumulator acc(level);

  Tier1Ping p;
  std::size_t n_in = 0, n_proj = 0, n_no_nadir = 0, n_placed = 0, n_bad_pose = 0;
  std::size_t n_dem_hit = 0, n_dem_no_coverage = 0, n_dem_degenerate = 0,
    n_dem_nonconverged = 0;
  // Datum cross-check: nadir_altitude_m (height above bottom, from the altimeter)
  // vs sensor ellipsoidal height − DEM height at the nadir point. Same physical
  // quantity by two independent paths, so a persistent offset means a datum
  // mismatch (orthometric store, lever-arm error, unexpected tide frame).
  std::size_t n_datum_check = 0;
  double datum_sum = 0.0, datum_sq_sum = 0.0;
  double bbox_min_lat = std::numeric_limits<double>::infinity();
  double bbox_max_lat = -std::numeric_limits<double>::infinity();
  double bbox_min_lon = std::numeric_limits<double>::infinity();
  double bbox_max_lon = -std::numeric_limits<double>::infinity();
  bool warned_wide_per_ping = false;   // throttle the degrees-slip warning to once.
  // A DEM lookup throws only when a tile the startup scan listed cannot be read —
  // a corrupt/truncated store. Abort the run (nothing has been written yet) rather
  // than silently finish with the rest of the samples placed flat.
  try {
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
      bbox_min_lat = std::min(bbox_min_lat, gb.latitude_deg);
      bbox_max_lat = std::max(bbox_max_lat, gb.latitude_deg);
      bbox_min_lon = std::min(bbox_min_lon, gb.longitude_deg);
      bbox_max_lon = std::max(bbox_max_lon, gb.longitude_deg);

      // Datum cross-check (see the counters above): only meaningful where the
      // altimeter actually returned, so it never runs under assume_zero.
      if (dem && p.nadir_altitude_m > 0.0F) {
        const auto nadir_bottom = dem->depthAt(gb.latitude_deg, gb.longitude_deg);
        if (nadir_bottom) {
          const double implied = gb.altitude_m - *nadir_bottom;
          const double discrepancy = static_cast<double>(p.nadir_altitude_m) - implied;
          ++n_datum_check;
          datum_sum += discrepancy;
          datum_sq_sum += discrepancy * discrepancy;
        }
      }

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
        const double slant =
          slantRange(static_cast<int>(j), p.sample0, p.sound_speed, p.sample_rate);
        const double flat_ground = groundRange(slant, altitude);
        if (flat_ground <= 0.0) {
          continue;   // nadir cone.
        }
        // DEM orthorectification (#297). The flat range stays the nadir-cone gate
        // and the iteration seed; on any degraded status the flat pair is used
        // unchanged, so the no-DEM path is bit-for-bit what it was.
        double ground = flat_ground;
        double vertical = altitude;
        if (dem) {
          const DemCorrection corrected = correctedGroundRange(
            slant, gb.altitude_m, origin, azimuth, flat_ground,
            [&dem](double lat, double lon) {return dem->depthAt(lat, lon);});
          switch (corrected.status) {
            case DemCorrection::Status::kConverged:
              ++n_dem_hit;
              ground = corrected.ground_range;
              vertical = corrected.vertical_offset;
              break;
            case DemCorrection::Status::kNoCoverage:
              ++n_dem_no_coverage;
              break;
            case DemCorrection::Status::kDegenerate:
              ++n_dem_degenerate;
              break;
            case DemCorrection::Status::kNotConverged:
              ++n_dem_nonconverged;
              break;
          }
        }
        const auto intensity =
          static_cast<std::uint16_t>(std::clamp(static_cast<double>(p.samples[j]), 0.0, 65535.0));
        // grazingQuality derives the grazing angle from the (vertical, horizontal)
        // pair, so the DEM-corrected pair improves the best-source score with no
        // marine_backscatter API change. (Seabed-NORMAL incidence — ADR-0006 D4 —
        // stays out of scope; it belongs with the GeoCoder radiometry phase.)
        const std::uint16_t quality = grazingQuality(vertical, ground);
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
  } catch (const std::exception & e) {
    std::cerr << "error: bathy DEM lookup failed: " << e.what() << "\n"
              << "  no tiles and no registry were written.\n";
    return 1;
  }

  // DEM coverage gate (#297). The startup hard-fail proves the store exists and
  // holds tiles; it cannot prove the store OVERLAPS this survey. A valid store for
  // another lake would yield n_dem_hit == 0 while every sample took the flat
  // fallback and the tool exited 0 with a normal-looking summary. Checked here,
  // BEFORE saveTiles/writeRegistry, so a failure writes nothing.
  if (dem) {
    const std::size_t denominator = n_dem_hit + n_dem_no_coverage;
    const double coverage =
      denominator == 0 ? 0.0 : static_cast<double>(n_dem_hit) / static_cast<double>(denominator);
    const bool below_gate = coverage < min_dem_coverage;
    if (below_gate || coverage < 0.5) {
      std::string gate_note;
      if (below_gate) {
        gate_note = " is below --min-dem-coverage " + std::to_string(min_dem_coverage);
      }
      std::cerr << (below_gate ? "error" : "warning") << ": DEM coverage " << coverage
                << " (" << n_dem_hit << " of " << denominator << " samples that reached the "
                << "lookup)" << gate_note << "\n"
                << "  bathy store: " << dem->describe() << "\n"
                << "  layer search order: " << bathy_layers << "\n"
                << "  counters: hit=" << n_dem_hit << " no-coverage=" << n_dem_no_coverage
                << " degenerate=" << n_dem_degenerate
                << " non-converged=" << n_dem_nonconverged << "\n"
                << "  survey bounds: lat [" << bbox_min_lat << ", " << bbox_max_lat
                << "] lon [" << bbox_min_lon << ", " << bbox_max_lon << "]\n";
      if (below_gate) {
        std::cerr << "  refusing to write: the output would be a flat-bottom store wearing a\n"
                  << "  DEM run's name. Check --bathy-store / --bathy-layers, or pass\n"
                  << "  --min-dem-coverage 0 to accept a deliberately partial run.\n";
        return 3;
      }
    }
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
        // Never destroy prior coverage on a read hiccup. If we cannot reload an
        // existing tile, saving would OVERWRITE it with this run's partial
        // composite and silently drop its accumulated coverage — so abort before
        // any tile is written. (Nothing has been saved yet at this point.)
        std::cerr << "error: --accumulate could not reload existing tile " << path
                  << ": " << e.what() << "\n"
                  << "  refusing to continue: saving now would OVERWRITE this tile and lose its\n"
                  << "  prior coverage. Inspect/remove the tile, or re-run WITHOUT --accumulate\n"
                  << "  to intentionally overwrite. No tiles were written.\n";
        return 1;
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
  // Every run records its projection mode, flat included — a later --accumulate
  // reads it to refuse mixing modes (#297).
  writeProjectionSidecar(out_dir, projection_mode, bathy_store, dem_mode ? bathy_layers : "");

  std::cerr << "tier2-processed: projected " << n_proj << "/" << n_in << " pings ("
            << n_placed << " samples; dropped no-nadir=" << n_no_nadir
            << " bad-pose=" << n_bad_pose << "), best-source into "
            << written << " 3-band tiles + registry.json in " << out_dir
            << " [projection=" << projection_mode << "]\n";
  if (dem) {
    std::cerr << "tier2-processed: dem hit=" << n_dem_hit
              << " no-coverage=" << n_dem_no_coverage
              << " degenerate=" << n_dem_degenerate
              << " non-converged=" << n_dem_nonconverged << "; per-layer hits";
    for (const auto & kv : dem->hitsByLayer()) {
      std::cerr << " " << kv.first << "=" << kv.second;
    }
    std::cerr << "\n";
    if (n_datum_check > 0) {
      const double n = static_cast<double>(n_datum_check);
      const double mean = datum_sum / n;
      const double rms = std::sqrt(datum_sq_sum / n);
      std::cerr << "tier2-processed: datum cross-check (nadir altimeter vs sensor height "
                << "- DEM) over " << n_datum_check << " pings: mean " << mean
                << " m, rms " << rms << " m\n";
      if (std::abs(mean) > datum_check_warn_m) {
        std::cerr << "warning: mean datum discrepancy " << mean << " m exceeds "
                  << datum_check_warn_m << " m. The altimeter's height above bottom and the "
                  << "sensor-height-minus-DEM height are the same quantity by two independent "
                  << "paths, so a persistent offset means a datum mismatch (an orthometric "
                  << "bathy store, a lever-arm error, or an unexpected tide frame) — the "
                  << "samples may be confidently mis-placed, not merely uncorrected.\n";
      }
    } else {
      std::cerr << "warning: datum cross-check had no usable ping (no altimeter return with "
                << "DEM coverage), so the vertical datum agreement is unverified\n";
    }
  }
  return 0;
}
