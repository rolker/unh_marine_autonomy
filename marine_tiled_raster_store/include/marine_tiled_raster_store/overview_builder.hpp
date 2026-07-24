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

#ifndef MARINE_TILED_RASTER_STORE__OVERVIEW_BUILDER_HPP_
#define MARINE_TILED_RASTER_STORE__OVERVIEW_BUILDER_HPP_

#include <functional>
#include <map>
#include <utility>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_autonomy/gggs/index_math.h"

#include "tiled_raster_tile.hpp"

/// @file
/// @brief Generic cross-tile GGGS overview (pyramid) fold — ADR-0011 / #188.
///
/// Folds finer-level tiles into their coarser-level parent tiles: 4 fine
/// children per parent in temperate bands (the count differs across polar
/// `latitudeScaleFactor` boundaries — handled, see below). Overviews are a
/// DERIVED, REGENERABLE product written to a per-layer `overviews/` sidecar
/// (flat, `<level>_<row>_<col>.tif` — the level rides in the filename exactly
/// as in the fine layer). They are never merged into the fine layer and never
/// enter anti-entropy/possession sets.
///
/// The load-bearing parent<->child mapping is `gggs::parent()` /
/// `gggs::children()` (`marine_autonomy/gggs/index_math.h`) plus PER-CELL
/// GEOGRAPHIC accumulation: each child cell's centre is located in the parent
/// grid via `gggs::CellIndex(parent, centre)`. Going through geography (not
/// row/column halving) keeps the fold correct across latitude bands where the
/// column scaling changes, at the cost of a few transcendental ops per cell —
/// an offline batch path, so clarity wins.
///
/// Fold POLICIES are per-store (the shared-engine / per-store-compositor
/// boundary from #191): imagery folds by mean; depths fold
/// shallowest-preserving (the coarse cell carries its shoalest-reliable
/// child's whole value/uncertainty pair — never a mean; ADR-0010 D9). The
/// policy here operates on whole cells (all bands of one contributor at once)
/// so cross-band-coherent folds like the depth pair are expressible.

