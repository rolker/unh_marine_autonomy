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

#include <map>
#include <stdexcept>
#include <utility>

#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"

using marine_bathymetry_store::BathyCell;
using marine_bathymetry_store::BathymetryStore;
using marine_bathymetry_store::BathymetryTile;
using marine_bathymetry_store::Provenance;
using marine_bathymetry_store::SourceLayer;

namespace
{

constexpr const char * kDay1 = "2026-06-09";
constexpr const char * kDay2 = "2026-06-10";

/// Build a single-tile map holding @p value at @p cell, for importEpoch.
std::map<gggs::GridIndex, BathymetryTile> oneTile(
  const gggs::CellIndex & cell, const BathyCell & value)
{
  std::map<gggs::GridIndex, BathymetryTile> tiles;
  BathymetryTile tile(cell.grid());
  tile.set(cell.row(), cell.column(), value);
  tiles.emplace(cell.grid(), std::move(tile));
  return tiles;
}

}  // namespace

TEST(Store, SetGetRoundTrip)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  EXPECT_TRUE(store.set(SourceLayer::Draft, kDay1, cell, BathyCell{-30.0, 0.5, 1000.0}));

  const auto got = store.get(SourceLayer::Draft, kDay1, cell);
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, -30.0);
  EXPECT_DOUBLE_EQ(got->uncertainty, 0.5);
  EXPECT_DOUBLE_EQ(got->timestamp, 1000.0);
}

TEST(Store, GetEmptyLayerIsNullopt)
{
  BathymetryStore store(5);
  EXPECT_FALSE(
    store.get(SourceLayer::Processed, kDay1, store.cellIndex(43.0, -70.5)).has_value());
}

TEST(Store, LayersAreIndependent)
{
  // A draft write must not touch the processed layer at the same cell.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Processed, kDay1, cell, BathyCell{-10.0, 0.1, 1.0});
  store.set(SourceLayer::Draft, kDay1, cell, BathyCell{-12.0, 2.0, 2.0});
  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Processed, kDay1, cell)->depth, -10.0);
  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Draft, kDay1, cell)->depth, -12.0);
}

TEST(Store, EpochsAreIndependent)
{
  // Day 2's write must not clobber day 1's value — epochs are never fused
  // (ADR-0002 §A1.1); a cell surveyed on N days keeps N records.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, kDay1, cell, BathyCell{-30.0, 0.5, 1.0});
  store.set(SourceLayer::Draft, kDay2, cell, BathyCell{-28.0, 0.6, 2.0});
  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Draft, kDay1, cell)->depth, -30.0);
  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Draft, kDay2, cell)->depth, -28.0);
}

TEST(Store, EpochsOrderChronologically)
{
  // ISO-date labels sort as strings, so the map's order is acquisition order
  // and rbegin() is the newest epoch — load-bearing for query resolution.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, kDay2, cell, BathyCell{-28.0, 0.6, 2.0});
  store.set(SourceLayer::Draft, kDay1, cell, BathyCell{-30.0, 0.5, 1.0});
  const auto & epochs = store.epochs(SourceLayer::Draft);
  ASSERT_EQ(epochs.size(), 2u);
  EXPECT_EQ(epochs.begin()->first, kDay1);
  EXPECT_EQ(epochs.rbegin()->first, kDay2);
}

TEST(Store, TilesAllocatedLazilyPerLayerAndEpoch)
{
  BathymetryStore store(5);
  EXPECT_TRUE(store.epochs(SourceLayer::Draft).empty());
  EXPECT_TRUE(store.epochs(SourceLayer::Processed).empty());

  store.set(SourceLayer::Draft, kDay1, store.cellIndex(43.0, -70.5),
    BathyCell{-30.0, 0.5, 1.0});
  ASSERT_EQ(store.epochs(SourceLayer::Draft).size(), 1u);
  EXPECT_EQ(store.epochs(SourceLayer::Draft).at(kDay1).tiles.size(), 1u);
  EXPECT_TRUE(store.epochs(SourceLayer::Processed).empty());
}

TEST(Store, NoDataCellReadsBackAsNoData)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, kDay1, cell, BathyCell{});  // depth NaN
  const auto got = store.get(SourceLayer::Draft, kDay1, cell);
  ASSERT_TRUE(got.has_value());      // tile exists
  EXPECT_FALSE(got->hasData());      // but the cell carries no usable depth
}

TEST(Store, WrongLevelCellThrows)
{
  BathymetryStore store(5);
  gggs::Level other(6);
  const auto level6_cell = other.cellIndex(gggs::geoPoint(43.0, -70.5));
  EXPECT_THROW(
    store.set(SourceLayer::Draft, kDay1, level6_cell, BathyCell{}), std::invalid_argument);
}

