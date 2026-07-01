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

#include "marine_backscatter/processed_accumulator.hpp"

namespace marine_backscatter
{

ProcessedAccumulator::Tile & ProcessedAccumulator::ensureOutput(const gggs::GridIndex & grid)
{
  auto it = output_.find(grid);
  if (it == output_.end()) {
    // 3 bands {intensity, quality, source}, all no-data (0).
    it = output_.emplace(grid, Tile(grid, kBands, 0)).first;
  }
  return it->second;
}

void ProcessedAccumulator::add(
  const gggs::CellIndex & cell,
  std::uint16_t intensity, std::uint16_t quality, std::uint16_t source_id)
{
  if (!cell.valid()) {
    return;
  }
  const std::uint16_t row = cell.row();
  const std::uint16_t col = cell.column();
  Tile & tile = ensureOutput(cell.grid());
  // Best-source: keep the highest-quality look; a poorer pass never overwrites a
  // better one. Ties (==) keep the incumbent, so the order of equally-good passes
  // is stable.
  if (quality > tile.get(row, col, kQuality)) {
    tile.set(row, col, kIntensity, intensity);
    tile.set(row, col, kQuality, quality);
    tile.set(row, col, kSource, source_id);
  }
}

void ProcessedAccumulator::foldTile(const Tile & existing)
{
  // Same best-source rule as add(), applied cell-by-cell from a reloaded tile.
  // A strictly-greater test means the incumbent wins ties, so folding an existing
  // store tile into a run that already composited the new pings keeps the higher-
  // quality look regardless of fold order.
  Tile & tile = ensureOutput(existing.index());
  for (std::uint16_t row = 0; row < gggs::cell_rows_per_grid; ++row) {
    for (std::uint16_t col = 0; col < gggs::cell_rows_per_grid; ++col) {
      const std::uint16_t q = existing.get(row, col, kQuality);
      if (q == 0) {
        continue;   // no-data cell in the reloaded tile.
      }
      if (q > tile.get(row, col, kQuality)) {
        tile.set(row, col, kIntensity, existing.get(row, col, kIntensity));
        tile.set(row, col, kQuality, q);
        tile.set(row, col, kSource, existing.get(row, col, kSource));
      }
    }
  }
}

}  // namespace marine_backscatter
