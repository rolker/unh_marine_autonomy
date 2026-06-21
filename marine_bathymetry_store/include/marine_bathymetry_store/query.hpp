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

#include <cstdint>
#include <functional>
#include <optional>

#include "geographic_msgs/msg/geo_point.hpp"
#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathy_cell.hpp"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/epoch.hpp"

namespace marine_bathymetry_store
{

/// @brief A depth value resolved from the store, tagged with its winning layer.
///
/// `depth` is **ellipsoidal height** (WGS84, metres, up-positive): a seafloor
/// below the ellipsoid has a negative value, and a *shallower* seafloor has a
/// *greater* (less negative) value. See `shallowestReliable`.
struct DepthSample
{
  double depth;            ///< Ellipsoidal height (WGS84, m, up-positive).
  double uncertainty;      ///< 1-sigma vertical uncertainty (m).
  int64_t timestamp;       ///< Acquisition / import time (ns since the Unix epoch).
  SourceLayer source;      ///< Which layer this value came from.
  /// Per-cell registry source index (ADR-0005 D2/D8): which platform/sensor
  /// contributed this value. 0 = unset (pre-migration / single-platform data).
  /// The navigation-safety query (`shallowestReliable`) ignores this axis
  /// (ADR-0005 D5 carve-out); it is exposed so provenance-aware consumers can
  /// resolve the record off a query result.
  uint16_t source_index = 0;
  /// GGGS level of the cell this value was resolved at. The store is
  /// multi-level (ADR-0002 §D2); the query resolves the best-available level
  /// per cell, so a single region scan can return samples at mixed levels.
  uint8_t level = 0;
  /// Which epoch within the layer this value came from (ADR-0002 A1). Queries
  /// resolve a layer newest-epoch-first; this records the winning epoch.
  /// (Provenance — live-fused vs replayed — is a property of the epoch's
  /// tiles, not of an individual sample.)
  Epoch epoch;
};

/// @brief Best-available depth at @p cell across layers, levels **and epochs**.
///
/// Walks layers in `source_layers_by_priority` order; within a layer it walks
/// epochs **newest-first** (ADR-0002 §A1.3 default resolution) and, within each
/// epoch, resolves the **finest GGGS level present** that has data at the query
/// position (the store is multi-level, ADR-0002 §D2). Returns the first
/// (layer, epoch, level) that resolves. `std::nullopt` means **unknown** — no
/// layer has data here; a safety-conscious caller must treat that as not-safe
/// (ADR-0002 §D7), not as deep water.
std::optional<DepthSample> bestSource(
  const BathymetryStore & store, const gggs::CellIndex & cell);

/// @brief Shallowest reliable depth at @p cell (navigation-safety query).
///
/// Within each layer, walks epochs **newest-first** and takes the first value
/// passing the reliability gate (uncertainty ≤ @p max_uncertainty; a NaN
/// uncertainty is never reliable) — the ADR-0002 §A1.3 safety walk: a fresh
/// noisy pass falls through to a prior epoch's confident value, with no
/// cross-epoch fusion. Across all levels present and all layers, returns the
/// **shallowest** — the greatest ellipsoidal height, the value closest to the
/// surface and therefore the most hazardous. `std::nullopt` means no reliable
/// data covers the cell; the caller must treat that as not-safe.
std::optional<DepthSample> shallowestReliable(
  const BathymetryStore & store, const gggs::CellIndex & cell, double max_uncertainty);

/// @brief Visit every GGGS cell overlapping the geographic box, with its best source.
///
/// Iterates the cells covered by the lat/lon box [@p minimum, @p maximum] at the
/// store's **default** level and invokes @p visitor with each `CellIndex` and
/// its `bestSource` result (`std::nullopt` = unknown cell). The per-cell
/// resolution is still multi-level (the returned `DepthSample.level` may differ
/// from the iteration level); only the iteration granularity is the default
/// level. Region form of `bestSource`; work is bounded by the requested box, so
/// callers control cost.
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
/// usable data, passing the two records (@p epoch_a's, then @p epoch_b's). The
/// visitor owns the significance test — e.g. comparing `|b.depth - a.depth|`
/// against the combined uncertainty. Cells observed in only one epoch are
/// *coverage* change, not depth change, and are not visited. Only grids at the
/// **same level** in both epochs are compared (a cell has no cross-level
/// counterpart).
/// @return The number of cells visited.
std::size_t forEachChangedCell(
  const BathymetryStore & store, SourceLayer layer,
  const Epoch & epoch_a, const Epoch & epoch_b,
  const std::function<void(const gggs::CellIndex &,
  const BathyCell &, const BathyCell &)> & visitor);

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__QUERY_HPP_
