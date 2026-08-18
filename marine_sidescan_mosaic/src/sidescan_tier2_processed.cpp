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
#include <system_error>
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
/// @brief Sticky `projection_mode` for a store an operator deliberately mixed
///   (`--accumulate --allow-mixed-projection` across modes). It is never
///   downgraded back to `flat`/`dem`: the store holds samples placed both ways
///   for good, and a later run must be told so rather than reading a pure mode.
constexpr char kMixedMode[] = "mixed";

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
///
/// A flag followed by **another flag** is the same slip one slot along
/// (`--campaign --platform bizzy` would otherwise record the campaign as
/// `"--platform"` in `registry.json`), so a value beginning with `--` is refused
/// too. Single-dash tokens are left alone: those are negative numbers.
std::string argValue(int argc, char ** argv, const std::string & flag, const std::string & dflt)
{
  for (int i = 1; i < argc; ++i) {
    if (flag == argv[i]) {
      if (i + 1 >= argc) {
        std::cerr << "error: " << flag << " requires a value, but none followed it\n";
        std::exit(2);
      }
      const std::string value = argv[i + 1];
      if (value.rfind("--", 0) == 0) {
        std::cerr << "error: " << flag << " requires a value, but the next argument is "
                  << "another flag ('" << value << "').\n"
                  << "  A value beginning with '--' is not accepted: it is almost always a\n"
                  << "  dropped argument, and taking it literally would write the flag name\n"
                  << "  into the store's provenance.\n";
        std::exit(2);
      }
      return value;
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

/// @brief JSON string escape for the values written into the sidecar (store paths
///   and the layer list).
///
/// Control characters are escaped too, not merely unexpected: a path holding a
/// newline would otherwise emit invalid JSON *and* let an injected line be read
/// back as a `projection_mode` by the line-based reader.
std::string jsonEscape(const std::string & s)
{
  static const char * const kHex = "0123456789abcdef";
  std::string out;
  out.reserve(s.size());
  for (const char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out += "\\u00";
          out.push_back(kHex[(static_cast<unsigned char>(c) >> 4) & 0xF]);
          out.push_back(kHex[static_cast<unsigned char>(c) & 0xF]);
        } else {
          out.push_back(c);
        }
        break;
    }
  }
  return out;
}

/// @brief Record this run's projection mode next to `registry.json` (#297).
///
/// The mode cannot live in `registry.json` yet: `writeRegistry` is a fixed-shape
/// single-source writer in `marine_backscatter`, and widening it is a package API
/// change that belongs with #179's append-only registry merge. Until then the tool
/// writes its own sidecar, which every run (flat included) emits.
/// @return true when the sidecar is on disk complete; false on any I/O failure
///   (open, write, or the flush that close() performs — a full filesystem fails
///   only there, so the stream state is checked after the close, not before).
bool writeProjectionSidecar(
  const std::string & out_dir, const std::string & mode,
  const std::string & bathy_store, const std::string & bathy_layers)
{
  const std::string path = (std::filesystem::path(out_dir) / "projection.json").string();
  std::ofstream out(path);
  if (!out) {
    return false;
  }
  out << "{\n"
      << "  \"version\": 1,\n"
      << "  \"projection_mode\": \"" << mode << "\",\n"
      << "  \"bathy_store\": \"" << jsonEscape(bathy_store) << "\",\n"
      << "  \"bathy_layers\": \"" << jsonEscape(bathy_layers) << "\"\n"
      << "}\n";
  out.close();
  return !out.fail();
}

/// @brief What @p out_dir's sidecar says about how its store was projected.
///
/// `kMissing` (no sidecar at all) is a **pre-#297 store**, which is flat-built by
/// construction. `kUnreadable` (present but unopenable, truncated, or carrying no
/// parseable `projection_mode`) is an unknown mode and must never be silently
/// read as flat — a DEM store whose sidecar was truncated would otherwise accept
/// a flat `--accumulate`.
enum class SidecarState { kMissing, kUnreadable, kOk };

struct ProjectionSidecar
{
  SidecarState state = SidecarState::kMissing;
  std::string mode;   ///< only meaningful when `state == kOk`.
};

ProjectionSidecar readProjectionMode(const std::string & out_dir)
{
  const std::string path = (std::filesystem::path(out_dir) / "projection.json").string();
  if (!std::filesystem::exists(path)) {
    return {SidecarState::kMissing, ""};
  }
  std::ifstream in(path);
  if (!in) {
    return {SidecarState::kUnreadable, ""};
  }
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
    const std::string mode = line.substr(open + 1, close - open - 1);
    if (mode.empty()) {
      break;   // present but empty: an unknown mode, not a flat store.
    }
    return {SidecarState::kOk, mode};
  }
  // Ran off the end (or hit a read error) without a parseable mode: truncated or
  // corrupt, never "flat".
  return {SidecarState::kUnreadable, ""};
}

