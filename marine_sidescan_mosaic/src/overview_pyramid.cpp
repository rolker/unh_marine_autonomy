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

// [#188 / ADR-0011] Batch overview-pyramid builder for a sidescan store layer —
// production path (grid reconstruction, per-level fold, level loop, argument
// parsing). The `build_sidescan_overviews` CLI is a thin main() over these.
//
// Folds the layer's fine tiles (default GGGS level 13) into coarser parent
// tiles, level by level, into the layer's `overviews/` sidecar (flat dir,
// `<level>_<row>_<col>.tif` — level rides in the filename, same as the fine
// layer). Overviews are DERIVED + REGENERABLE: each run rebuilds the sidecar
// (idempotent; safe to re-run after every ingest).
//
// Fold policy (imagery, ADR-0011): intensity + quality fold by MEAN over valid
// contributors, where VALID means the QUALITY band (band 1) is non-zero — that
// is the processed store's no-data sentinel (marine_backscatter's
// ProcessedAccumulator starts a cell at quality 0 and grazingQuality() floors a
// real return to 1 "so a real return is never mistaken for the no-data 0").
// Intensity is an unfloored clamp of the sample, so a zero-intensity cell is a
// legitimate acoustic-shadow return — gating on intensity would drop every
// shadow cell and bias the overviews bright. The source band is 0 in every
// overview — a composite has no single source; provenance readers must use
// fine tiles.
//
// Memory: tiles are grouped by parent FROM FILENAMES and loaded <=4 children
// at a time (a whole-level in-memory fold of a 1000-tile store would be
// ~5.6 GB; this path peaks around ~250 MB per parent tile — dominated by the
// fold engine's per-cell contributor buckets, see overview_builder.hpp — and
// that peak does not grow with store size). Each level is built from the level
// below it (already in the sidecar), not by re-reading the fine data.

#include "marine_sidescan_mosaic/overview_pyramid.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_autonomy/gggs/index_math.h"
#include "marine_tiled_raster_store/overview_builder.hpp"
#include "marine_tiled_raster_store/tile_io.hpp"

namespace marine_sidescan_mosaic
{

namespace
{

namespace fs = std::filesystem;
using marine_tiled_raster_store::TiledRasterTile;
using Cell = marine_tiled_raster_store::CellValues<std::uint16_t>;

constexpr std::size_t kBands = 3;        // intensity, quality, source
constexpr std::size_t kQualityBand = 1;   // the no-data sentinel band (see validCell)
constexpr std::uint16_t kNoData = 0;

// Mean intensity + mean quality over the contributors; source = 0 (composite).
// ROUNDED, not truncated: integer truncation loses up to 1 count per level and
// the pyramid chains 13 levels by default, so the darkening bias compounds all
// the way to the apex.
std::uint16_t roundedMean(std::uint64_t sum, std::size_t n)
{
  return static_cast<std::uint16_t>((sum + n / 2) / n);
}

Cell imageryMeanFold(const std::vector<Cell> & contributors)
{
  std::uint64_t sum_intensity = 0;
  std::uint64_t sum_quality = 0;
  for (const Cell & c : contributors) {
    sum_intensity += c[0];
    sum_quality += c[1];
  }
  return Cell{
    roundedMean(sum_intensity, contributors.size()),
    roundedMean(sum_quality, contributors.size()),
    0};
}

// Contributor gate. The processed store's no-data sentinel is the QUALITY band,
// NOT intensity: a cell starts at quality 0 and a real return's quality is
// floored to >=1 (marine_backscatter::grazingQuality), while intensity is an
// unfloored clamp(sample, 0, 65535). A zero-intensity, non-zero-quality cell is
// an acoustic shadow — surveyed, real, and dark — and must participate in the
// fold, or every shadow is erased and the overview biases bright.
bool validCell(const Cell & cell) {return cell[kQualityBand] != kNoData;}

// Reconstruct the GridIndex named `<level>_<row>_<col>` through the public
// geographic lookup (the (level,row,col) ctor is Level-private by design): the
// filename parts give the grid's south/west corner via the level spec, and the
// centre point maps back through Level::gridIndex. tileFilename round-trip
// verifies the arithmetic — a mismatch (e.g. a future spec change) skips the
// file loudly instead of folding it into the wrong parent.
//
// NOTE: the column width uses the latitude-based gggs::latitudeScaleFactor(double)
// overload, which disagrees with the authoritative row-based
// LevelSpec::latitudeScaleFactor(row) exactly on the 72/80 degree polar band
// boundaries. The sidescan survey envelope is non-polar (|lat| < 72; see
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
    // domain and gggs::Level::gridIndex throws — one malformed filename must
    // skip its own file, not abort the whole run.
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

