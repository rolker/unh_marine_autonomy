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

// [uma-ADR-0010 D9 / uma-ADR-0011 / uma-ADR-0013] Batch depth overview-pyramid
// builder CLI. A thin shell over
// marine_bathymetry_store::{parseDepthOverviewArgs, buildDepthOverviewPyramid}
// — the production path lives (and is tested) in overview_pyramid.cpp.

#include <exception>
#include <iostream>
#include <string>

#include "marine_bathymetry_store/overview_pyramid.hpp"

namespace
{

void usage()
{
  std::cerr <<
    "usage: build_depth_overviews <layer_dir> [--min-level N] [--dry-run]\n"
    "  Rebuilds <layer_dir>/overviews/ from a depth layer's native tiles, folding\n"
    "  shallowest-preserving (the shoalest child's {depth, uncertainty} pair\n"
    "  survives each downsample — uma-ADR-0010 D9). <layer_dir> must be a\n"
    "  `draft/`, `processed/` or `reference/` depth layer; `chart` gets no\n"
    "  generated pyramid (it has the ENC scale ladder).\n"
    "  The layer's native levels are DISCOVERED — a mixed-level layer folds from\n"
    "  its finest native level toward the apex, writing a derived tile only where\n"
    "  no native tile exists. Native data always wins on disk; a display that\n"
    "  composites levels still draws the finer derived tile over a coarser native\n"
    "  one, which is intended.\n"
    "  The rebuild is WHOLESALE: on success the existing sidecar is DISCARDED, not\n"
    "  merged (overviews are a derived, regenerable cache). Crash-safe — the\n"
    "  previous sidecar is only dropped once the new one is complete and in place.\n"
    "  --min-level N   coarsest level to build, 0 = apex (default 0); must be\n"
    "                  below the layer's finest discovered native level\n"
    "  --dry-run       run the layer checks and report the discovered per-level\n"
    "                  native coverage; write nothing\n";
}

}  // namespace

int main(int argc, char ** argv)
{
  namespace mbs = marine_bathymetry_store;

  mbs::DepthOverviewOptions opts;
  switch (mbs::parseDepthOverviewArgs(argc, argv, opts)) {
    case mbs::DepthArgStatus::kHelp:
      usage();
      return 0;
    case mbs::DepthArgStatus::kError:
      usage();
      return 2;
    case mbs::DepthArgStatus::kOk:
      break;
  }

  try {
    const mbs::DepthOverviewBuildResult result =
      mbs::buildDepthOverviewPyramid(opts, &std::cerr);
    if (opts.dry_run) {
      if (result.tiles_skipped > 0) {
        std::cerr << "dry run: " << result.tiles_skipped <<
          " tile(s) would fail grid reconstruction, so the rebuild would be "
          "refused; fix them first\n";
        return 4;
      }
      std::cerr << "dry run: nothing written\n";
      return 0;
    }
    // Report the refusals before any success line: on level_uncovered or a skip
    // the swap never happened (sidecar_replaced stays false), so "complete" would
    // be a lie next to the error.
    if (result.level_uncovered) {
      std::cerr << "error: level " << result.uncovered_level <<
        " (above --min-level " << opts.min_level << ") has no coverage at all — "
        "no derived tile was written there and no native tile is present; the "
        "tile chain is broken. overviews/ left unchanged\n";
      return 3;
    }
    if (result.tiles_skipped > 0) {
      std::cerr << "error: " << result.tiles_skipped <<
        " input tile(s) failed grid reconstruction, so the pyramid is missing "
        "their coverage — refusing to replace overviews/ with a partial "
        "pyramid; overviews/ left unchanged\n";
      return 4;
    }
    // Success: the swap happened (result.sidecar_replaced is true here).
    std::cerr << "overview pyramid complete: " << result.tiles_written <<
      " tile(s) written across levels " << result.coarsest_level << ".." <<
      (result.native_levels.empty() ? 0 : result.native_levels.back() - 1) <<
      "; " << result.tiles_suppressed_by_native <<
      " parent(s) left to native tiles\n";
    return 0;
  } catch (const std::exception & e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
