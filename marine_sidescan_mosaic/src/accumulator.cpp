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

#include "marine_sidescan_mosaic/accumulator.hpp"

#include <utility>

namespace marine_sidescan_mosaic
{

MosaicAccumulator::MosaicAccumulator(gggs::Level level, SplatPolicy policy)
: level_(level), policy_(policy) {}

MosaicAccumulator::Tile & MosaicAccumulator::ensureOutput(const gggs::GridIndex & grid)
{
  auto it = output_.find(grid);
  if (it == output_.end()) {
    it = output_.emplace(grid, Tile(grid, 1, 0)).first;
  }
  return it->second;
}

MosaicAccumulator::MeanGrid & MosaicAccumulator::ensureMean(const gggs::GridIndex & grid)
{
  auto it = mean_.find(grid);
  if (it == mean_.end()) {
    MeanGrid mg;
    mg.sum.assign(Tile::cell_count, 0);
    mg.count.assign(Tile::cell_count, 0);
    it = mean_.emplace(grid, std::move(mg)).first;
  }
  return it->second;
}

void MosaicAccumulator::add(const gggs::CellIndex & cell, std::uint16_t value)
{
  if (!cell.valid()) {
    return;
  }
  const gggs::GridIndex grid = cell.grid();
  const std::uint16_t row = cell.row();
  const std::uint16_t col = cell.column();
  Tile & tile = ensureOutput(grid);

  if (policy_ == SplatPolicy::Mean) {
    MeanGrid & mg = ensureMean(grid);
    const std::uint32_t i = Tile::offset(row, col);
    mg.sum[i] += value;
    mg.count[i] += 1;
    // Only write (and dirty) when the rounded mean actually changes, so a dwell
    // over stable ground doesn't re-flush the same tile every cycle.
    const auto mean = static_cast<std::uint16_t>(mg.sum[i] / mg.count[i]);
    if (mean != tile.get(row, col, 0)) {
      tile.set(row, col, 0, mean);
    }
  } else if (policy_ == SplatPolicy::Newest) {
    // Newest-valid-wins: a real return always overwrites so the operator sees
    // live painting. value==0 is the no-data sentinel (real returns are floored
    // to >=1 by RollingNormalizer), so skip it — don't punch holes in coverage.
    if (value != 0 && value != tile.get(row, col, 0)) {
      tile.set(row, col, 0, value);
    }
  } else {
    // MaxHold: only write (and dirty) when the cell actually brightens.
    if (value > tile.get(row, col, 0)) {
      tile.set(row, col, 0, value);
    }
  }
}

}  // namespace marine_sidescan_mosaic