  const std::vector<std::optional<std::uint16_t>> nodata(
    kBands, std::optional<std::uint16_t>(kNoData));
  for (const auto & group : by_parent) {
    std::vector<TiledRasterTile<std::uint16_t>> children;
    children.reserve(group.second.size());
    std::vector<const TiledRasterTile<std::uint16_t> *> child_ptrs;
    for (const gggs::GridIndex & grid : group.second) {
      const fs::path path =
        src_dir / marine_tiled_raster_store::tileFilename(grid);
      children.push_back(
        marine_tiled_raster_store::loadTile<std::uint16_t>(
          path.string(), gggs::Level(child_level), kBands));
      child_ptrs.push_back(&children.back());
    }
    const TiledRasterTile<std::uint16_t> parent_tile =
      marine_tiled_raster_store::buildParentTile<std::uint16_t>(
      group.first, child_ptrs,
      std::vector<std::uint16_t>(kBands, kNoData),
      validCell, imageryMeanFold);
    marine_tiled_raster_store::saveTile<std::uint16_t>(
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

ArgStatus parseOverviewArgs(int argc, char ** argv, OverviewOptions & out)
{
  out = OverviewOptions{};
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--fine-level" && i + 1 < argc) {
      if (!parseInt(argv[++i], out.fine_level)) {
        return ArgStatus::kError;
      }
    } else if (arg == "--min-level" && i + 1 < argc) {
      if (!parseInt(argv[++i], out.min_level)) {
        return ArgStatus::kError;
      }
    } else if (arg == "--dry-run") {
      out.dry_run = true;
    } else if (arg == "--help" || arg == "-h") {
      return ArgStatus::kHelp;
    } else if (out.layer_dir.empty() && !arg.empty() && arg[0] != '-') {
      out.layer_dir = arg;
    } else {
      return ArgStatus::kError;
    }
  }
  if (out.layer_dir.empty() || out.fine_level <= 0 || out.fine_level > 20 ||
    out.min_level < 0 || out.min_level >= out.fine_level)
  {
    return ArgStatus::kError;
  }
  return ArgStatus::kOk;
}

OverviewBuildResult buildOverviewPyramid(
  const OverviewOptions & opts, std::ostream * progress)
{
  if (opts.fine_level <= 0 || opts.fine_level > 20 ||
    opts.min_level < 0 || opts.min_level >= opts.fine_level)
  {
    throw std::invalid_argument("buildOverviewPyramid: level range out of bounds");
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
  // happen to be named <fine_level>_<row>_<col>.tif would pass it and be
  // rebuilt with the sidescan imagery policy (and its band semantics). Open one
  // tile and require the 3-band sidescan shape before touching anything.
  const std::string probe_path =
    (layer_dir / marine_tiled_raster_store::tileFilename(fine_grids.front())).string();
  const int probe_bands = marine_tiled_raster_store::tileRasterCount(probe_path);
  if (probe_bands != static_cast<int>(kBands)) {
    throw std::runtime_error(
      "not a sidescan layer: " + probe_path + " has " +
      std::to_string(probe_bands) + " band(s), expected " +
      std::to_string(kBands) + " (intensity, quality, source); refusing to "
      "replace overviews/ with a sidescan-policy pyramid");
  }

  // --dry-run stops here: the guards above are exactly the checks that catch a
  // mistyped path or wrong layer, and nothing below this point is reached
  // without writing. Report what the run would do and touch nothing.
  if (opts.dry_run) {
    OverviewBuildResult preview;
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
  // The staging directory doubles as the run lock: create_directory fails when
  // it already exists, so a second concurrent run over the same layer refuses
  // instead of interleaving its tiles with the first run's. A crashed run leaves
  // the directory behind as debris — the message says how to clear it.
  std::error_code create_ec;
  if (!fs::create_directory(staging, create_ec)) {
    throw std::runtime_error(
      "cannot claim staging directory " + staging.string() +
      (create_ec ?
      ": " + create_ec.message() :
      " (it already exists — another build is running over this layer, or a "
      "crashed run left it behind; remove it to retry)"));
  }

  OverviewBuildResult result;
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
        // A level above min_level produced nothing: the fine-tile chain broke
        // (a healthy store folds down to min_level without an empty level, since
        // gggs::parent stays valid to level 0). Surface it; do not swap in a
        // partial pyramid.
        result.early_empty = true;
        break;
      }
      result.tiles_written += counts.out;
      result.coarsest_level = level - 1;
    }
  } catch (...) {
    fs::remove_all(staging);   // never leave a partial staging dir behind
    throw;
  }

  // Refuse the swap unless the pyramid is complete: a partial one must never
  // displace a previously-complete sidecar. `early_empty` means the fine-tile
  // chain broke; `tiles_skipped` means one or more fine tiles failed grid
  // reconstruction, so their coverage is simply missing from every level built.
  if (result.early_empty || result.tiles_skipped > 0) {
    fs::remove_all(staging);
    return result;
  }

  // Swap staging over the live sidecar, rename-aside rather than
  // remove-then-rename so the previous sidecar exists at every instant:
  // overviews/ -> overviews.old/, staging -> overviews/, then drop
  // overviews.old/. If the second rename fails, the first is undone and the run
  // leaves the previous sidecar exactly where it was. A crash in the (two
  // same-directory renames) window leaves the previous sidecar as
  // overviews.old/ — recoverable by hand; it is never destroyed before the new
  // one is in place. This is crash-SAFE, not atomic: POSIX offers no atomic
  // directory swap on a plain filesystem.
  const bool had_previous = fs::exists(overviews);
  if (had_previous) {
    fs::remove_all(retired);
    fs::rename(overviews, retired);
  }
  try {
    fs::rename(staging, overviews);
  } catch (...) {
    if (had_previous) {
      fs::rename(retired, overviews);   // restore the previous sidecar
    }
    fs::remove_all(staging);
    throw;
  }
  fs::remove_all(retired);
  result.sidecar_replaced = true;
  return result;
}

}  // namespace marine_sidescan_mosaic
