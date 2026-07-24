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
// (non-zero-intensity) contributors; the source band is 0 in every overview —
// a composite has no single source; provenance readers must use fine tiles.
//
// Memory: tiles are grouped by parent FROM FILENAMES and loaded <=4 children
// at a time (a whole-level in-memory fold of a 1000-tile store would be
// ~5.6 GB; this path stays ~100 MB). Each level is built from the level below
// it (already in the sidecar), not by re-reading the fine data.

#include "marine_sidescan_mosaic/overview_pyramid.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
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
constexpr std::uint16_t kNoData = 0;

// Mean intensity + mean quality over the contributors; source = 0 (composite).
Cell imageryMeanFold(const std::vector<Cell> & contributors)
{
  std::uint64_t sum_intensity = 0;
  std::uint64_t sum_quality = 0;
  for (const Cell & c : contributors) {
    sum_intensity += c[0];
    sum_quality += c[1];
  }
  return Cell{
    static_cast<std::uint16_t>(sum_intensity / contributors.size()),
    static_cast<std::uint16_t>(sum_quality / contributors.size()),
    0};
}

bool validIntensity(const Cell & cell) {return cell[0] != kNoData;}

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
  const gggs::GridIndex grid = gggs::Level(level).gridIndex(lat, lon);
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
  static const std::regex kName(R"((\d+)_(\d+)_(\d+)\.tiff?)");
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
    if (std::stoul(m[1]) != level) {
      continue;
    }
    const gggs::GridIndex grid =
      gridFromName(level, std::stoul(m[2]), std::stoul(m[3]), name);
    if (grid.valid()) {
      grids.push_back(grid);
    } else {
      ++skipped;
    }
  }
  return grids;
}

// Build one coarser level: children at @p child_level read from @p src_dir,
// parents written to @p out_dir. Returns the number of parent tiles written;
// grid-reconstruction skips accumulate into @p skipped.
std::size_t buildLevel(
  const fs::path & src_dir, const fs::path & out_dir, uint8_t child_level,
  std::size_t & skipped)
{
  std::map<gggs::GridIndex, std::vector<gggs::GridIndex>> by_parent;
  for (const gggs::GridIndex & child : gridsInDir(src_dir, child_level, skipped)) {
    const gggs::GridIndex parent_grid = gggs::parent(child);
    if (parent_grid.valid()) {
      by_parent[parent_grid].push_back(child);
    }
  }

  const std::vector<std::optional<std::uint16_t>> nodata(
    kBands, std::optional<std::uint16_t>(kNoData));
  std::size_t written = 0;
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
      validIntensity, imageryMeanFold);
    marine_tiled_raster_store::saveTile<std::uint16_t>(
      parent_tile,
      (out_dir / marine_tiled_raster_store::tileFilename(group.first)).string(),
      nodata);
    ++written;
  }
  return written;
}

}  // namespace

ArgStatus parseOverviewArgs(int argc, char ** argv, OverviewOptions & out)
{
  out = OverviewOptions{};
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--fine-level" && i + 1 < argc) {
      out.fine_level = std::atoi(argv[++i]);
    } else if (arg == "--min-level" && i + 1 < argc) {
      out.min_level = std::atoi(argv[++i]);
    } else if (arg == "--help" || arg == "-h") {
      return ArgStatus::kHelp;
    } else if (out.layer_dir.empty() && arg[0] != '-') {
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
  if (gridsInDir(layer_dir, static_cast<uint8_t>(opts.fine_level), guard_skipped).empty()) {
    throw std::runtime_error(
      "no fine tiles at level " + std::to_string(opts.fine_level) + " under " +
      opts.layer_dir + " (refusing to wipe overviews/)");
  }

  const fs::path overviews = layer_dir / "overviews";
  const fs::path staging = layer_dir / "overviews.tmp";

  // Atomic regeneration: build into a staging sibling and swap it over the live
  // sidecar only after every level succeeds. An interrupted or throwing run
  // leaves the previous overviews/ intact rather than a truncated one that a
  // consumer would read as complete.
  fs::remove_all(staging);
  fs::create_directories(staging);

  OverviewBuildResult result;
  try {
    // The finest overview level folds the fine layer itself; every subsequent
    // level folds the staging level just written.
    for (int level = opts.fine_level; level > opts.min_level; --level) {
      const fs::path src = (level == opts.fine_level) ? layer_dir : staging;
      const std::size_t written = buildLevel(
        src, staging, static_cast<uint8_t>(level), result.tiles_skipped);
      if (progress != nullptr) {
        *progress << "level " << level << " -> " << (level - 1) << ": " <<
          written << " overview tile(s)\n";
      }
      if (written == 0) {
        // A level above min_level produced nothing: the fine-tile chain broke
        // (a healthy store folds down to min_level without an empty level, since
        // gggs::parent stays valid to level 0). Surface it; do not swap in a
        // partial pyramid.
        result.early_empty = true;
        break;
      }
      result.tiles_written += written;
      result.coarsest_level = level - 1;
    }
  } catch (...) {
    fs::remove_all(staging);   // never leave a partial staging dir behind
    throw;
  }

  if (result.early_empty) {
    fs::remove_all(staging);
    return result;
  }

  // Swap staging over the live sidecar. remove_all first: rename onto a
  // non-empty directory fails; the window between remove and rename is the
  // price of a plain-filesystem atomic (no cross-dir hardlink swap here).
  fs::remove_all(overviews);
  fs::rename(staging, overviews);
  return result;
}

}  // namespace marine_sidescan_mosaic
