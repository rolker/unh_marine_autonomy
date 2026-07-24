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

// [#188 / ADR-0011] Batch overview-pyramid builder for a sidescan store layer.
//
// Folds the layer's fine tiles (default GGGS level 13) into coarser parent
// tiles, level by level, into the layer's `overviews/` sidecar (flat dir,
// `<level>_<row>_<col>.tif` — level rides in the filename, same as the fine
// layer). Overviews are DERIVED + REGENERABLE: each run deletes and recreates
// the sidecar (idempotent; safe to re-run after every ingest).
//
// Fold policy (imagery, ADR-0011): intensity + quality fold by MEAN over valid
// (non-zero-intensity) contributors; the source band is 0 in every overview —
// a composite has no single source; provenance readers must use fine tiles.
//
// Memory: tiles are grouped by parent FROM FILENAMES and loaded <=4 children
// at a time (a whole-level in-memory fold of a 1000-tile store would be
// ~5.6 GB; this path stays ~100 MB). Each level is built from the level below
// it (already in the sidecar), not by re-reading the fine data.

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_autonomy/gggs/index_math.h"
#include "marine_tiled_raster_store/overview_builder.hpp"
#include "marine_tiled_raster_store/tile_io.hpp"

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
// nothing is loaded).
std::vector<gggs::GridIndex> gridsInDir(const fs::path & dir, uint8_t level)
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
    }
  }
  return grids;
}

// Build one coarser level: children at @p child_level read from @p src_dir,
// parents written to @p out_dir. Returns the number of parent tiles written.
std::size_t buildLevel(
  const fs::path & src_dir, const fs::path & out_dir, uint8_t child_level)
{
  std::map<gggs::GridIndex, std::vector<gggs::GridIndex>> by_parent;
  for (const gggs::GridIndex & child : gridsInDir(src_dir, child_level)) {
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

void usage()
{
  std::cerr <<
    "usage: build_sidescan_overviews <layer_dir> [--fine-level N] [--min-level N]\n"
    "  Regenerates <layer_dir>/overviews/ (deletes it first; idempotent).\n"
    "  --fine-level N  the layer's native GGGS level (default 13)\n"
    "  --min-level N   coarsest level to build, 0 = apex (default 0)\n";
}

}  // namespace

int main(int argc, char ** argv)
{
  std::string layer_dir;
  int fine_level = 13;
  int min_level = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--fine-level" && i + 1 < argc) {
      fine_level = std::atoi(argv[++i]);
    } else if (arg == "--min-level" && i + 1 < argc) {
      min_level = std::atoi(argv[++i]);
    } else if (arg == "--help" || arg == "-h") {
      usage();
      return 0;
    } else if (layer_dir.empty() && arg[0] != '-') {
      layer_dir = arg;
    } else {
      usage();
      return 2;
    }
  }
  if (layer_dir.empty() || fine_level <= 0 || fine_level > 20 ||
    min_level < 0 || min_level >= fine_level)
  {
    usage();
    return 2;
  }
  if (!fs::is_directory(layer_dir)) {
    std::cerr << "error: not a directory: " << layer_dir << "\n";
    return 1;
  }

  const fs::path overviews = fs::path(layer_dir) / "overviews";
  // Regenerable-cache semantics: rebuild the whole sidecar every run.
  fs::remove_all(overviews);
  fs::create_directories(overviews);

  // The finest overview level folds the fine layer itself; every subsequent
  // level folds the sidecar level just written.
  std::size_t total = 0;
  for (int level = fine_level; level > min_level; --level) {
    const fs::path src = (level == fine_level) ? fs::path(layer_dir) : overviews;
    const std::size_t written =
      buildLevel(src, overviews, static_cast<uint8_t>(level));
    std::cerr << "level " << level << " -> " << (level - 1) << ": " << written <<
      " overview tile(s)\n";
    total += written;
    if (written == 0) {
      break;
    }
  }
  std::cerr << "overview pyramid complete: " << total << " tile(s) in " <<
    overviews.string() << "\n";
  return 0;
}
