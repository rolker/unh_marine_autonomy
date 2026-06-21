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

/// The distinct GGGS levels present in one @p epoch's tile map, **finest first**
/// (largest level number = finest resolution). Empty if the epoch has no tiles.
std::set<uint8_t, std::greater<uint8_t>> levelsPresent(const EpochTiles & epoch_tiles)
{
  std::set<uint8_t, std::greater<uint8_t>> levels;
  for (const auto & [grid, tile] : epoch_tiles.tiles) {
    (void)tile;
    levels.insert(grid.level());
  }
  return levels;
}

/// Look up @p cell in one epoch's tile map, or nullopt if its grid is absent.
std::optional<BathyCell> cellIn(const EpochTiles & epoch_tiles, const gggs::CellIndex & cell)
{
  const auto it = epoch_tiles.tiles.find(cell.grid());
  if (it == epoch_tiles.tiles.end()) {
    return std::nullopt;
  }
  return it->second.get(cell.row(), cell.column());
}

/// Resolve a single epoch's best-available sample at a cell across the levels it
/// holds, finest-first (ADR-0002 §D2). @p center is the query position.
std::optional<DepthSample> sampleInEpoch(
  const EpochTiles & epoch_tiles, SourceLayer layer, const Epoch & epoch,
  const gggs::CellIndex & cell, const geographic_msgs::msg::GeoPoint & center)
{
  for (const uint8_t lvl : levelsPresent(epoch_tiles)) {
    const gggs::CellIndex lvl_cell =
      (lvl == cell.level()) ? cell : gggs::Level(lvl).cellIndex(center);
    const auto c = cellIn(epoch_tiles, lvl_cell);
    if (c && c->hasData()) {
      return DepthSample{c->depth, c->uncertainty, c->timestamp, layer, c->source_index,
        lvl, epoch};
    }
  }
  return std::nullopt;
}

/// Resolve a single layer's best-available sample at a cell: newest epoch with
/// usable data (ADR-0002 §A1.3 default resolution), best level within it.
std::optional<DepthSample> sampleFor(
  const BathymetryStore & store, SourceLayer layer, const gggs::CellIndex & cell,
  const geographic_msgs::msg::GeoPoint & center)
{
  const auto & epochs_map = store.epochs(layer);
  for (auto it = epochs_map.rbegin(); it != epochs_map.rend(); ++it) {
    if (auto sample = sampleInEpoch(it->second, layer, it->first, cell, center)) {
      return sample;
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<DepthSample> bestSource(
  const BathymetryStore & store, const gggs::CellIndex & cell)
{
  const auto center = cellCenter(cell);
  for (const SourceLayer layer : source_layers_by_priority) {
    if (auto sample = sampleFor(store, layer, cell, center)) {
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
  const auto consider =
    [&](const BathyCell & c, SourceLayer layer, uint8_t lvl, const Epoch & epoch) {
      // A NaN uncertainty is never reliable; otherwise require within tolerance.
      if (std::isnan(c.uncertainty) || c.uncertainty > max_uncertainty) {
        return;
      }
      if (!shallowest || c.depth > shallowest->depth) {
        shallowest = DepthSample{c.depth, c.uncertainty, c.timestamp, layer,
          c.source_index, lvl, epoch};
      }
    };

  for (const SourceLayer layer : source_layers_by_priority) {
    const auto & epochs_map = store.epochs(layer);
    // §A1.3 safety walk: within a layer, take the newest epoch that has a
    // reliable value at the cell (a noisy fresh epoch falls through to an older
    // confident one). Resolve each epoch independently (newest-first), and keep
    // the shallowest across the layers. Within an epoch, examine EVERY level so
    // a coarse-but-reliable value can win where a finer one is too uncertain.
    for (auto it = epochs_map.rbegin(); it != epochs_map.rend(); ++it) {
      std::optional<DepthSample> epoch_pick;
      for (const uint8_t lvl : levelsPresent(it->second)) {
        const gggs::CellIndex lvl_cell =
          (lvl == cell.level()) ? cell : gggs::Level(lvl).cellIndex(center);
        const auto c = cellIn(it->second, lvl_cell);
        if (!c || !c->hasData()) {
          continue;
        }
        if (std::isnan(c->uncertainty) || c->uncertainty > max_uncertainty) {
          continue;
        }
        // Within this epoch, prefer the shallowest reliable value.
        if (!epoch_pick || c->depth > epoch_pick->depth) {
          epoch_pick = DepthSample{c->depth, c->uncertainty, c->timestamp, layer,
            c->source_index, lvl, it->first};
        }
      }
      if (epoch_pick) {
        consider(BathyCell{epoch_pick->depth, epoch_pick->uncertainty,
            epoch_pick->timestamp, epoch_pick->source_index},
          layer, epoch_pick->level, epoch_pick->epoch);
        break;   // newest reliable epoch wins for this layer (§A1.3)
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
  // coverage change, not depth change. Grids carry their level, so a grid only
  // matches its same-level counterpart (cross-level cells have no 1:1 pairing).
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
