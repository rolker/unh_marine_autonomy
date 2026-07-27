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

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "marine_tiled_raster_store/overview_builder.hpp"

// [#188 / ADR-0011] Fold-engine correctness: per-cell geographic accumulation
// into the parent (via gggs::parent + CellIndex geography — the load-bearing
// mapping), no-data exclusion, cross-band-coherent selection policies, and
// value-idempotency (fold twice -> identical).

namespace
{

using marine_tiled_raster_store::TiledRasterTile;
using marine_tiled_raster_store::buildParentTile;
using marine_tiled_raster_store::buildOverviewLevel;
using Cell = marine_tiled_raster_store::CellValues<std::uint16_t>;

constexpr int kLevel = 13;              // sidescan native level (temperate site)
constexpr double kLat = 43.0, kLon = -71.4;   // Lake Massabesic-ish

// Integer mean of all bands over the contributors (the imagery policy shape).
Cell meanFold(const std::vector<Cell> & contributors)
{
  const std::size_t bands = contributors.front().size();
  Cell out(bands, 0);
  for (std::size_t b = 0; b < bands; ++b) {
    std::uint64_t sum = 0;
    for (const Cell & c : contributors) {
      sum += c[b];
    }
    out[b] = static_cast<std::uint16_t>(sum / contributors.size());
  }
  return out;
}

bool nonZeroBand0(const Cell & cell) {return cell[0] != 0;}

// A level-13 grid at the test site and its level-12 parent.
gggs::GridIndex fineGrid() {return gggs::Level(kLevel).gridIndex(kLat, kLon);}

}  // namespace

TEST(OverviewBuilder, MeanFoldAveragesChildCellsIntoOneQuadrant)
{
  const gggs::GridIndex fine = fineGrid();
  const gggs::GridIndex parent_grid = gggs::parent(fine);
  ASSERT_TRUE(parent_grid.valid());

  // Checkerboard 100/200 -> every folded parent cell must be the exact mean 150
  // (each parent cell receives a 2x2 block holding two 100s and two 200s).
  TiledRasterTile<std::uint16_t> child(fine, 2, 0);
  for (uint16_t r = 0; r < child.edge; ++r) {
    for (uint16_t c = 0; c < child.edge; ++c) {
      child.set(r, c, 0, ((r + c) % 2) ? 200 : 100);
      child.set(r, c, 1, ((r + c) % 2) ? 400 : 300);
    }
  }

  const auto parent_tile = buildParentTile<std::uint16_t>(
    parent_grid, {&child}, {0, 0}, nonZeroBand0, meanFold);

  // One child covers exactly one quadrant of its parent: a quarter of the
  // parent cells fold to the means, the rest keep the no-data fill.
  std::size_t folded = 0;
  for (uint16_t r = 0; r < parent_tile.edge; ++r) {
    for (uint16_t c = 0; c < parent_tile.edge; ++c) {
      const std::uint16_t v0 = parent_tile.get(r, c, 0);
      if (v0 == 0) {continue;}
      ++folded;
      EXPECT_EQ(v0, 150);
      EXPECT_EQ(parent_tile.get(r, c, 1), 350);
    }
  }
  EXPECT_EQ(folded, TiledRasterTile<std::uint16_t>::cell_count / 4);
}

TEST(OverviewBuilder, NoDataCellsAreExcludedFromTheMean)
{
  const gggs::GridIndex fine = fineGrid();
  const gggs::GridIndex parent_grid = gggs::parent(fine);

  // Three 100s + one 0 (no-data) per 2x2 block: mean over VALID contributors
  // only -> 100, not 75.
  TiledRasterTile<std::uint16_t> child(fine, 1, 0);
  for (uint16_t r = 0; r < child.edge; ++r) {
    for (uint16_t c = 0; c < child.edge; ++c) {
      child.set(r, c, 0, (r % 2 == 0 && c % 2 == 0) ? 0 : 100);
    }
  }
  const auto parent_tile = buildParentTile<std::uint16_t>(
    parent_grid, {&child}, {0}, nonZeroBand0, meanFold);

  for (uint16_t r = 0; r < parent_tile.edge; ++r) {
    for (uint16_t c = 0; c < parent_tile.edge; ++c) {
      const std::uint16_t v = parent_tile.get(r, c, 0);
      EXPECT_TRUE(v == 0 || v == 100) << "cell (" << r << "," << c << ") = " << v;
    }
  }
}

