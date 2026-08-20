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

// [ADR-0010 D9 / ADR-0011] Batch overview-pyramid builder for a depth store
// layer (draft/processed) — production path (grid reconstruction, per-level
// fold, level loop, argument parsing). The `build_depth_overviews` CLI is a thin
// main() over these.
//
// Folds the layer's fine tiles (native GGGS level) into coarser parent tiles,
// level by level, into the layer's `overviews/` sidecar (flat dir,
// `<level>_<row>_<col>.tif` — level rides in the filename, same as the fine
// layer). Overviews are DERIVED + REGENERABLE: each run rebuilds the sidecar
// (idempotent; safe to re-run after every ingest).
//
// Fold policy (depth, ADR-0010 D9): SHALLOWEST-PRESERVING. Among the valid
// contributors that land in one coarse cell, the one with the MAXIMUM ellipsoidal
// height (band 0 — most positive / least negative, i.e. shoalest and most
// hazardous to navigation) is selected and its whole {depth, σ} pair is carried
// through. Never a mean: a coarse corridor query must plan around the rock, not
// average it away, and the σ must stay coherent with the depth it describes, so
// the pair travels together (the fold operates on whole cells, all bands at once
// — see overview_builder.hpp CellFoldPolicy). VALID means band 0 (depth) is not
// NaN — NaN is the store's per-band no-data sentinel (see bathymetry_tile.hpp).
//
// Layer scope (ADR-0010 D9): only draft/processed get a generated pyramid; chart
// inherits the ENC scale ladder and reference stays as-imported (neither is a
// caller of this path). No upsampling — only parent levels are built (the
// min_level < fine_level range check plus the fold-toward-apex loop enforce it).
//
// Memory: tiles are grouped by parent FROM FILENAMES and loaded <=4 children at a
// time (a whole-level in-memory fold would grow without bound with store size;
// this path peaks around one parent tile's contributor buckets — see
// overview_builder.hpp — and that peak does not grow with store size). Each level
// is built from the level below it (already in the sidecar), not by re-reading
// the fine data.

#include "marine_bathymetry_store/overview_pyramid.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_autonomy/gggs/index_math.h"
#include "marine_bathymetry_store/bathymetry_tile.hpp"
#include "marine_tiled_raster_store/overview_builder.hpp"
#include "marine_tiled_raster_store/tile_io.hpp"