TEST(Store, InvalidCellThrows)
{
  BathymetryStore store(5);
  EXPECT_THROW(store.set(SourceLayer::Draft, kDay1, gggs::CellIndex{}, BathyCell{}),
    std::invalid_argument);
}

TEST(Store, InvalidEpochLabelThrows)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  EXPECT_THROW(store.set(SourceLayer::Draft, "", cell, BathyCell{}), std::invalid_argument);
  EXPECT_THROW(
    store.set(SourceLayer::Draft, "2026/06/09", cell, BathyCell{}), std::invalid_argument);
  EXPECT_THROW(store.set(SourceLayer::Draft, "..", cell, BathyCell{}), std::invalid_argument);
}

TEST(Store, FromCellSizeChoosesAFiniteLevel)
{
  const auto store = BathymetryStore::fromCellSize(30.0f);
  // The chosen level must produce valid cells for a normal position.
  EXPECT_TRUE(store.cellIndex(43.0, -70.5).valid());
}

TEST(Store, ImportEpochSupersedesWholesale)
{
  // Compaction replaces the whole epoch (ADR-0002 §A1.2): cells of the live
  // surface not covered by the replay product must be gone, not mixed in.
  BathymetryStore store(5);
  const auto cell_a = store.cellIndex(43.0, -70.5);
  const auto cell_b = store.cellIndex(43.2, -70.2);  // far enough to be a distinct cell
  store.set(SourceLayer::Draft, kDay1, cell_a, BathyCell{-30.0, 0.5, 1.0});
  store.set(SourceLayer::Draft, kDay1, cell_b, BathyCell{-31.0, 0.5, 1.0});

  // Replay product covers only cell_a, with a refined value.
  EXPECT_TRUE(
    store.importEpoch(
      SourceLayer::Draft, kDay1, oneTile(cell_a, BathyCell{-29.5, 0.2, 2.0}),
      Provenance::Replayed));

  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Draft, kDay1, cell_a)->depth, -29.5);
  const auto b = store.get(SourceLayer::Draft, kDay1, cell_b);
  // cell_b is either absent (its grid wasn't imported) or no-data (same grid,
  // fresh tile) — wholesale means the old value cannot survive either way.
  EXPECT_TRUE(!b.has_value() || !b->hasData());
  EXPECT_EQ(
    store.epochs(SourceLayer::Draft).at(kDay1).provenance, Provenance::Replayed);
}

TEST(Store, LiveFusedNeverReplacesReplayed)
{
  // §A1.2 ordering: replayed beats live-fused, never the reverse.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  ASSERT_TRUE(
    store.importEpoch(
      SourceLayer::Draft, kDay1, oneTile(cell, BathyCell{-29.5, 0.2, 2.0}),
      Provenance::Replayed));

  // A whole live import must be refused...
  EXPECT_FALSE(
    store.importEpoch(
      SourceLayer::Draft, kDay1, oneTile(cell, BathyCell{-50.0, 5.0, 3.0}),
      Provenance::LiveFused));
  // ...and so must a stray live cell write (late snapshot after compaction).
  EXPECT_FALSE(store.set(SourceLayer::Draft, kDay1, cell, BathyCell{-50.0, 5.0, 3.0}));

  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Draft, kDay1, cell)->depth, -29.5);
}

TEST(Store, RecompactionIsAllowed)
{
  // Replayed-over-replayed is legitimate (a re-run compaction supersedes).
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  ASSERT_TRUE(
    store.importEpoch(
      SourceLayer::Draft, kDay1, oneTile(cell, BathyCell{-29.5, 0.2, 2.0}),
      Provenance::Replayed));
  EXPECT_TRUE(
    store.importEpoch(
      SourceLayer::Draft, kDay1, oneTile(cell, BathyCell{-29.6, 0.15, 3.0}),
      Provenance::Replayed));
  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Draft, kDay1, cell)->depth, -29.6);
}

TEST(Store, ImportEpochMarksTilesDirtyAndSupersedesDisk)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  ASSERT_TRUE(
    store.importEpoch(
      SourceLayer::Draft, kDay1, oneTile(cell, BathyCell{-29.5, 0.2, 2.0}),
      Provenance::Replayed));
  const auto & epoch = store.epochs(SourceLayer::Draft).at(kDay1);
  EXPECT_TRUE(epoch.supersedes_disk);
  for (const auto & [grid, tile] : epoch.tiles) {
    static_cast<void>(grid);
    EXPECT_TRUE(tile.dirty());
  }
}

TEST(Store, ImportEpochWrongLevelTileThrows)
{
  BathymetryStore store(5);
  gggs::Level other(6);
  const auto level6_cell = other.cellIndex(gggs::geoPoint(43.0, -70.5));
  EXPECT_THROW(
    store.importEpoch(
      SourceLayer::Draft, kDay1, oneTile(level6_cell, BathyCell{-29.5, 0.2, 2.0}),
      Provenance::Replayed),
    std::invalid_argument);
}
