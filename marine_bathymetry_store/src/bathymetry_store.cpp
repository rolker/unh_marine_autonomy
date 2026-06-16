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

#include "marine_bathymetry_store/bathymetry_store.hpp"

#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace marine_bathymetry_store
{

void BathymetryStore::requireGridAtLevel(
  const gggs::GridIndex & grid, const char * who) const
{
  if (!grid.valid()) {
    throw std::invalid_argument(std::string(who) + ": invalid GridIndex");
  }
  if (grid.level() != level_.level()) {
    throw std::invalid_argument(
            std::string(who) + ": GridIndex at level " +
            std::to_string(grid.level()) + " but store is at level " +
            std::to_string(level_.level()));
  }
}

bool BathymetryStore::set(
  SourceLayer layer, const Epoch & epoch, const gggs::CellIndex & cell,
  const BathyCell & value)
{
  // Validate the inputs first, then the layer permission — so a malformed cell
  // always reports invalid_argument (the more actionable error), and the
  // read-only logic_error fires only for an otherwise-valid Chart write.
  if (!cell.valid()) {
    throw std::invalid_argument("BathymetryStore::set: invalid CellIndex");
  }
  if (cell.level() != level_.level()) {
    throw std::invalid_argument(
            "BathymetryStore::set: CellIndex at level " +
            std::to_string(cell.level()) + " but store is at level " +
            std::to_string(level_.level()));
  }
  if (layer == SourceLayer::Chart && !chart_writable_) {
    throw std::logic_error(
            "BathymetryStore::set: Chart is a read-only prior layer; construct "
            "the store with chart_writable=true (importer only) to write it");
  }
  EpochTiles & epoch_tiles = getOrCreateEpoch(layer, epoch, Provenance::LiveFused);
  // A compacted (replayed) epoch is immutable: a late live snapshot must not
  // regress it (ADR-0002 §A1.2 provenance ordering). No-op; the caller logs.
  if (epoch_tiles.provenance == Provenance::Replayed) {
    return false;
  }
  auto it = epoch_tiles.tiles.find(cell.grid());
  if (it == epoch_tiles.tiles.end()) {
    it = epoch_tiles.tiles.emplace(cell.grid(), BathymetryTile(cell.grid())).first;
  }
  it->second.set(cell.row(), cell.column(), value);
  return true;
}

std::optional<BathyCell> BathymetryStore::get(
  SourceLayer layer, const Epoch & epoch, const gggs::CellIndex & cell) const
{
  if (!cell.valid()) {
    return std::nullopt;
  }
  const auto & epochs_map = layerMap(layer);
  const auto epoch_it = epochs_map.find(epoch);
  if (epoch_it == epochs_map.end()) {
    return std::nullopt;
  }
  const auto tile_it = epoch_it->second.tiles.find(cell.grid());
  if (tile_it == epoch_it->second.tiles.end()) {
    return std::nullopt;
  }
  return tile_it->second.get(cell.row(), cell.column());
}

bool BathymetryStore::importEpoch(
  SourceLayer layer, const Epoch & epoch,
  std::map<gggs::GridIndex, BathymetryTile> tiles, Provenance provenance)
{
  validateEpochLabel(epoch);
  for (const auto & [grid, tile] : tiles) {
    requireGridAtLevel(grid, "BathymetryStore::importEpoch");
    // The map key and the tile's own index must agree: persistence
    // georeferences by tile.index() but names the file by the key, so a
    // mismatch produces a tile that load() silently re-keys elsewhere.
    if (tile.index() != grid) {
      throw std::invalid_argument(
              "BathymetryStore::importEpoch: tile.index() does not match its map key");
    }
  }

  auto & epochs_map = layerMap(layer);
  const auto it = epochs_map.find(epoch);
  // §A1.2 ordering: live-fused never replaces replayed. Equal-or-higher
  // provenance (a re-compaction, or replayed over live) is allowed.
  if (it != epochs_map.end() &&
    it->second.provenance == Provenance::Replayed &&
    provenance == Provenance::LiveFused)
  {
    return false;
  }

  EpochTiles replacement;
  replacement.provenance = provenance;
  replacement.supersedes_disk = true;
  replacement.tiles = std::move(tiles);
  // Everything just imported is unsaved by definition.
  for (auto & [grid, tile] : replacement.tiles) {
    static_cast<void>(grid);
    tile.markDirty();
  }
  epochs_map[epoch] = std::move(replacement);
  return true;
}

EpochTiles & BathymetryStore::getOrCreateEpoch(
  SourceLayer layer, const Epoch & epoch, Provenance provenance)
{
  validateEpochLabel(epoch);
  auto & epochs_map = layerMap(layer);
  auto it = epochs_map.find(epoch);
  if (it == epochs_map.end()) {
    it = epochs_map.emplace(epoch, EpochTiles{}).first;
    it->second.provenance = provenance;
  }
  return it->second;
}

BathymetryTile & BathymetryStore::getOrCreateTile(
  SourceLayer layer, const Epoch & epoch, const gggs::GridIndex & grid)
{
  requireGridAtLevel(grid, "BathymetryStore::getOrCreateTile");
  EpochTiles & epoch_tiles = getOrCreateEpoch(layer, epoch, Provenance::LiveFused);
  auto it = epoch_tiles.tiles.find(grid);
  if (it == epoch_tiles.tiles.end()) {
    it = epoch_tiles.tiles.emplace(grid, BathymetryTile(grid)).first;
  }
  return it->second;
}

}  // namespace marine_bathymetry_store
