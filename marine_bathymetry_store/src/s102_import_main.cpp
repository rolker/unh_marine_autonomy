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
/// @brief CLI: fetch NOAA S-102 tiles for an area and import them into a
/// store layer (#278: discover → SHA256-verified fetch → warp + per-cell
/// MLLW→ellipsoid convert → multi-level import).
///
/// SCRATCH STORES ONLY until uma#276 lands (ADR-0010 D7 precondition) — see
/// s102/run.hpp. Never point --store at a store a live costmap reads.

#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include "s102/run.hpp"
#include "s102/vdatum_provider.hpp"

namespace
{

void usage()
{
  std::cout <<
    "usage: s102_import --area minLon,minLat,maxLon,maxLat\n"
    "                   --store <dir> --cache <dir>\n"
    "                   --datum <constant:<mllw_z_m>|vdatum>\n"
    "                   [--layer survey|reference]   (default reference)\n"
    "                   [--catalog <url|path>]       (default: newest in bucket)\n"
    "                   [--geoid <path>]             (vdatum: geoid grid)\n"
    "                   [--vdatum-grids <dir>]       (vdatum: .gtx directory)\n"
    "                   [--datum-config <path>]      (vdatum: polygon config)\n"
    "                   [--cell-size m]              (store default, 0.5)\n"
    "                   [--offline] [--force]\n"
    "\n"
    "Fetches NOAA S-102 ed3.0.0 tiles intersecting --area into --cache\n"
    "(SHA256-verified, idempotent), converts them to store convention\n"
    "(geographic WGS84, per-cell MLLW->ellipsoid via --datum), and imports\n"
    "each at the GGGS level matching its native resolution.\n"
    "\n"
    "--datum constant:<m>  fixed MLLW ellipsoidal height (scratch use only)\n"
    "--datum vdatum        marine_vertical_datum grids (--geoid/--vdatum-grids)\n"
    "--offline             cache only, never touch the network\n"
    "--force               re-import tiles even at unchanged issuance\n"
    "\n"
    "SCRATCH STORES ONLY until uma#276 lands: the current costmap layer\n"
    "treats high-uncertainty cells as reject->LETHAL; chart-grade sigma\n"
    "under a live costmap would render charted regions keepout.\n";
  exit(1);
}

}  // namespace