/// @brief Does @p out_dir already hold value tiles from an earlier build?
///
/// The provenance guards must key on the **tiles**, not on `registry.json` alone:
/// the fold loop reads tile files, so a store whose registry never landed (an
/// interrupted or failed run) still has coverage that a later run would composite
/// into or overwrite (#297 round-2 review). Any read problem answers "no tiles" —
/// the caller's other checks still apply, and a directory that cannot be listed
/// fails loudly at `saveTiles` anyway.
bool hasTileFiles(const std::string & out_dir)
{
  std::error_code ec;
  if (!std::filesystem::is_directory(out_dir, ec) || ec) {
    return false;
  }
  std::filesystem::directory_iterator it(out_dir, ec);
  if (ec) {
    return false;
  }
  for (const auto & entry : it) {
    if (entry.is_regular_file(ec) && !ec && entry.path().extension() == ".tif") {
      return true;
    }
  }
  return false;
}

/// @brief Delete a prior build's tiles and provenance files from @p out_dir.
///
/// Only the files this tool writes are removed — `<...>.tif` value tiles,
/// `registry.json`, `projection.json`. Anything else in the directory (a derived
/// `overviews/` sidecar, operator notes) is left alone for its owner to rebuild.
/// @return the number of files removed, or `nullopt` with @p error set on the
///   first failure (a partially cleared store must not then be written into).
std::optional<std::size_t> removePriorStore(const std::string & out_dir, std::string * error)
{
  std::error_code ec;
  std::vector<std::filesystem::path> victims;
  std::filesystem::directory_iterator it(out_dir, ec);
  if (ec) {
    *error = "cannot list " + out_dir + ": " + ec.message();
    return std::nullopt;
  }
  for (const auto & entry : it) {
    if (!entry.is_regular_file(ec) || ec) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (entry.path().extension() == ".tif" || name == "registry.json" ||
      name == "projection.json")
    {
      victims.push_back(entry.path());
    }
  }
  std::size_t removed = 0;
  for (const auto & victim : victims) {
    if (!std::filesystem::remove(victim, ec) || ec) {
      *error = "cannot remove " + victim.string() + ": " + ec.message();
      return std::nullopt;
    }
    ++removed;
  }
  return removed;
}

