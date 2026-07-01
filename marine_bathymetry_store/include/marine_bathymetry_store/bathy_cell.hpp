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
/// The taxonomy was simplified to **two** layers in #248 (ADR-0002 Amendment
/// A2.1): the old `chart`/`draft`/`processed` collapse to `cube` (highest
/// priority) and `pre-existing` (the read-only prior). The numeric value is the
/// priority rank (0 = highest). `PreExisting` (any prior surface imported before
/// the survey — a chart-derived contour prior, an external processed grid) is
/// lowest priority: best-source falls through to it where no CUBE data exists.
/// The store treats `PreExisting` as a **read-only prior** so live CUBE ingest can
/// never clobber it — see `BathymetryStore::set` and the `pre_existing_writable`
/// construction flag the importer opts into.
enum class SourceLayer : uint8_t
{
  Cube = 0,         ///< The CUBE product (live on-boat or off-boat re-run). Highest
                    ///< priority. Subsumes the pre-#248 draft/processed distinction
                    ///< (one fused surface per layer since #221).
  PreExisting = 1,  ///< A prior surface imported before the survey (chart-derived
                    ///< contour prior, external processed grid). Broad, coarse,
                    ///< read-only. Ellipsoidal heights converted at import (§D4).
};

/// @brief Source layers in descending priority order — iterate for best-source.
inline constexpr std::array<SourceLayer, 2> source_layers_by_priority{
  SourceLayer::Cube, SourceLayer::PreExisting};

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