int main(int argc, char * argv[])
{
  namespace s102 = marine_bathymetry_store::s102;

  s102::S102ImportOptions options;
  std::string area_text;
  std::string layer_text = "reference";
  std::string datum_text;
  s102::VDatumProviderConfig vdatum_config;

  const auto need_arg = [&](int & i) -> const char * {
      if (i + 1 >= argc) {
        usage();
      }
      return argv[++i];
    };

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--area") == 0) {
      area_text = need_arg(i);
    } else if (std::strcmp(argv[i], "--store") == 0) {
      options.store_dir = need_arg(i);
    } else if (std::strcmp(argv[i], "--cache") == 0) {
      options.cache_dir = need_arg(i);
    } else if (std::strcmp(argv[i], "--layer") == 0) {
      layer_text = need_arg(i);
    } else if (std::strcmp(argv[i], "--catalog") == 0) {
      options.catalog = need_arg(i);
    } else if (std::strcmp(argv[i], "--datum") == 0) {
      datum_text = need_arg(i);
    } else if (std::strcmp(argv[i], "--geoid") == 0) {
      vdatum_config.geoid_grid = need_arg(i);
    } else if (std::strcmp(argv[i], "--vdatum-grids") == 0) {
      vdatum_config.vdatum_grid_dir = need_arg(i);
    } else if (std::strcmp(argv[i], "--datum-config") == 0) {
      vdatum_config.datum_config_path = need_arg(i);
    } else if (std::strcmp(argv[i], "--cell-size") == 0) {
      const char * val = need_arg(i);
      try {
        options.cell_size = std::stod(val);
      } catch (const std::exception &) {
        // std::stod throws on non-numeric input; this parse sits outside the
        // main try block below, so catch here rather than abort via terminate.
        std::cerr << "bad --cell-size '" << val << "'\n";
        return 1;
      }
      if (!std::isfinite(options.cell_size) || options.cell_size <= 0.0) {
        // std::stod happily parses "0"/"-1"/"nan"/"inf"; reject them here.
        // A non-finite or non-positive cell size flows into
        // gggs::Level::fromCellSize() where std::log2(<=0) yields inf/NaN and
        // the subsequent static_cast<int> is undefined behavior. Mirrors the
        // --area finiteness guard below.
        std::cerr << "bad --cell-size '" << val <<
          "' (want a positive finite number)\n";
        return 1;
      }
    } else if (std::strcmp(argv[i], "--offline") == 0) {
      options.offline = true;
    } else if (std::strcmp(argv[i], "--force") == 0) {
      options.force = true;
    } else {
      usage();
    }
  }
  if (area_text.empty() || options.store_dir.empty() ||
    options.cache_dir.empty() || datum_text.empty())
  {
    usage();
  }

  {
    std::istringstream in(area_text);
    char c1 = 0, c2 = 0, c3 = 0, trailing = 0;
    in >> options.area.min_lon >> c1 >> options.area.min_lat >> c2 >>
    options.area.max_lon >> c3 >> options.area.max_lat;
    const bool parsed = static_cast<bool>(in);
    // Any non-whitespace after max_lat is garbage (e.g. a stray 5th field).
    const bool has_trailing = static_cast<bool>(in >> trailing);
    if (!parsed || has_trailing || c1 != ',' || c2 != ',' || c3 != ',' ||
      !std::isfinite(options.area.min_lon) ||
      !std::isfinite(options.area.min_lat) ||
      !std::isfinite(options.area.max_lon) ||
      !std::isfinite(options.area.max_lat) ||
      options.area.min_lon >= options.area.max_lon ||
      options.area.min_lat >= options.area.max_lat)
    {
      // Reject NaN/inf too: a NaN would slip through the min<max guard (all
      // comparisons false) into the spatial filter as an unbounded query.
      std::cerr << "bad --area '" << area_text <<
        "' (want minLon,minLat,maxLon,maxLat, finite, min < max)\n";
      return 1;
    }
  }

  if (layer_text == "survey") {
    options.layer = marine_bathymetry_store::SourceLayer::Survey;
  } else if (layer_text == "reference") {
    options.layer = marine_bathymetry_store::SourceLayer::Reference;
  } else {
    std::cerr << "unknown --layer '" << layer_text <<
      "' (expected survey|reference)\n";
    return 1;
  }

  std::unique_ptr<s102::VerticalDatumProvider> datum;
  if (datum_text.rfind("constant:", 0) == 0) {
    try {
      datum = std::make_unique<s102::ConstantOffsetProvider>(
        std::stod(datum_text.substr(9)));
    } catch (const std::exception &) {
      std::cerr << "bad --datum '" << datum_text << "'\n";
      return 1;
    }
  } else if (datum_text == "vdatum") {
    if (vdatum_config.geoid_grid.empty() ||
      vdatum_config.vdatum_grid_dir.empty())
    {
      std::cerr << "--datum vdatum needs --geoid and --vdatum-grids\n";
      return 1;
    }
    try {
      datum = std::make_unique<s102::MarineVerticalDatumProvider>(vdatum_config);
    } catch (const std::exception & e) {
      // The vdatum ctor throws on grid-setup failure and load_datum_config can
      // throw on a missing/mistyped --geoid/--vdatum-grids/--datum-config; this
      // sits outside the main try below, so catch here (matching the --cell-size
      // and constant: paths) rather than abort via std::terminate.
      std::cerr << "bad --datum vdatum: " << e.what() << "\n";
      return 1;
    }
  } else {
    std::cerr << "unknown --datum '" << datum_text <<
      "' (expected constant:<m> or vdatum)\n";
    return 1;
  }
  options.datum = datum.get();

  try {
    const auto summary = s102::runS102Import(options);
    std::cout << "s102: done — " << summary.discovered << " discovered, " <<
      summary.fetched << " fetched, " << summary.skipped << " skipped, " <<
      summary.converted << " converted, " << summary.imported_cells <<
      " cell(s) imported\n";
  } catch (const std::exception & e) {
    std::cerr << "s102_import: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