/// @brief The tool body. `main` wraps it in a last-resort handler (below): the DEM
///   call sites catch their own faults, but `gggs::Level::gridIndex`'s
///   `std::out_of_range` and the throwing `std::filesystem` overloads used around
///   the store can still escape, and `std::terminate` with no diagnostic is not an
///   acceptable ending for a store writer (#297 review).
int runTool(int argc, char ** argv)
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
      "       [--overwrite]    # a populated out_dir is refused without one of these:\n"
      "                        # delete the previous build's tiles/registry/sidecar first\n"
      "       [--bathy-store PATH]        # DEM-orthorectify against a bathy store\n"
      "                                   # (omitted = flat-bottom, unchanged)\n"
      "       [--bathy-layers CSV]        # layer search order (default survey,reference)\n"
      "       [--min-dem-coverage FRAC]   # abort (exit 3) below this DEM hit share\n"
      "                                   # (default 0.5; 0 = explicit opt-in, warns)\n"
      "       [--datum-check-warn-m M]    # warn above this mean nadir-vs-DEM offset\n"
      "                                   # (default 1.0)\n"
      "       [--bathy-cache-tiles N]     # resident bathy tiles (default 8, ~14.7 MB each)\n"
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
  const bool overwrite = hasFlag(argc, argv, "--overwrite");
  if (accumulate && overwrite) {
    std::cerr << "error: --accumulate and --overwrite are opposites: one composites into "
              << "the existing store, the other deletes it first. Pass at most one.\n";
    return 2;
  }

  // DEM orthorectification (#297). Omitting --bathy-store keeps the unchanged
  // flat-bottom code path; ADR-0006 D6/D9 keep the live draft node and
  // sidescan_tier2_flat flat-bottom by design, so only this tool gains the option.
  const std::string bathy_store = argValue(argc, argv, "--bathy-store", "");
  const std::string bathy_layers = argValue(argc, argv, "--bathy-layers", kDefaultBathyLayers);
  const double min_dem_coverage =
    toDouble(argValue(argc, argv, "--min-dem-coverage", "0.5"), "--min-dem-coverage");
  const double datum_check_warn_m =
    toDouble(argValue(argc, argv, "--datum-check-warn-m", "1.0"), "--datum-check-warn-m");
  const int bathy_cache_tiles = toInt(
    argValue(
      argc, argv, "--bathy-cache-tiles",
      std::to_string(BathyDem::kDefaultCacheTiles)), "--bathy-cache-tiles");
  if (bathy_cache_tiles < 1) {
    std::cerr << "error: --bathy-cache-tiles must be at least 1\n";
    return 2;
  }
  const bool allow_mixed_projection = hasFlag(argc, argv, "--allow-mixed-projection");
  const bool dem_mode = !bathy_store.empty();
  const std::string projection_mode = dem_mode ? "dem" : "flat";
  // What the sidecar will record. It is this run's mode unless the run
  // deliberately mixes modes into an existing store, which makes the store
  // permanently `mixed` (set in the guard below) — see kMixedMode.
  std::string sidecar_mode = projection_mode;
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
  //
  // "Already a store" is decided by the TILES (plus either provenance file), not by
  // registry.json alone: a run interrupted between the tile write and the registry
  // write leaves coverage on disk that a later --accumulate would otherwise fold
  // into with both guards skipped, and then re-stamp with a pure mode (#297 round-2
  // review).
  const std::filesystem::path registry_file = std::filesystem::path(out_dir) / "registry.json";
  const std::filesystem::path sidecar_file = std::filesystem::path(out_dir) / "projection.json";
  const bool out_dir_has_tiles = hasTileFiles(out_dir);
  const bool out_dir_has_registry = std::filesystem::exists(registry_file);
  const bool out_dir_has_sidecar = std::filesystem::exists(sidecar_file);
  const bool out_dir_populated =
    out_dir_has_tiles || out_dir_has_registry || out_dir_has_sidecar;

  // The same protection for a re-run WITHOUT --accumulate. saveTiles overwrites
  // only the tiles this run touches, so a plain re-run into a populated directory
  // leaves the previous build's untouched tiles in place — a store that is
  // materially mixed (this run's placement in some cells, the previous run's in
  // others) while the sidecar is re-stamped with this run's pure mode, at exit 0
  // and with no flag. Refuse, and make clearing the prior build explicit (#297
  // round-2 review).
  if (!accumulate && overwrite && !out_dir_populated) {
    std::cerr << "warning: --overwrite: " << out_dir << " holds no previous build; "
              << "nothing to clear.\n";
  }
  if (!accumulate && out_dir_populated && !overwrite) {
    std::cerr << "error: " << out_dir << " already holds a build (tiles/registry/"
              << "projection.json) and this run does not pass --accumulate.\n"
              << "  Writing into it would leave the previous build's untouched tiles beside\n"
              << "  this run's, so the store would hold samples placed two different ways\n"
              << "  while projection.json recorded a single pure mode. Per-cell provenance\n"
              << "  cannot tell them apart afterwards (ADR-0005 D2/D6). Choose one:\n"
              << "    --accumulate   composite into the existing store (mode-guarded), or\n"
              << "    --overwrite    delete the prior tiles + registry + sidecar first, or\n"
              << "    a fresh out_dir.\n";
    return 2;
  }
  if (accumulate && out_dir_populated) {
    const ProjectionSidecar recorded = readProjectionMode(out_dir);
    if (recorded.state == SidecarState::kUnreadable) {
      std::cerr << "error: --accumulate: the store in " << out_dir << " has a projection.json "
                << "that could not be read or carries no projection_mode.\n"
                << "  Its projection mode is therefore UNKNOWN, and an unknown mode is not\n"
                << "  assumed to be flat: folding this run in could composite flat-bottom and\n"
                << "  DEM-orthorectified samples into the same cells, which per-cell provenance\n"
                << "  cannot tell apart afterwards. Repair or remove the sidecar (a store with\n"
                << "  NO projection.json is a pre-#297 flat build), or use a fresh out_dir.\n";
      return 2;
    }
    const std::string existing_mode =
      recorded.state == SidecarState::kOk ? recorded.mode : "flat";
    const std::string mode_note = recorded.state == SidecarState::kMissing ?
      " (no projection.json — a pre-#297 flat build)" : "";
    if (existing_mode != projection_mode) {
      std::cerr << (allow_mixed_projection ? "warning" : "error")
                << ": --accumulate: the store in " << out_dir << " was built with "
                << "projection_mode '" << existing_mode << "'" << mode_note
                << ", but this run uses '" << projection_mode << "'.\n"
                << "  Compositing flat-bottom and DEM-orthorectified samples into the same\n"
                << "  cells is unrecoverable: per-cell provenance records only the source id,\n"
                << "  which is identical either way. Regenerate into a fresh out_dir instead\n"
                << "  of accumulating, or pass --allow-mixed-projection to accept the mix.\n";
      if (!allow_mixed_projection) {
        return 2;
      }
      // The mix is accepted, so the store now holds samples placed BOTH ways and
      // stays that way forever: record `mixed`, not this run's mode. Rewriting it
      // as pure `dem`/`flat` would launder the provenance — every later
      // --accumulate would then pass the guard silently (#297 review).
      sidecar_mode = kMixedMode;
      std::cerr << "warning: " << out_dir << " is now a MIXED-projection store; its "
                << "projection.json records '" << kMixedMode << "' permanently, and every "
                << "later --accumulate into it needs --allow-mixed-projection.\n";
    } else if (existing_mode == kMixedMode) {
      // Unreachable while kMixedMode is neither "flat" nor "dem" (the branch above
      // fires first), but keep the stickiness explicit rather than incidental.
      sidecar_mode = kMixedMode;
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
    const std::string reg_path = registry_file.string();
    if (out_dir_has_tiles && !out_dir_has_registry) {
      std::cerr << "error: --accumulate: " << out_dir << " holds value tiles but no "
                << "registry.json.\n"
                << "  The tiles' per-cell source indices are unresolvable without it, so this\n"
                << "  run cannot check that its --source-id matches theirs — and writing a\n"
                << "  fresh single-source registry over folded foreign coverage would assert a\n"
                << "  provenance the tiles do not have (ADR-0005 D2/D6). A store in this state\n"
                << "  is an interrupted build: remove it, or regenerate into a fresh out_dir.\n";
      return 2;
    }
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
      dem = std::make_unique<BathyDem>(
        bathy_store, splitCsv(bathy_layers), static_cast<std::size_t>(bathy_cache_tiles));
    } catch (const std::exception & e) {
      std::cerr << "error: " << e.what() << "\n";
      return 1;
    }
    for (const auto & warning : dem->warnings()) {
      std::cerr << "warning: " << warning << "\n";
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
  // than silently finish with the rest of the samples placed flat. The handler is
  // installed around the DEM CALL SITES only: a catch-all around the whole ping
  // loop would also change the flat path (which this feature must leave untouched)
  // and would report a Tier-1 decode or accumulator failure as a DEM fault.
  const auto demLookupFailed = [](const std::exception & e) {
      std::cerr << "error: bathy DEM lookup failed: " << e.what() << "\n"
                << "  a tile the startup scan listed could not be read, so the store is\n"
                << "  corrupt or truncated. No tiles and no registry were written.\n";
    };
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
      std::optional<double> nadir_bottom;
      try {
        nadir_bottom = dem->depthAt(gb.latitude_deg, gb.longitude_deg);
      } catch (const std::exception & e) {
        demLookupFailed(e);
        return 1;
      }
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
        DemCorrection corrected;
        try {
          corrected = correctedGroundRange(
            slant, gb.altitude_m, origin, azimuth, flat_ground,
            [&dem](double lat, double lon) {return dem->depthAt(lat, lon);});
        } catch (const std::exception & e) {
          demLookupFailed(e);
          return 1;
        }
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

  // DEM coverage gate (#297). The startup hard-fail proves the store exists and
  // holds tiles; it cannot prove the store OVERLAPS this survey. A valid store for
  // another lake would yield n_dem_hit == 0 while every sample took the flat
  // fallback and the tool exited 0 with a normal-looking summary. Checked here,
  // BEFORE saveTiles/writeRegistry, so a failure writes nothing.
  if (dem) {
    // Datum cross-check, reported FIRST — before the coverage gate's own exits and
    // before saveTiles/writeRegistry — because it is the stronger signal of the two:
    // low coverage means samples were left uncorrected, while a datum offset means
    // they were CONFIDENTLY MIS-PLACED. Reporting it after the gate's `return 3`
    // would silence it on exactly the runs a wrong vertical datum produces — a
    // store in the wrong vertical frame drives coverage down (its heights push the
    // geometry degenerate or non-convergent) and would exit 3 with no mention of
    // the datum at all (#297 round-2 review). It still only warns (the offset can
    // be a legitimate known bias, and the tool cannot tell).
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

    // Every non-converged status places the sample FLAT, so all of them belong in
    // the denominator — a run that is 40 % degenerate is 40 % flat-placed, and
    // scoring it on hits-vs-no-coverage alone would report coverage 1.0 and sail
    // through the very gate meant to stop it (#297 review).
    const std::size_t denominator =
      n_dem_hit + n_dem_no_coverage + n_dem_degenerate + n_dem_nonconverged;
    if (denominator == 0) {
      // No sample ever reached the lookup: an empty archive, or every ping dropped
      // upstream. That is a no-usable-input failure, not a coverage verdict —
      // reporting it as "coverage 0 (0 of 0)" would send the operator hunting for
      // the wrong problem.
      std::cerr << "error: no sample reached the DEM lookup, so this run placed nothing.\n"
                << "  read " << n_in << " ping(s), projected " << n_proj
                << "; dropped no-nadir=" << n_no_nadir << " bad-pose=" << n_bad_pose << "\n"
                << "  Check the Tier-1 archive rather than the bathy store: an empty .sst1,\n"
                << "  or pings without an altimeter return under --no-nadir-policy drop,\n"
                << "  produce this. Nothing was written.\n";
      return 3;
    }
    const double coverage =
      static_cast<double>(n_dem_hit) / static_cast<double>(denominator);
    const bool below_gate = coverage < min_dem_coverage;
    if (below_gate || coverage < 0.5) {
      std::string gate_note;
      if (below_gate) {
        gate_note = " is below --min-dem-coverage " + std::to_string(min_dem_coverage);
      }
      std::cerr << (below_gate ? "error" : "warning") << ": DEM coverage " << coverage
                << " (" << n_dem_hit << " of " << denominator << " samples that reached the "
                << "lookup; the other " << (denominator - n_dem_hit)
                << " were placed flat)" << gate_note << "\n"
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

  // Clear the prior build BEFORE anything of this run's is written, and only on the
  // explicit --overwrite (the refusal above is what an unflagged re-run gets). A
  // partial clear must not be written into, so a failure here aborts.
  if (!accumulate && overwrite && out_dir_populated) {
    std::string remove_error;
    const auto removed = removePriorStore(out_dir, &remove_error);
    if (!removed) {
      std::cerr << "error: --overwrite could not clear the previous build in " << out_dir
                << ": " << remove_error << "\n"
                << "  Refusing to write into a half-cleared store. Nothing of this run was\n"
                << "  written; the directory may now hold a partial mix of the previous\n"
                << "  build — remove it by hand or use a fresh out_dir.\n";
      return 1;
    }
    std::cerr << "overwrite: removed " << *removed << " file(s) of the previous build in "
              << out_dir << "\n";
  }

  // The projection sidecar is written FIRST — before any tile — so there is no
  // window in which tiles exist without a mode record. Written last, a crash or a
  // full filesystem between the tile write and the sidecar write would leave a
  // DEM-orthorectified store with no projection.json, which every later run reads
  // as a pre-#297 FLAT build and happily accumulates flat samples into (#297
  // round-2 review). The failure mode is now the harmless one instead: a sidecar
  // with no tiles beside it, which the guards treat as an interrupted build.
  // `saveTiles` creates out_dir lazily (and not at all when nothing is dirty), so
  // create it here.
  std::error_code mkdir_ec;
  std::filesystem::create_directories(out_dir, mkdir_ec);
  if (mkdir_ec && !std::filesystem::is_directory(out_dir)) {
    std::cerr << "error: cannot create output directory " << out_dir << ": "
              << mkdir_ec.message() << "\n";
    return 1;
  }
  if (!writeProjectionSidecar(
      out_dir, sidecar_mode, bathy_store, dem_mode ? bathy_layers : ""))
  {
    std::cerr << "error: could not write " << out_dir << "/projection.json.\n"
              << "  It records how this run placed its samples, and it is written before the\n"
              << "  tiles precisely so no tile can exist without it. Nothing was written:\n"
              << "  fix the output directory's permissions/space and re-run.\n";
    return 1;
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
  const std::string registry_path = registry_file.string();
  writeRegistry(registry_path, source_id, platform, sensor, sensor_class, campaign);
  // `writeRegistry` returns void, and BOTH --accumulate provenance guards key on
  // registry.json being present — an interrupted or failed write would leave tiles
  // that every later accumulate folds into unguarded. Verify the file landed.
  std::error_code registry_ec;
  if (!std::filesystem::exists(registry_path) ||
    std::filesystem::file_size(registry_path, registry_ec) == 0 || registry_ec)
  {
    std::cerr << "error: " << registry_path << " was not written (or is empty) after the "
              << "tiles were saved.\n"
              << "  The tiles carry per-cell source indices that only the registry resolves,\n"
              << "  and both --accumulate provenance guards look for this file — without it a\n"
              << "  later run would fold into the store unchecked. Regenerate the store.\n";
    return 1;
  }

  std::cerr << "tier2-processed: projected " << n_proj << "/" << n_in << " pings ("
            << n_placed << " samples; dropped no-nadir=" << n_no_nadir
            << " bad-pose=" << n_bad_pose << "), best-source into "
            << written << " 3-band tiles + registry.json in " << out_dir
            << " [projection=" << projection_mode
            << (sidecar_mode == projection_mode ? "" : " store=" + sidecar_mode) << "]\n";
  if (dem) {
    std::cerr << "tier2-processed: dem hit=" << n_dem_hit
              << " no-coverage=" << n_dem_no_coverage
              << " degenerate=" << n_dem_degenerate
              << " non-converged=" << n_dem_nonconverged
      // Deliberately NOT called "hits": these count DEM PROBES that returned a
      // value (up to one bilinear stencil per iteration per sample, plus one per
      // ping for the datum check), so they are not comparable with hit= above.
              << "; per-layer store lookups that returned data (probes, not samples)";
    for (const auto & kv : dem->lookupsByLayer()) {
      std::cerr << " " << kv.first << "=" << kv.second;
    }
    std::cerr << "\n";
  }
  return 0;
}

}  // namespace

int main(int argc, char ** argv)
{
  try {
    return runTool(argc, argv);
  } catch (const std::exception & e) {
    std::cerr << "error: unhandled exception: " << e.what() << "\n"
              << "  The run aborted. If the store had already begun writing, treat it as\n"
              << "  incomplete and regenerate it into a fresh output directory.\n";
    return 1;
  } catch (...) {
    std::cerr << "error: unhandled non-standard exception; the run aborted. Treat any\n"
              << "  partially written store as incomplete and regenerate it.\n";
    return 1;
  }
}
