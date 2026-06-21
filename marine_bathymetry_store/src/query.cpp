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
#include <set>

namespace marine_bathymetry_store
{

namespace
{

/// Geographic center of a cell (its SW corner plus half a cell each way). Used
/// to re-resolve the query position at other GGGS levels (multi-level store).
geographic_msgs::msg::GeoPoint cellCenter(const gggs::CellIndex & cell)
{
  const gggs::GridIndex & grid = cell.grid();
  const double lat_per_cell = grid.latitudinalSpan() / gggs::cell_rows_per_grid;
  const double lon_per_cell = grid.longitudinalSpan() / gggs::cell_columns_per_grid;
  const auto sw = cell.position();   // SW corner of the cell
  return gggs::geoPoint(sw.latitude + 0.5 * lat_per_cell, sw.longitude + 0.5 * lon_per_cell);
}

/// The distinct GGGS levels present in @p layer, **finest first** (largest level
/// number = finest resolution). Empty if the layer holds no tiles.
std::set<uint8_t, std::greater<uint8_t>> levelsPresent(
  const BathymetryStore & store, SourceLayer layer)
{
  std::set<uint8_t, std::greater<uint8_t>> levels;
  for (const auto & [grid, tile] : store.tiles(layer)) {
    (void)tile;
    levels.insert(grid.level());
  }
  return levels;
}

/// Resolve a single layer's best-available sample at a cell, scanning the levels
/// present finest-first and returning the first with usable data (ADR-0002 §D2
/// best-available-across-levels). @p center is the query position.
std::optional<DepthSample> sampleFor(
  const BathymetryStore & store, SourceLayer layer, const gggs::CellIndex & cell)
{
  // Fast path: the cell's own level, queried directly (no re-projection).
  if (const auto c = store.get(layer, cell); c && c->hasData()) {
    return DepthSample{c->depth, c->uncertainty, c->timestamp, layer, c->source_index,
      cell.level()};
  }
  // Multi-level: re-resolve the query position at each level present, finest
  // first. The cell's own level (tried above) is skipped here.
  const auto center = cellCenter(cell);
  for (const uint8_t lvl : levelsPresent(store, layer)) {
    if (lvl == cell.level()) {
      continue;
    }
    const gggs::CellIndex lvl_cell = gggs::Level(lvl).cellIndex(center);
    if (const auto c = store.get(layer, lvl_cell); c && c->hasData()) {
      return DepthSample{c->depth, c->uncertainty, c->timestamp, layer, c->source_index, lvl};
    }
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
  const auto center = cellCenter(cell);

  // Consider one candidate sample (already known to have data) against the
  // running shallowest, applying the reliability gate. depth is ellipsoidal
  // height (up-positive): shallower == greater height.
  const auto consider = [&](const BathyCell & c, SourceLayer layer, uint8_t lvl) {
      // A NaN uncertainty is never reliable; otherwise require within tolerance.
      if (std::isnan(c.uncertainty) || c.uncertainty > max_uncertainty) {
        return;
      }
      if (!shallowest || c.depth > shallowest->depth) {
        shallowest = DepthSample{c.depth, c.uncertainty, c.timestamp, layer,
          c.source_index, lvl};
      }
    };

  for (const SourceLayer layer : source_layers_by_priority) {
    // Safety query: examine EVERY level present, not just the finest-with-data
    // — a coarser level could be both shallower and reliable when a finer one is
    // noisy (ADR-0002 §D2 / §D7). The cell's own level is included via the set.
    for (const uint8_t lvl : levelsPresent(store, layer)) {
      const gggs::CellIndex lvl_cell =
        (lvl == cell.level()) ? cell : gggs::Level(lvl).cellIndex(center);
      const auto c = store.get(layer, lvl_cell);
      if (c && c->hasData()) {
        consider(*c, layer, lvl);
      }
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

}  // namespace marine_bathymetry_store
