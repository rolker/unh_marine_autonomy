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

#include <stdexcept>
#include <string>
#include <utility>

namespace marine_bathymetry_store
{

bool BathymetryStore::set(
  SourceLayer layer, const Epoch & epoch, const gggs::CellIndex & cell,
  const BathyCell & value)
{
  // Validate the inputs first, then the layer permission — so a malformed cell
  // always reports invalid_argument (the more actionable error), and the
  // read-only logic_error fires only for an otherwise-valid Chart write. The
  // cell may be at any valid level — the store is multi-level (ADR-0002 §D2).
  validateEpochLabel(epoch);
  if (!cell.valid()) {
    throw std::invalid_argument("BathymetryStore::set: invalid CellIndex");
  }
  if (layer == SourceLayer::Chart && !chart_writable_) {
    throw std::logic_error(
            "BathymetryStore::set: Chart is a read-only prior layer; construct "
            "the store with chart_writable=true (importer only) to write it");
  }
  // A compacted (Replayed) epoch is immutable: a live write must not regress it
  // (ADR-0002 §A1.2). Report the no-op so the caller can log it.
  auto & m = layerMap(layer);
  const auto existing = m.find(epoch);
  if (existing != m.end() && existing->second.provenance == Provenance::Replayed) {
    return false;
  }
  BathymetryTile & tile = getOrCreateTile(layer, epoch, cell.grid());
  tile.set(cell.row(), cell.column(), value);
  return true;
}

std::optional<BathyCell> BathymetryStore::get(
  SourceLayer layer, const Epoch & epoch, const gggs::CellIndex & cell) const
{
  if (!cell.valid()) {
    return std::nullopt;
  }
  const auto & m = layerMap(layer);
  const auto epoch_it = m.find(epoch);
  if (epoch_it == m.end()) {
    return std::nullopt;
  }
  const auto & tiles = epoch_it->second.tiles;
  const auto tile_it = tiles.find(cell.grid());
  if (tile_it == tiles.end()) {
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
    if (!grid.valid()) {
      throw std::invalid_argument("BathymetryStore::importEpoch: invalid GridIndex key");
    }
    // The tile must have been built for the grid it is keyed under: a mismatch
    // would write a tile under one grid's filename but with another grid's
    // georeference, corrupting the store on the next load (harvested #148
    // Copilot med-fix).
    if (!(tile.index() == grid)) {
      throw std::invalid_argument(
              "BathymetryStore::importEpoch: tile GridIndex does not match its map key");
    }
  }

  auto & m = layerMap(layer);
  const auto existing = m.find(epoch);
  // §A1.2 ordering: live-fused never replaces replayed.
  if (existing != m.end() &&
    existing->second.provenance == Provenance::Replayed &&
    provenance == Provenance::LiveFused)
  {
    return false;
  }

  EpochTiles replacement;
  replacement.provenance = provenance;
  replacement.supersedes_disk = true;   // persistence clears stale files first
  replacement.tiles = std::move(tiles);
  // A wholesale import is a fresh surface: mark every tile dirty so it persists.
  for (auto & [grid, tile] : replacement.tiles) {
    (void)grid;
    tile.markDirty();
  }
  m[epoch] = std::move(replacement);
  return true;
}

EpochTiles & BathymetryStore::getOrCreateEpoch(
  SourceLayer layer, const Epoch & epoch, Provenance provenance)
{
  validateEpochLabel(epoch);
  auto & m = layerMap(layer);
  auto it = m.find(epoch);
  if (it == m.end()) {
    EpochTiles fresh;
    fresh.provenance = provenance;
    it = m.emplace(epoch, std::move(fresh)).first;
  }
  return it->second;
}

BathymetryTile & BathymetryStore::getOrCreateTile(
  SourceLayer layer, const Epoch & epoch, const gggs::GridIndex & grid)
{
  // Any valid level is accepted — the store is multi-level (ADR-0002 §D2). The
  // GridIndex carries its own level, so tiles at different levels coexist.
  if (!grid.valid()) {
    throw std::invalid_argument("BathymetryStore::getOrCreateTile: invalid GridIndex");
  }
  EpochTiles & epoch_tiles = getOrCreateEpoch(layer, epoch, Provenance::LiveFused);
  auto & tiles = epoch_tiles.tiles;
  auto it = tiles.find(grid);
  if (it == tiles.end()) {
    it = tiles.emplace(grid, BathymetryTile(grid)).first;
  }
  return it->second;
}

}  // namespace marine_bathymetry_store
