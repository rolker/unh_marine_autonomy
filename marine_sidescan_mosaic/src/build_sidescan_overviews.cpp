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

// [#188 / ADR-0011] Batch overview-pyramid builder CLI. A thin shell over
// marine_sidescan_mosaic::{parseOverviewArgs, buildOverviewPyramid} — the
// production path lives (and is tested) in overview_pyramid.cpp.

#include <exception>
#include <iostream>
#include <string>

#include "marine_sidescan_mosaic/overview_pyramid.hpp"

namespace
{

void usage()
{
  std::cerr <<
    "usage: build_sidescan_overviews <layer_dir> [--fine-level N] [--min-level N]\n"
    "                                 [--dry-run]\n"
    "  Rebuilds <layer_dir>/overviews/ from the layer's fine tiles. The rebuild\n"
    "  is WHOLESALE: on success the existing sidecar is DISCARDED, not merged\n"
    "  (overviews are a derived, regenerable cache). Crash-safe — the previous\n"
    "  sidecar is only dropped once the new one is complete and in place.\n"
    "  --fine-level N  the layer's native GGGS level (default 13)\n"
    "  --min-level N   coarsest level to build, 0 = apex (default 0)\n"
    "  --dry-run       run the layer checks and report; write nothing\n";
}

}  // namespace

int main(int argc, char ** argv)
{
  namespace msm = marine_sidescan_mosaic;

  msm::OverviewOptions opts;
  switch (msm::parseOverviewArgs(argc, argv, opts)) {
    case msm::ArgStatus::kHelp:
      usage();
      return 0;
    case msm::ArgStatus::kError:
      usage();
      return 2;
    case msm::ArgStatus::kOk:
      break;
  }

  try {
    const msm::OverviewBuildResult result =
      msm::buildOverviewPyramid(opts, &std::cerr);
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
    // Report the refusals before any success line: on early_empty or a skip the
    // swap never happened (sidecar_replaced stays false), so "complete" would be
    // a lie next to the error.
    if (result.early_empty) {
      std::cerr << "error: a level above --min-level " << opts.min_level <<
        " produced no tiles — the fine-tile chain is broken; overviews/ left "
        "unchanged\n";
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
      " tile(s) written\n";
    return 0;
  } catch (const std::exception & e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
