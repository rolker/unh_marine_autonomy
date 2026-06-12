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
#include "marine_bathymetry_store/epoch.hpp"

namespace marine_bathymetry_store
{

/// @brief A depth value resolved from the store, tagged with its provenance.
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
  Epoch epoch;           ///< Which epoch within that layer (ADR-0002 A1).
};

/// @brief Highest-priority layer that has data at @p cell; within a layer, the
///        **newest epoch** with data (ADR-0002 §A1.3 default query).
///
/// Walks layers in `source_layers_by_priority` order; within each layer, walks
/// epochs newest-first and takes the first with usable data. Returns the first
/// layer that resolves. `std::nullopt` means **unknown** — no layer has data
/// here; a safety-conscious caller must treat that as not-safe (ADR-0002 §D7),
/// not as deep water.
std::optional<DepthSample> bestSource(
  const BathymetryStore & store, const gggs::CellIndex & cell);

/// @brief Shallowest reliable depth at @p cell (navigation-safety query).
///
/// Within each layer, walks epochs **newest-first and takes the first value
/// passing the reliability gate** — uncertainty ≤ @p max_uncertainty (a NaN
/// uncertainty is never reliable). This is the ADR-0002 §A1.3 walk: a fresh
/// noisy pass fails the gate and falls through to the prior epoch's confident
/// value, so a recently observed shoal keeps protecting navigation without any
/// cross-epoch fusion. Among the layers that resolve, returns the
/// **shallowest** — the greatest ellipsoidal height, the value closest to the
/// surface and therefore the most hazardous. `std::nullopt` means no reliable
/// data covers the cell; the caller must treat that as not-safe.
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

/// @brief Visit every cell observed in **both** epochs of @p layer (change map).
///
/// The reason epochs exist (ADR-0002 §A1.1): differencing two days locates
/// change instead of averaging it away. Iterates the grids present in both
/// epochs' tile maps and invokes @p visitor for each cell where both have
/// usable data, passing the two records (@p epoch_a's, then @p epoch_b's).
/// The visitor owns the significance test — e.g. comparing
/// `|b.depth - a.depth|` against the combined uncertainty.
/// Cells observed in only one epoch are *coverage* change, not depth change,
/// and are not visited.
/// @return The number of cells visited.
std::size_t forEachChangedCell(
  const BathymetryStore & store, SourceLayer layer,
  const Epoch & epoch_a, const Epoch & epoch_b,
  const std::function<void(const gggs::CellIndex &,
  const BathyCell &, const BathyCell &)> & visitor);

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__QUERY_HPP_
