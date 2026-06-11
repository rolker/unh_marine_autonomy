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

#ifndef MARINE_BATHYMETRY_STORE__QUERY_HPP_
#define MARINE_BATHYMETRY_STORE__QUERY_HPP_

#include <functional>
#include <optional>

#include "geographic_msgs/msg/geo_point.hpp"
#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathy_cell.hpp"
#include "marine_bathymetry_store/bathymetry_store.hpp"

namespace marine_bathymetry_store
{

/// @brief A depth value resolved from the store, tagged with its winning layer.
///
/// `depth` is **ellipsoidal height** (WGS84, metres, up-positive): a seafloor
/// below the ellipsoid has a negative value, and a *shallower* seafloor has a
/// *greater* (less negative) value. See `shallowestReliable`.
struct DepthSample
{
  double depth;          ///< Ellipsoidal height (WGS84, m, up-positive).
  double uncertainty;    ///< 1-sigma vertical uncertainty (m).
  double timestamp;      ///< Acquisition / import time (Unix seconds).
  SourceLayer source;    ///< Which layer this value came from.
};

/// @brief Highest-priority layer that has data at @p cell.
///
/// Walks layers in `source_layers_by_priority` order and returns the first with
/// usable data. `std::nullopt` means **unknown** — no layer has data here; a
/// safety-conscious caller must treat that as not-safe (ADR-0002 §D7), not as
/// deep water.
std::optional<DepthSample> bestSource(
  const BathymetryStore & store, const gggs::CellIndex & cell);

/// @brief Shallowest reliable depth at @p cell (navigation-safety query).
///
/// Among all layers that (a) have data and (b) have uncertainty ≤
/// @p max_uncertainty (a NaN uncertainty is never reliable), returns the
/// **shallowest** — i.e. the greatest ellipsoidal height, the value closest to
/// the surface and therefore the most hazardous. `std::nullopt` means no
/// reliable layer covers the cell; the caller must treat that as not-safe.
std::optional<DepthSample> shallowestReliable(
  const BathymetryStore & store, const gggs::CellIndex & cell, double max_uncertainty);

/// @brief Visit every GGGS cell overlapping the geographic box, with its best source.
///
/// Iterates the cells covered by the lat/lon box [@p minimum, @p maximum] at the
/// store's level and invokes @p visitor with each `CellIndex` and its
/// `bestSource` result (`std::nullopt` = unknown cell). Region form of
/// `bestSource`; work is bounded by the requested box, so callers control cost.
void forEachCellBestSource(
  const BathymetryStore & store,
  const geographic_msgs::msg::GeoPoint & minimum,
  const geographic_msgs::msg::GeoPoint & maximum,
  const std::function<void(const gggs::CellIndex &,
  const std::optional<DepthSample> &)> & visitor);

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__QUERY_HPP_