namespace marine_bathymetry_store
{

namespace
{

namespace fs = std::filesystem;
using marine_tiled_raster_store::TiledRasterTile;
using Cell = marine_tiled_raster_store::CellValues<double>;

// The on-disk depth value tile is 2-band Float64: depth (band 0, ellipsoidal
// height in metres) + uncertainty (band 1, σ in metres). NaN is the per-band
// no-data sentinel (bathymetry_tile.hpp).
constexpr std::size_t kBands = BathymetryTile::value_band_count;   // 2
constexpr std::size_t kDepthBand = 0;   // the shoalest-selection + no-data band

// Shallowest-preserving fold (ADR-0010 D9). Among the valid contributors, select
// the one with the MAXIMUM ellipsoidal height (band 0) — the shoalest, most
// hazardous cell — and return its WHOLE {depth, σ} pair, so the coarse cell's
// uncertainty stays coherent with the depth it describes. Never a mean: a coarse
// corridor query must not average a rock away, and a mixed depth/σ pair would
// describe a cell that never existed. Called only with >=1 contributor (the
// engine gates on validCell first), each carrying a non-NaN depth.
Cell depthShallowestFold(const std::vector<Cell> & contributors)
{
  const Cell * shoalest = &contributors.front();
  for (const Cell & c : contributors) {
    // Strict `>`: ties keep the first contributor. Both are non-NaN here
    // (validCell gate), so no NaN-propagation surprise in the comparison.
    if (c[kDepthBand] > (*shoalest)[kDepthBand]) {
      shoalest = &c;
    }
  }
  return *shoalest;   // the whole pair, depth AND its paired uncertainty
}

// Contributor gate. NaN in the DEPTH band (band 0) is the no-data sentinel
// (bathymetry_tile.hpp initialises empty cells to {NaN, NaN}). A cell with a real
// depth participates even if its uncertainty happens to be NaN — the depth is the
// navigable quantity and the pair is carried as-is; gating on band 0 alone
// matches how the store itself distinguishes surveyed from unsurveyed cells.
bool validCell(const Cell & cell) {return !std::isnan(cell[kDepthBand]);}

// Reconstruct the GridIndex named `<level>_<row>_<col>` through the public
// geographic lookup (the (level,row,col) ctor is Level-private by design): the
// filename parts give the grid's south/west corner via the level spec, and the
// centre point maps back through Level::gridIndex. tileFilename round-trip
// verifies the arithmetic — a mismatch (e.g. a future spec change) skips the file
// loudly instead of folding it into the wrong parent. Inlined from the sidescan
// builder rather than reusing tile_io's `gridIndexFromTileFilename`: that helper
// THROWS on a malformed name, which would abort the whole run and defeat the
// skip-loudly-and-refuse property below (a partial pyramid must never displace a
// complete sidecar).
//
// NOTE: the column width uses the latitude-based gggs::latitudeScaleFactor(double)
// overload, which disagrees with the authoritative row-based
// LevelSpec::latitudeScaleFactor(row) exactly on the 72/80 degree polar band
// boundaries. The depth survey envelope is non-polar (|lat| < 72; see
// tile_io.hpp), so the two agree here; on a polar tile they could diverge, but
// the tileFilename round-trip check below would then fail and skip the file
// rather than mis-place it — so the assumption fails safe.
gggs::GridIndex gridFromName(
  uint8_t level, uint32_t row, uint32_t col, const std::string & name)
{
  const double span = gggs::levels[level].grid_angular_span;
  const double south = -96.0 + row * span;
  const double lat = south + span / 2.0;
  const double lon_span = span * gggs::latitudeScaleFactor(lat);
  const double west = -180.0 + col * lon_span;
  const double lon = west + lon_span / 2.0;
  gggs::GridIndex grid;
  try {
    // A row/column field far out of range puts `lat`/`lon` outside the geodetic
    // domain and gggs::Level::gridIndex throws — one malformed filename must skip
    // its own file, not abort the whole run.
    grid = gggs::Level(level).gridIndex(lat, lon);
  } catch (const std::exception & e) {
    std::cerr << "warning: skipping " << name <<
      " (grid reconstruction failed: " << e.what() << ")\n";
    return gggs::GridIndex();
  }
  if (marine_tiled_raster_store::tileFilename(grid) != name) {
    std::cerr << "warning: skipping " << name <<
      " (grid reconstruction mismatch)\n";
    return gggs::GridIndex();
  }
  return grid;
}

// Enumerate `<level>_<row>_<col>.tif` grids at @p level in @p dir (names only —
// nothing is loaded). Each name that matches the level but fails grid
// reconstruction increments @p skipped.
std::vector<gggs::GridIndex> gridsInDir(
  const fs::path & dir, uint8_t level, std::size_t & skipped)
{
  // `.tif` only: tileFilename() emits nothing else, so a `.tiff` here is not one
  // of our tiles — matching it would only produce a misleading "grid
  // reconstruction mismatch" for a file we never wrote.
  static const std::regex kName(R"((\d+)_(\d+)_(\d+)\.tif)");
  std::vector<gggs::GridIndex> grids;
  if (!fs::is_directory(dir)) {
    return grids;
  }
  for (const auto & entry : fs::directory_iterator(dir)) {
    std::smatch m;
    const std::string name = entry.path().filename().string();
    if (!entry.is_regular_file() || !std::regex_match(name, m, kName)) {
      continue;
    }
    // The regex only proves the fields are digits — an overlong one still
    // overflows std::stoul. A malformed name must skip its own file loudly, as
    // documented, not abort the whole run with an uncaught out_of_range.
    unsigned long parts[3] = {0, 0, 0};   // NOLINT(runtime/int) — stoul's type
    bool parsed = true;
    for (std::size_t p = 0; p < 3 && parsed; ++p) {
      try {
        parts[p] = std::stoul(m[p + 1]);
      } catch (const std::exception &) {
        parsed = false;
      }
    }
    constexpr unsigned long kMaxIndex = 0xFFFFFFFFUL;   // NOLINT(runtime/int)
    if (!parsed || parts[1] > kMaxIndex || parts[2] > kMaxIndex) {
      std::cerr << "warning: skipping " << name <<
        " (level/row/column out of representable range)\n";
      ++skipped;
      continue;
    }
    if (parts[0] != level) {
      continue;
    }
    const gggs::GridIndex grid = gridFromName(
      level, static_cast<uint32_t>(parts[1]), static_cast<uint32_t>(parts[2]), name);
    if (grid.valid()) {
      grids.push_back(grid);
    } else {
      ++skipped;
    }
  }
  return grids;
}

// One level's tile counts: how many child tiles were read, how many parents were
// written. The IN count is the diagnostic one for a partial store — an operator
// seeing "40 in" for a 1000-tile layer knows immediately what is wrong.
struct LevelCounts
{
  std::size_t in = 0;
  std::size_t out = 0;
};

// Build one coarser level: children at @p child_level read from @p src_dir,
// parents written to @p out_dir. Grid-reconstruction skips accumulate into
// @p skipped (and are not counted in @c LevelCounts::in).
LevelCounts buildLevel(
  const fs::path & src_dir, const fs::path & out_dir, uint8_t child_level,
  std::size_t & skipped)
{
  LevelCounts counts;
  std::map<gggs::GridIndex, std::vector<gggs::GridIndex>> by_parent;
  for (const gggs::GridIndex & child : gridsInDir(src_dir, child_level, skipped)) {
    ++counts.in;
    const gggs::GridIndex parent_grid = gggs::parent(child);
    if (parent_grid.valid()) {
      by_parent[parent_grid].push_back(child);
    }
  }

  const double nan = std::numeric_limits<double>::quiet_NaN();
  const std::vector<std::optional<double>> nodata(
    kBands, std::optional<double>(nan));
  for (const auto & group : by_parent) {
    std::vector<TiledRasterTile<double>> children;
    children.reserve(group.second.size());
    std::vector<const TiledRasterTile<double> *> child_ptrs;
    for (const gggs::GridIndex & grid : group.second) {
      const fs::path path =
        src_dir / marine_tiled_raster_store::tileFilename(grid);
      children.push_back(
        marine_tiled_raster_store::loadTile<double>(
          path.string(), gggs::Level(child_level), kBands));
      child_ptrs.push_back(&children.back());
    }
    const TiledRasterTile<double> parent_tile =
      marine_tiled_raster_store::buildParentTile<double>(
      group.first, child_ptrs,
      std::vector<double>(kBands, nan),
      validCell, depthShallowestFold);
    marine_tiled_raster_store::saveTile<double>(
      parent_tile,
      (out_dir / marine_tiled_raster_store::tileFilename(group.first)).string(),
      nodata);
    ++counts.out;
  }
  return counts;
}

// Strict integer parse: rejects an empty, non-numeric or trailing-garbage value
// that std::atoi would silently read as 0 (`--min-level abc` must be a usage
// error, not a silent "build to the apex").
bool parseInt(const char * text, int & out)
{
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  try {
    std::size_t used = 0;
    const int value = std::stoi(text, &used);
    if (text[used] != '\0') {
      return false;
    }
    out = value;
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

}  // namespace

DepthArgStatus parseDepthOverviewArgs(
  int argc, char ** argv, DepthOverviewOptions & out)
{
  out = DepthOverviewOptions{};
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--fine-level" && i + 1 < argc) {
      if (!parseInt(argv[++i], out.fine_level)) {
        return DepthArgStatus::kError;
      }
    } else if (arg == "--min-level" && i + 1 < argc) {
      if (!parseInt(argv[++i], out.min_level)) {
        return DepthArgStatus::kError;
      }
    } else if (arg == "--dry-run") {
      out.dry_run = true;
    } else if (arg == "--help" || arg == "-h") {
      return DepthArgStatus::kHelp;
    } else if (out.layer_dir.empty() && !arg.empty() && arg[0] != '-') {
      out.layer_dir = arg;
    } else {
      return DepthArgStatus::kError;
    }
  }
  // min_level < fine_level is also the no-upsample guard: the coarsest level built
  // is strictly finer-numbered than (i.e. coarser than) the fine tiles, never the
  // reverse.
  if (out.layer_dir.empty() || out.fine_level <= 0 || out.fine_level > 20 ||
    out.min_level < 0 || out.min_level >= out.fine_level)
  {
    return DepthArgStatus::kError;
  }
  return DepthArgStatus::kOk;
}

DepthOverviewBuildResult buildDepthOverviewPyramid(
  const DepthOverviewOptions & opts, std::ostream * progress)
{
  // Level-range guard (also the no-upsample invariant): min_level must be strictly
  // below fine_level, so the build only ever produces coarser tiles.
  if (opts.fine_level <= 0 || opts.fine_level > 20 ||
    opts.min_level < 0 || opts.min_level >= opts.fine_level)
  {
    throw std::invalid_argument(
      "buildDepthOverviewPyramid: level range out of bounds");
  }
  const fs::path layer_dir(opts.layer_dir);
  if (!fs::is_directory(layer_dir)) {
    throw std::runtime_error("not a directory: " + opts.layer_dir);
  }

  // Guard: never wipe the sidecar for an empty or mis-pointed layer. Require at
  // least one fine tile at the declared level before touching overviews/ — a
  // wrong --fine-level or a path typo must not destroy a previously-good build.
  std::size_t guard_skipped = 0;
  const std::vector<gggs::GridIndex> fine_grids =
    gridsInDir(layer_dir, static_cast<uint8_t>(opts.fine_level), guard_skipped);
  if (fine_grids.empty()) {
    // Distinguish "nothing there" (a path or --fine-level typo) from "tiles are
    // there but none could be reconstructed" — the same message for both sends
    // the operator hunting a typo that does not exist.
    throw std::runtime_error(
      "no usable fine tiles at level " + std::to_string(opts.fine_level) +
      " under " + opts.layer_dir +
            (guard_skipped > 0 ?
            " (" + std::to_string(guard_skipped) + " tile name(s) matched that level "
            "but failed grid reconstruction — see the warnings above; not a path or "
            "--fine-level typo)" :
            " (no tile matched that level — check the path and --fine-level)") +
      "; refusing to replace overviews/");
  }

  // The enumeration above is filename-only. Another store's layer whose tiles
  // happen to be named <fine_level>_<row>_<col>.tif would pass it and be rebuilt
  // with the depth shallowest-preserving policy (and its band semantics). Open one
  // tile and require the 2-band depth shape before touching anything.
  const std::string probe_path =
    (layer_dir / marine_tiled_raster_store::tileFilename(fine_grids.front())).string();
  const int probe_bands = marine_tiled_raster_store::tileRasterCount(probe_path);
  if (probe_bands != static_cast<int>(kBands)) {
    throw std::runtime_error(
      "not a depth layer: " + probe_path + " has " +
      std::to_string(probe_bands) + " band(s), expected " +
      std::to_string(kBands) + " (depth, uncertainty); refusing to replace "
      "overviews/ with a depth-policy pyramid");
  }

  // --dry-run stops here: the guards above are exactly the checks that catch a
  // mistyped path or wrong layer, and nothing below this point is reached without
  // writing. Report what the run would do and touch nothing.
  if (opts.dry_run) {
    DepthOverviewBuildResult preview;
    preview.tiles_skipped = guard_skipped;
    if (progress != nullptr) {
      *progress << "dry run: " << fine_grids.size() << " usable fine tile(s) at "
        "level " << opts.fine_level << " under " << opts.layer_dir <<
        " (" << guard_skipped << " unreconstructable); would build levels " <<
        (opts.fine_level - 1) << "..." << opts.min_level <<
        " and replace " << (layer_dir / "overviews").string() << "\n";
    }
    return preview;
  }

  const fs::path overviews = layer_dir / "overviews";
  const fs::path staging = layer_dir / "overviews.tmp";
  const fs::path retired = layer_dir / "overviews.old";

  // Crash-safe regeneration: build into a staging sibling and swap it over the
  // live sidecar only after every level succeeds, so an interrupted or throwing
  // run leaves the previous overviews/ intact rather than a truncated one a
  // consumer would read as complete.
  //
  // The staging directory doubles as the run lock: create_directory fails when it
  // already exists, so a second concurrent run over the same layer refuses instead
  // of interleaving its tiles with the first run's. A crashed run leaves the
  // directory behind as debris — the message says how to clear it.
  std::error_code create_ec;
  if (!fs::create_directory(staging, create_ec)) {
    throw std::runtime_error(
      "cannot claim staging directory " + staging.string() +
            (create_ec ?
            ": " + create_ec.message() :
            " (it already exists — another build is running over this layer, or a "
            "crashed run left it behind; remove it to retry)"));
  }

  DepthOverviewBuildResult result;
  try {
    // The finest overview level folds the fine layer itself; every subsequent
    // level folds the staging level just written.
    for (int level = opts.fine_level; level > opts.min_level; --level) {
      const fs::path src = (level == opts.fine_level) ? layer_dir : staging;
      const LevelCounts counts = buildLevel(
        src, staging, static_cast<uint8_t>(level), result.tiles_skipped);
      if (progress != nullptr) {
        *progress << "level " << level << " -> " << (level - 1) << ": " <<
          counts.in << " tile(s) in, " << counts.out << " overview tile(s) out\n";
      }
      if (counts.out == 0) {
        // A level above min_level produced nothing: the fine-tile chain broke (a
        // healthy store folds down to min_level without an empty level, since
        // gggs::parent stays valid to level 0). Surface it; do not swap in a
        // partial pyramid.
        result.early_empty = true;
        break;
      }
      result.tiles_written += counts.out;
      result.coarsest_level = level - 1;
    }
  } catch (...) {
    // Best-effort: cleanup must not throw here, or it would replace the original
    // exception with its own.
    std::error_code ec;
    fs::remove_all(staging, ec);   // never leave a partial staging dir behind
    throw;
  }

  // Refuse the swap unless the pyramid is complete: a partial one must never
  // displace a previously-complete sidecar. `early_empty` means the fine-tile
  // chain broke; `tiles_skipped` means one or more fine tiles failed grid
  // reconstruction, so their coverage is simply missing from every level built.
  if (result.early_empty || result.tiles_skipped > 0) {
    // Best-effort: a throwing cleanup would replace the refusal result (and its
    // skip/empty diagnostics) with an exception, and leave the run-lock debris
    // regardless. Warn so the cleanup failure is still visible.
    std::error_code ec;
    fs::remove_all(staging, ec);
    if (ec) {
      std::cerr << "warning: could not remove staging dir " <<
        staging.string() << ": " << ec.message() << std::endl;
    }
    return result;
  }

  // Swap staging over the live sidecar, rename-aside rather than
  // remove-then-rename so the previous sidecar exists at every instant:
  // overviews/ -> overviews.old/, staging -> overviews/, then drop overviews.old/.
  // If the second rename fails, the first is undone and the run leaves the
  // previous sidecar exactly where it was. A crash in the (two same-directory
  // renames) window leaves the previous sidecar as overviews.old/ — recoverable by
  // hand; it is never destroyed before the new one is in place. This is
  // crash-SAFE, not atomic: POSIX offers no atomic directory swap on a plain
  // filesystem.
  const bool had_previous = fs::exists(overviews);
  bool retired_moved = false;
  try {
    // Retire the previous sidecar (overviews/ -> overviews.old/) and swap the new
    // one in under one guard: a throw from the retire step must clear the staging
    // lock too, or overviews.tmp/ blocks the next run.
    if (had_previous) {
      fs::remove_all(retired);
      fs::rename(overviews, retired);
      retired_moved = true;
    }
    fs::rename(staging, overviews);
  } catch (...) {
    // Clear the staging debris (best-effort, non-throwing — a throwing cleanup
    // would mask the original failure and skip the restore below), then restore.
    // Restore only if the retire rename actually completed — otherwise overviews/
    // was never moved and is still in place.
    std::error_code ec;
    fs::remove_all(staging, ec);
    if (retired_moved) {
      fs::rename(retired, overviews);   // restore the previous sidecar
    }
    throw;
  }
  // Best-effort: the swap already succeeded — a throwing cleanup here would report
  // the build as failed with the new sidecar live, prompting a needless re-run.
  // Warn so the leftover overviews.old/ is still visible.
  std::error_code ec;
  fs::remove_all(retired, ec);
  if (ec) {
    std::cerr << "warning: could not remove retired sidecar " <<
      retired.string() << ": " << ec.message() << std::endl;
  }
  result.sidecar_replaced = true;
  return result;
}

}  // namespace marine_bathymetry_store