namespace marine_tiled_raster_store
{

/// @brief One child cell's values, all bands, in band order.
template<typename T>
using CellValues = std::vector<T>;

/// @brief Reduce the valid contributors that landed in one parent cell to the
///        parent cell's per-band values.
///
/// Called only when at least one valid contributor exists. Contributors carry
/// ALL bands of one child cell, so a policy can fold bands independently
/// (imagery mean) or select one contributor's coherent value set (depth
/// shallowest-preserving).
template<typename T>
using CellFoldPolicy =
  std::function<CellValues<T>(const std::vector<CellValues<T>> & contributors)>;

/// @brief Whether a child cell participates in the fold (e.g. band 0 != the
///        no-data sentinel). Invalid cells contribute nothing.
template<typename T>
using CellValidPolicy = std::function<bool (const CellValues<T> &)>;

/// @brief Fold @p children (tiles at level L) into one parent tile (level L-1).
///
/// Every valid child cell is accumulated into the parent cell containing its
/// geographic centre; @p fold reduces each parent cell's contributors. Parent
/// cells with no valid contributor keep @p band_fills (the no-data fills).
///
/// @param parent_grid The parent grid (level = children's level - 1). Children
///   whose `gggs::parent()` is not @p parent_grid are skipped (caller grouping
///   error — skipping keeps the fold safe rather than corrupting a neighbour).
/// @param children Child tiles; all must share one level and band count.
/// @param band_fills Per-band no-data fill for unset parent cells (also fixes
///   the band count).
/// @param valid Contributor gate (see CellValidPolicy).
/// @param fold Per-cell reduction (see CellFoldPolicy).
/// @return The folded parent tile (dirty, ready for saveTile).
/// @throws std::invalid_argument on empty @p band_fills, no children, or a
///   child band count differing from @p band_fills.
template<typename T>
TiledRasterTile<T> buildParentTile(
  const gggs::GridIndex & parent_grid,
  const std::vector<const TiledRasterTile<T> *> & children,
  const std::vector<T> & band_fills,
  const CellValidPolicy<T> & valid,
  const CellFoldPolicy<T> & fold)
{
  if (band_fills.empty()) {
    throw std::invalid_argument("buildParentTile: band_fills must be non-empty");
  }
  if (children.empty()) {
    throw std::invalid_argument("buildParentTile: no children");
  }
  const std::size_t bands = band_fills.size();

  // Per-parent-cell contributor buckets. ~4 contributors per cell in temperate
  // bands; transient (~a hundred MB for a 960x960 3-band uint16 fold) — an
  // offline batch path, freed per parent tile.
  std::vector<std::vector<CellValues<T>>> buckets(TiledRasterTile<T>::cell_count);

  for (const TiledRasterTile<T> * child : children) {
    if (child == nullptr) {continue;}
    if (child->bandCount() != bands) {
      throw std::invalid_argument("buildParentTile: child band count mismatch");
    }
    if (gggs::parent(child->index()) != parent_grid) {
      continue;   // caller grouping error — skip rather than corrupt a neighbour
    }
    const gggs::GridIndex & cg = child->index();
    const double lat_span = cg.latitudinalSpan();
    const double lon_span = cg.longitudinalSpan();
    const double south = cg.southLatitude();
    const double west = cg.westLongitude();
    constexpr double edge_d = TiledRasterTile<T>::edge;
    for (uint16_t row = 0; row < TiledRasterTile<T>::edge; ++row) {
      const double lat = south + (row + 0.5) / edge_d * lat_span;
      for (uint16_t col = 0; col < TiledRasterTile<T>::edge; ++col) {
        CellValues<T> cell(bands);
        for (std::size_t b = 0; b < bands; ++b) {
          cell[b] = child->get(row, col, b);
        }
        if (!valid(cell)) {continue;}
        const double lon = west + (col + 0.5) / edge_d * lon_span;
        const gggs::CellIndex parent_cell(parent_grid, gggs::geoPoint(lat, lon));
        buckets[TiledRasterTile<T>::offset(parent_cell.row(), parent_cell.column())]
        .push_back(std::move(cell));
      }
    }
  }

  TiledRasterTile<T> parent_tile(parent_grid, band_fills);
  for (uint16_t row = 0; row < TiledRasterTile<T>::edge; ++row) {
    for (uint16_t col = 0; col < TiledRasterTile<T>::edge; ++col) {
      const auto & contributors = buckets[TiledRasterTile<T>::offset(row, col)];
      if (contributors.empty()) {continue;}
      const CellValues<T> folded = fold(contributors);
      for (std::size_t b = 0; b < bands && b < folded.size(); ++b) {
        parent_tile.set(row, col, b, folded[b]);
      }
    }
  }
  return parent_tile;
}

/// @brief Fold a whole level: group @p fine_tiles by `gggs::parent()` and build
///        every parent that has at least one child tile.
///
/// @return The parent tiles (level = fine level - 1), keyed by grid — ready to
///   save AND to feed the next-coarser fold (each level builds from the one
///   below it, not from the finest data).
template<typename T>
std::map<gggs::GridIndex, TiledRasterTile<T>> buildOverviewLevel(
  const std::map<gggs::GridIndex, TiledRasterTile<T>> & fine_tiles,
  const std::vector<T> & band_fills,
  const CellValidPolicy<T> & valid,
  const CellFoldPolicy<T> & fold)
{
  std::map<gggs::GridIndex, std::vector<const TiledRasterTile<T> *>> by_parent;
  for (const auto & item : fine_tiles) {
    const gggs::GridIndex parent_grid = gggs::parent(item.first);
    if (!parent_grid.valid()) {continue;}   // already at level 0
    by_parent[parent_grid].push_back(&item.second);
  }
  std::map<gggs::GridIndex, TiledRasterTile<T>> parents;
  for (const auto & group : by_parent) {
    parents.emplace(
      group.first,
      buildParentTile<T>(group.first, group.second, band_fills, valid, fold));
  }
  return parents;
}

}  // namespace marine_tiled_raster_store

#endif  // MARINE_TILED_RASTER_STORE__OVERVIEW_BUILDER_HPP_