TEST(OverviewBuilder, PairCoherentSelectionPolicyIsExpressible)
{
  // The depth-style policy shape (ADR-0010 D9): select ONE contributor by band
  // 0 and carry its WHOLE value set — bands must stay coherent, never mixed.
  const gggs::GridIndex fine = fineGrid();
  const gggs::GridIndex parent_grid = gggs::parent(fine);

  TiledRasterTile<std::uint16_t> child(fine, 2, 0);
  for (uint16_t r = 0; r < child.edge; ++r) {
    for (uint16_t c = 0; c < child.edge; ++c) {
      // band0 varies per cell; band1 is a per-cell tag = band0 + 1000 so a
      // mixed (non-coherent) fold is detectable.
      const std::uint16_t v = 10 + (r % 2) * 2 + (c % 2);   // 10..13
      child.set(r, c, 0, v);
      child.set(r, c, 1, v + 1000);
    }
  }
  const auto minSelect = [](const std::vector<Cell> & contributors) {
      return *std::min_element(
        contributors.begin(), contributors.end(),
        [](const Cell & a, const Cell & b) {return a[0] < b[0];});
    };
  const auto parent_tile = buildParentTile<std::uint16_t>(
    parent_grid, {&child}, {0, 0}, nonZeroBand0, minSelect);

  for (uint16_t r = 0; r < parent_tile.edge; ++r) {
    for (uint16_t c = 0; c < parent_tile.edge; ++c) {
      const std::uint16_t v0 = parent_tile.get(r, c, 0);
      if (v0 == 0) {continue;}
      EXPECT_EQ(v0, 10);   // the 2x2 block's minimum
      EXPECT_EQ(parent_tile.get(r, c, 1), v0 + 1000)
        << "selected pair must stay coherent";
    }
  }
}

TEST(OverviewBuilder, MisgroupedChildThrowsRatherThanBeingDroppedSilently)
{
  // A child belonging to a different parent used to be skipped with no count —
  // its coverage would vanish from the pyramid unnoticed.
  const gggs::GridIndex fine = fineGrid();
  const gggs::GridIndex parent_grid = gggs::parent(fine);
  const std::vector<gggs::GridIndex> cousins =
    gggs::children(gggs::parent(parent_grid));
  gggs::GridIndex other_parent;
  for (const auto & g : cousins) {
    if (g != parent_grid) {other_parent = g;}
  }
  ASSERT_TRUE(other_parent.valid());
  const TiledRasterTile<std::uint16_t> stranger(gggs::children(other_parent).front(), 1, 5);

  EXPECT_THROW(
    buildParentTile<std::uint16_t>(
      parent_grid, {&stranger}, {0}, nonZeroBand0, meanFold),
    std::invalid_argument);
}

TEST(OverviewBuilder, ShortFoldResultThrowsRatherThanHalfWritingTheCell)
{
  const gggs::GridIndex fine = fineGrid();
  const TiledRasterTile<std::uint16_t> child(fine, 2, 42);
  const auto shortFold = [](const std::vector<Cell> &) {return Cell{1};};   // 1 of 2

  EXPECT_THROW(
    buildParentTile<std::uint16_t>(
      gggs::parent(fine), {&child}, {0, 0}, nonZeroBand0, shortFold),
    std::invalid_argument);
}

TEST(OverviewBuilder, LevelBuildGroupsByParentAndIsValueIdempotent)
{
  // Two adjacent fine grids (same parent in the common case; the grouping is by
  // gggs::parent either way) + a full level fold run twice -> identical values.
  const gggs::GridIndex fine_a = fineGrid();
  const std::vector<gggs::GridIndex> siblings = gggs::children(gggs::parent(fine_a));
  ASSERT_GE(siblings.size(), 2u);

  std::map<gggs::GridIndex, TiledRasterTile<std::uint16_t>> fine_tiles;
  std::uint16_t fill_value = 500;
  for (std::size_t i = 0; i < 2; ++i) {
    TiledRasterTile<std::uint16_t> t(siblings[i], 1, 0);
    for (uint16_t r = 0; r < t.edge; ++r) {
      for (uint16_t c = 0; c < t.edge; ++c) {
        t.set(r, c, 0, fill_value);
      }
    }
    fine_tiles.emplace(siblings[i], std::move(t));
    fill_value += 100;
  }

  const auto run = [&]() {
      return buildOverviewLevel<std::uint16_t>(
        fine_tiles, {0}, nonZeroBand0, meanFold);
    };
  const auto parents1 = run();
  const auto parents2 = run();

  ASSERT_FALSE(parents1.empty());
  for (const auto & item : parents1) {
    EXPECT_EQ(item.first, gggs::parent(fine_tiles.begin()->first));
  }
  ASSERT_EQ(parents1.size(), parents2.size());
  auto it1 = parents1.begin();
  auto it2 = parents2.begin();
  for (; it1 != parents1.end(); ++it1, ++it2) {
    EXPECT_EQ(it1->first, it2->first);
    EXPECT_EQ(it1->second.band(0), it2->second.band(0))
      << "fold must be value-idempotent (regenerable-cache contract)";
  }
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
