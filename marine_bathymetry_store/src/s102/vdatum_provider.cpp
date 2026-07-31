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

#include "s102/vdatum_provider.hpp"

#include <iostream>
#include <stdexcept>

namespace marine_bathymetry_store::s102
{

MarineVerticalDatumProvider::MarineVerticalDatumProvider(
  const VDatumProviderConfig & config)
{
  marine_vertical_datum::VDatumConfig vdatum_config;
  vdatum_config.geoid_grid = config.geoid_grid;
  vdatum_config.vdatum_grid_dir = config.vdatum_grid_dir;
  query_ = marine_vertical_datum::make_vdatum_query(
    vdatum_config,
    [](const std::string & msg) {std::cerr << "s102: vdatum: " << msg << "\n";});
  // make_vdatum_query returns an EMPTY std::function on grid-setup failure
  // (missing/unreadable grids, PROJ pipeline error — see the reason on stderr
  // above). Left unchecked, mllwHeight would return nullopt for every cell and
  // the whole import would silently become all-nodata while exiting 0. Fail
  // loud instead (no-silent-failure): a bad --geoid/--vdatum-grids is an
  // operator error, not a per-cell coverage gap.
  if (!query_) {
    throw std::runtime_error(
            "s102: vertical-datum grid setup failed (check --geoid / "
            "--vdatum-grids; see 's102: vdatum:' diagnostics above)");
  }
  if (!config.datum_config_path.empty()) {
    entries_ = marine_vertical_datum::load_datum_config(config.datum_config_path);
  }
}

std::optional<double> MarineVerticalDatumProvider::mllwHeight(
  double lat, double lon) const
{
  const auto vdatum = query_ ? query_(lat, lon) : std::nullopt;
  const auto result = marine_vertical_datum::resolve_datum(
    lat, lon, std::nullopt, std::nullopt, vdatum, entries_);
  if (!result.has_value()) {
    return std::nullopt;
  }
  return result->chart_datum_z;
}

}  // namespace marine_bathymetry_store::s102
