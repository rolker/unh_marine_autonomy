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

// [uma#397 / docs/world_store_design.md §7, Part 4] Per-parent multi-band
// overview builder CLI — the work unit a Snakemake DAG invokes.
//
// A thin shell over marine_bathymetry_store::buildMultiBandDepthOverviewParent;
// the production path lives (and is tested) in overview_pyramid.cpp. This is
// the rev-3 counterpart to `build_depth_overviews`: that one re-folds a WHOLE
// layer to refresh anything, which is why the prototype found the batch call
// could not benefit from incremental regeneration. One tile per invocation lets
// the DAG decide what is stale.
//
// There is no --sigma-fold flag, deliberately. §7's σ rule is open, and a flag
// would make choosing it an operator decision taken one invocation at a time.
// Until the rule is decided the fourth band is written as nodata and every tile
// records `sigma_fold: undecided`.

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/overview_pyramid.hpp"

namespace
{

void usage()
{
  std::cerr <<
    "usage: build_depth_overview_parent <layer_dir> <level> <row> <col>\n"
    "  Folds ONE parent tile at <level>_<row>_<col> from its up-to-four children\n"
    "  and writes it to <layer_dir>/overviews/ as a 4-band MIN/MEAN/COUNT/sigma\n"
    "  tile (the rev-3 world store schema; docs/world_store_design.md section 7).\n"
    "  <layer_dir> is a rev-3 quantity layer, e.g. <store root>/depths/<state>/\n"
    "  <origin>/ — NOT a draft/processed/reference/chart layer, whose pyramid is\n"
    "  the 2-band one build_depth_overviews writes.\n"
    "  Children are the native tiles in <layer_dir> (2-band, promoted on read)\n"
    "  and the derived tiles already in overviews/. A parent already covered by a\n"
    "  NATIVE tile is left alone: native data wins on disk. A DERIVED tile at the\n"
    "  parent's index that a native tile now covers, or whose children are all\n"
    "  gone, is removed with its record (reported on stderr).\n"
    "  The sigma band is RESERVED and written as nodata — section 7's fold rule\n"
    "  is not decided yet, and each tile records `sigma_fold: undecided` beside\n"
    "  it so the later decision is a new fingerprint, not a migration.\n"
    "  Writes are per-tile atomic (write-beside then rename), so many of these\n"
    "  may run concurrently over one layer.\n"
    "  Exit: 0 written, 0 also when suppressed by native or no child exists\n"
    "        (both reported on stderr), 2 usage, 1 failure.\n"
    "\n"
    "usage: build_depth_overview_parent --list-parents <layer_dir> <level>\n"
    "  Prints, one parent per line, the parents at <level> that have at least\n"
    "  one child and are not already covered by a native tile, as\n"
    "  `<level>_<row>_<col>` followed by a TAB-separated list of the children\n"
    "  it folds, relative to <layer_dir> (`<name>` native, `overviews/<name>`\n"
    "  derived) — what a Snakemake DAG enumerates before it can schedule\n"
    "  anything, and what it uses as each job's inputs. It is here rather than\n"
    "  in the rules because the parent/child mapping is GGGS, whose column\n"
    "  counts vary by latitude band; a second implementation of that\n"
    "  arithmetic would be a second thing to get wrong.\n"
    "\n"
    "usage: build_depth_overview_parent --prune <layer_dir> <level>\n"
    "  Removes the derived tiles at <level> that describe nothing any more — a\n"
    "  native tile now covers the index, or no child of it exists — with their\n"
    "  records and .fp sidecars, printing each removed `<level>_<row>_<col>`.\n"
    "  A per-parent DAG only schedules parents that have children, so nothing\n"
    "  else would ever remove one. Run it once the level below is final.\n"
    "\n"
    "usage: build_depth_overview_parent --remove-level <layer_dir> <level>\n"
    "  Removes EVERY derived tile at <level> (with records and .fp sidecars),\n"
    "  printing each: for a level the regenerate no longer builds (outside its\n"
    "  configured min_level..fine_level-1). Native tiles are never touched.\n";
}

// Strict unsigned parse: an empty, negative, non-numeric or trailing-garbage
// value must be a usage error, not a silent 0 — the same discipline
// parseDepthOverviewArgs applies to --min-level.
bool parseU32(const char * text, uint32_t & out)
{
  if (text == nullptr || text[0] == '\0' || text[0] == '-') {
    return false;
  }
  try {
    std::size_t used = 0;
    const unsigned long value = std::stoul(text, &used);   // NOLINT(runtime/int)
    if (text[used] != '\0' || value > UINT32_MAX) {
      return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

std::string gridName(const gggs::GridIndex & grid)
{
  return std::to_string(static_cast<int>(grid.level())) + "_" +
         std::to_string(grid.row()) + "_" + std::to_string(grid.column());
}

}  // namespace

int main(int argc, char ** argv)
{
  namespace mbs = marine_bathymetry_store;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      usage();
      return 0;
    }
  }
  if (argc == 4 && std::string(argv[1]) == "--list-parents") {
    uint32_t list_level = 0;
    if (!parseU32(argv[3], list_level)) {
      usage();
      return 2;
    }
    try {
      for (const mbs::MultiBandOverviewParent & entry :
        mbs::listMultiBandOverviewParentInputs(
          argv[2], static_cast<int>(list_level)))
      {
        std::cout << gridName(entry.parent);
        for (const std::string & child : entry.children) {
          std::cout << "\t" << child;
        }
        std::cout << "\n";
      }
      return 0;
    } catch (const std::exception & e) {
      std::cerr << "error: " << e.what() << "\n";
      return 1;
    }
  }
  if (argc == 4 &&
    (std::string(argv[1]) == "--prune" || std::string(argv[1]) == "--remove-level"))
  {
    uint32_t prune_level = 0;
    if (!parseU32(argv[3], prune_level)) {
      usage();
      return 2;
    }
    const bool everything = std::string(argv[1]) == "--remove-level";
    try {
      for (const gggs::GridIndex & removed :
        everything ?
        mbs::removeMultiBandOverviewLevel(argv[2], static_cast<int>(prune_level)) :
        mbs::pruneMultiBandOverviewLevel(argv[2], static_cast<int>(prune_level)))
      {
        std::cout << gridName(removed) << "\n";
      }
      return 0;
    } catch (const std::exception & e) {
      std::cerr << "error: " << e.what() << "\n";
      return 1;
    }
  }
  if (argc != 5) {
    usage();
    return 2;
  }
  uint32_t level = 0, row = 0, col = 0;
  if (!parseU32(argv[2], level) || !parseU32(argv[3], row) ||
    !parseU32(argv[4], col))
  {
    usage();
    return 2;
  }

  try {
    const mbs::MultiBandParentResult result =
      mbs::buildMultiBandDepthOverviewParent(
      argv[1], static_cast<int>(level), row, col);
    const char * removed = result.removed_stale ?
      "; the stale derived tile there was removed" : "";
    if (result.suppressed_by_native) {
      std::cerr << "parent " << level << "_" << row << "_" << col <<
        ": left to the native tile already at that index; nothing written" <<
        removed << "\n";
      return 0;
    }
    if (!result.written) {
      std::cerr << "parent " << level << "_" << row << "_" << col <<
        ": no child tile exists; nothing written" << removed << "\n";
      return 0;
    }
    std::cerr << "parent " << level << "_" << row << "_" << col << ": written "
      "from " << result.children_used << " child tile(s), geometric error " <<
      result.geometric_error_m << " m, sigma band nodata (sigma_fold: " <<
      mbs::sigmaFoldName(mbs::SigmaFold::kUndecided) << ")\n";
    return 0;
  } catch (const std::exception & e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
