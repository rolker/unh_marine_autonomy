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

#ifndef MARINE_BATHYMETRY_STORE__BATHY_CELL_HPP_
#define MARINE_BATHYMETRY_STORE__BATHY_CELL_HPP_

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace marine_bathymetry_store
{

/// @brief Source layers, ordered by query priority (highest first).
///
/// A cell's source is implied by which layer holds it (the store keeps one
/// tile map per layer), so it is not stored in the per-cell record. This is a
/// deliberate, cleaner realization of ADR-0002 §D3's "each cell stores a
/// source layer": the layer is the map it lives in.
///
/// #248 (ADR-0002 Amendment A2.1) collapsed the old `chart`/`draft`/`processed`
/// classes into **two** layers — `Survey` (highest priority) and `Reference`
/// (the read-only prior). #275 then reintroduced `Chart` as a **third**, distinct
/// layer for official navigation products (S57 exports; ADR-0010 D3/D7) — not the
/// pre-#248 `chart` class, and not a generalization of `reference`. The numeric
/// value is the priority rank (0 = highest), so the taxonomy is now three layers
/// ordered `Survey 0 > Reference 1 > Chart 2`. `Chart` is **lowest** priority — a
/// D4 placeholder ordering pending the #276 cost-model rework — so best-source
/// falls through Survey, then Reference, then Chart where no higher layer holds
/// data. Both `Reference` and `Chart` are write-gated priors that live CUBE
/// ingest can never clobber: `Reference` is read-only (see `BathymetryStore::set`
/// and the `reference_writable` flag the importer opts into); `Chart` is writable
/// only via the wholesale-regeneration swap (`replaceChartLayer`), gated by
/// `chart_staging_writable`.
enum class SourceLayer : uint8_t
{
  Survey = 0,         ///< The CUBE product (live on-boat or off-boat re-run). Highest
                      ///< priority. Subsumes the pre-#248 draft/processed distinction
                      ///< (one fused surface per layer since #221).
  Reference = 1,  ///< A prior surface imported before the survey (chart-derived
                  ///< contour prior, external processed grid). Broad, coarse,
                  ///< read-only. Ellipsoidal heights converted at import (§D4).
  Chart = 2,      ///< Official navigation products (S57 exports; ADR-0010 D3/D7).
                  ///< Lowest priority (D4 placeholder ordering). Writable ONLY via
                  ///< the wholesale-regeneration path (`replaceChartLayer` swap of
                  ///< a staged directory) — never by cell-wise ingest; a staging
                  ///< store opts in with `chart_staging_writable=true`.
};

/// @brief Source layers in descending priority order — iterate for best-source.
inline constexpr std::array<SourceLayer, 3> source_layers_by_priority{
  SourceLayer::Survey, SourceLayer::Reference, SourceLayer::Chart};

/// @brief Number of source layers present in this phase.
inline constexpr std::size_t source_layer_count = source_layers_by_priority.size();

/// @brief Per-cell bathymetric record.
///
/// `depth` and `uncertainty` are `double` — a single 2-band `Float64` value tile
/// per grid (ADR-0002 §D5 as simplified by #248 / Amendment A2.2). The per-cell
/// `timestamp` and `source_index` of the pre-#248 format were dropped: the store
/// is a regenerable cache over raw bags, the per-cell time band's only in-tree
/// consumer (the `bathymetry_layer` staleness gate) was retired, and per-cell
/// source provenance is a constant for the single-platform deployment (coarse
/// provenance now lives in the store-level `registry.json` `StoreMetadata`,
/// ADR-0005 #248 amendment). The on-disk layout is one tile per grid —
/// `<grid>.tif`; see `BathymetryTile` and `tile_io`.
struct BathyCell
{
  /// Ellipsoidal height (WGS84), metres. NaN = no data.
  double depth = std::numeric_limits<double>::quiet_NaN();
  /// 1-sigma vertical uncertainty, metres. NaN = unknown.
  double uncertainty = std::numeric_limits<double>::quiet_NaN();

  /// @brief True if this cell carries a usable depth (depth is not NaN).
  bool hasData() const noexcept
  {
    return !std::isnan(depth);
  }
};

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__BATHY_CELL_HPP_
