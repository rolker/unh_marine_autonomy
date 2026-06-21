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

#include "marine_mbes_backscatter_store/mbes_store.hpp"

#include <stdexcept>
#include <string>

namespace marine_mbes_backscatter_store
{

void MbesBackscatterStore::set(
  SourceLayer layer, const gggs::CellIndex & cell, const MbesCell & value)
{
  if (!cell.valid()) {
    throw std::invalid_argument("MbesBackscatterStore::set: invalid CellIndex");
  }
  if (cell.level() != level_.level()) {
    throw std::invalid_argument(
            "MbesBackscatterStore::set: CellIndex at level " +
            std::to_string(cell.level()) + " but store is at level " +
            std::to_string(level_.level()));
  }
  // Draft recency policy (ADR-0007 D7): newest-valid-wins. tile.set overwrites
  // the cell unconditionally — there is no accumulation in the store; the
  // caller supplies already-corrected node-output values.
  MbesTile & tile = getOrCreateTile(layer, cell.grid());
  tile.set(cell.row(), cell.column(), value);
}

std::optional<MbesCell> MbesBackscatterStore::get(
  SourceLayer layer, const gggs::CellIndex & cell) const
{
  if (!cell.valid()) {
    return std::nullopt;
  }
  const auto & m = layerMap(layer);
  const auto it = m.find(cell.grid());
  if (it == m.end()) {
    return std::nullopt;
  }
  return it->second.get(cell.row(), cell.column());
}

MbesTile & MbesBackscatterStore::getOrCreateTile(
  SourceLayer layer, const gggs::GridIndex & grid)
{
  if (!grid.valid()) {
    throw std::invalid_argument("MbesBackscatterStore::getOrCreateTile: invalid GridIndex");
  }
  if (grid.level() != level_.level()) {
    throw std::invalid_argument(
            "MbesBackscatterStore::getOrCreateTile: GridIndex at level " +
            std::to_string(grid.level()) + " but store is at level " +
            std::to_string(level_.level()));
  }
  auto & m = layerMap(layer);
  auto it = m.find(grid);
  if (it == m.end()) {
    it = m.emplace(grid, MbesTile(grid)).first;
  }
  return it->second;
}

}  // namespace marine_mbes_backscatter_store
