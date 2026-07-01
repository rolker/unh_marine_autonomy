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

#include <cstddef>
#include <map>
#include <stdexcept>
#include <utility>

namespace marine_bathymetry_store
{

bool BathymetryStore::set(
  SourceLayer layer, const gggs::CellIndex & cell, const BathyCell & value)
{
  // Validate the input first, then the layer permission — so a malformed cell
  // always reports invalid_argument (the more actionable error), and the
  // read-only logic_error fires only for an otherwise-valid Reference write.
  // The cell may be at any valid level — the store is multi-level (ADR-0002 §D2).
  if (!cell.valid()) {
    throw std::invalid_argument("BathymetryStore::set: invalid CellIndex");
  }
  if (layer == SourceLayer::Reference && !reference_writable_) {
    throw std::logic_error(
            "BathymetryStore::set: Reference is a read-only prior layer; construct "
            "the store with reference_writable=true (importer only) to write it");
  }
  // Last-write-wins per cell: there is no per-day epoch ordering since #221.
  BathymetryTile & tile = getOrCreateTile(layer, cell.grid());
  tile.set(cell.row(), cell.column(), value);
  return true;
}

std::optional<BathyCell> BathymetryStore::get(
  SourceLayer layer, const gggs::CellIndex & cell) const
{
  if (!cell.valid()) {
    return std::nullopt;
  }
  const auto & layer_tiles = layerMap(layer);
  const auto tile_it = layer_tiles.find(cell.grid());
  if (tile_it == layer_tiles.end()) {
    return std::nullopt;
  }
  return tile_it->second.get(cell.row(), cell.column());
}

std::size_t BathymetryStore::importTiles(
  SourceLayer layer, std::map<gggs::GridIndex, BathymetryTile> tiles)
{
  // Reference is a read-only prior (ADR-0002 §D3). importTiles is a public
  // mutator just like set(), so it must honor the same gate -- otherwise the CLI
  // or any library consumer could overwrite the prior on a default store,
  // defeating the read-only guarantee. Only an importer that explicitly opted in
  // (reference_writable=true) may write Reference.
  if (layer == SourceLayer::Reference && !reference_writable_) {
    throw std::logic_error(
            "BathymetryStore::importTiles: Reference is a read-only prior layer; "
            "construct the store with reference_writable=true (importer only) to write it");
  }
  for (const auto & [grid, tile] : tiles) {
    if (!grid.valid()) {
      throw std::invalid_argument("BathymetryStore::importTiles: invalid GridIndex key");
    }
    // The tile must have been built for the grid it is keyed under: a mismatch
    // would write a tile under one grid's filename but with another grid's
    // georeference, corrupting the store on the next load (harvested #148
    // Copilot med-fix, preserved across the epoch removal).
    if (!(tile.index() == grid)) {
      throw std::invalid_argument(
              "BathymetryStore::importTiles: tile GridIndex does not match its map key");
    }
  }

  auto & m = layerMap(layer);
  std::size_t inserted = 0;
  for (auto & [grid, tile] : tiles) {
    // A bulk import is a fresh surface: mark every inserted tile dirty so it
    // persists on the next save. insert_or_assign (not operator[]) because
    // BathymetryTile is not default-constructible.
    tile.markDirty();
    m.insert_or_assign(grid, std::move(tile));
    ++inserted;
  }
  return inserted;
}

BathymetryTile & BathymetryStore::getOrCreateTile(
  SourceLayer layer, const gggs::GridIndex & grid)
{
  // Any valid level is accepted — the store is multi-level (ADR-0002 §D2). The
  // GridIndex carries its own level, so tiles at different levels coexist.
  if (!grid.valid()) {
    throw std::invalid_argument("BathymetryStore::getOrCreateTile: invalid GridIndex");
  }
  auto & layer_tiles = layerMap(layer);
  auto it = layer_tiles.find(grid);
  if (it == layer_tiles.end()) {
    it = layer_tiles.emplace(grid, BathymetryTile(grid)).first;
  }
  return it->second;
}

}  // namespace marine_bathymetry_store
