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

#include "marine_bathymetry_store/query.hpp"

#include <cmath>

namespace marine_bathymetry_store
{

namespace
{

/// Look up a cell in one epoch's tile map, or nullopt if absent.
std::optional<BathyCell> cellIn(
  const EpochTiles & epoch_tiles, const gggs::CellIndex & cell)
{
  const auto it = epoch_tiles.tiles.find(cell.grid());
  if (it == epoch_tiles.tiles.end()) {
    return std::nullopt;
  }
  return it->second.get(cell.row(), cell.column());
}

/// Resolve a single layer's sample at a cell: newest epoch with usable data
/// (ADR-0002 §A1.3 default resolution), or nullopt if none.
std::optional<DepthSample> sampleFor(
  const BathymetryStore & store, SourceLayer layer, const gggs::CellIndex & cell)
{
  const auto & epochs_map = store.epochs(layer);
  for (auto it = epochs_map.rbegin(); it != epochs_map.rend(); ++it) {
    const auto c = cellIn(it->second, cell);
    if (c && c->hasData()) {
      return DepthSample{c->depth, c->uncertainty, c->timestamp, layer, it->first};
    }
  }
  return std::nullopt;
}

/// Resolve a single layer's *reliable* sample at a cell: newest epoch whose
/// value passes the uncertainty gate (ADR-0002 §A1.3 safety walk) — a noisy
/// fresh epoch is skipped, falling through to an older confident one.
std::optional<DepthSample> reliableSampleFor(
  const BathymetryStore & store, SourceLayer layer, const gggs::CellIndex & cell,
  double max_uncertainty)
{
  const auto & epochs_map = store.epochs(layer);
  for (auto it = epochs_map.rbegin(); it != epochs_map.rend(); ++it) {
    const auto c = cellIn(it->second, cell);
    if (!c || !c->hasData()) {
      continue;
    }
    // A NaN uncertainty is never reliable; otherwise require it within tolerance.
    if (std::isnan(c->uncertainty) || c->uncertainty > max_uncertainty) {
      continue;
    }
    return DepthSample{c->depth, c->uncertainty, c->timestamp, layer, it->first};
  }
  return std::nullopt;
}

}  // namespace

std::optional<DepthSample> bestSource(
  const BathymetryStore & store, const gggs::CellIndex & cell)
{
  for (const SourceLayer layer : source_layers_by_priority) {
    if (auto sample = sampleFor(store, layer, cell)) {
      return sample;
    }
  }
  return std::nullopt;
}

std::optional<DepthSample> shallowestReliable(
  const BathymetryStore & store, const gggs::CellIndex & cell, double max_uncertainty)
{
  std::optional<DepthSample> shallowest;
  for (const SourceLayer layer : source_layers_by_priority) {
    const auto sample = reliableSampleFor(store, layer, cell, max_uncertainty);
    if (!sample) {
      continue;
    }
    // depth is ellipsoidal height (up-positive): shallower == greater height.
    if (!shallowest || sample->depth > shallowest->depth) {
      shallowest = sample;
    }
  }
  return shallowest;
}

void forEachCellBestSource(
  const BathymetryStore & store,
  const geographic_msgs::msg::GeoPoint & minimum,
  const geographic_msgs::msg::GeoPoint & maximum,
  const std::function<void(const gggs::CellIndex &,
  const std::optional<DepthSample> &)> & visitor)
{
  const gggs::Level & level = store.level();
  gggs::GridAreaIterator grid_it(
    level.gridIndex(minimum.latitude, minimum.longitude),
    level.gridIndex(maximum.latitude, maximum.longitude));
  for (; grid_it.valid(); grid_it.next()) {
    for (gggs::CellAreaIterator cell_it(*grid_it, minimum, maximum);
      cell_it.valid(); cell_it.next())
    {
      visitor(*cell_it, bestSource(store, *cell_it));
    }
  }
}

std::size_t forEachChangedCell(
  const BathymetryStore & store, SourceLayer layer,
  const Epoch & epoch_a, const Epoch & epoch_b,
  const std::function<void(const gggs::CellIndex &,
  const BathyCell &, const BathyCell &)> & visitor)
{
  const auto & epochs_map = store.epochs(layer);
  const auto a_it = epochs_map.find(epoch_a);
  const auto b_it = epochs_map.find(epoch_b);
  if (a_it == epochs_map.end() || b_it == epochs_map.end()) {
    return 0;
  }

  std::size_t visited = 0;
  // Iterate grids present in both epochs; a grid covered by only one is
  // coverage change, not depth change.
  for (const auto & [grid, tile_a] : a_it->second.tiles) {
    const auto tile_b_it = b_it->second.tiles.find(grid);
    if (tile_b_it == b_it->second.tiles.end()) {
      continue;
    }
    const BathymetryTile & tile_b = tile_b_it->second;
    for (uint16_t row = 0; row < BathymetryTile::edge; ++row) {
      for (uint16_t col = 0; col < BathymetryTile::edge; ++col) {
        const BathyCell a = tile_a.get(row, col);
        if (!a.hasData()) {
          continue;
        }
        const BathyCell b = tile_b.get(row, col);
        if (!b.hasData()) {
          continue;
        }
        visitor(gggs::CellIndex(grid, row, col), a, b);
        ++visited;
      }
    }
  }
  return visited;
}

}  // namespace marine_bathymetry_store
