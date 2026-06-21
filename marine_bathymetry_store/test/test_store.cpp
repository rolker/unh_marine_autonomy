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

#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"

using marine_bathymetry_store::BathyCell;
using marine_bathymetry_store::BathymetryStore;
using marine_bathymetry_store::SourceLayer;

// A single ISO-date epoch label for the single-epoch store tests (ADR-0002 A1).
static const char * const kEpoch = "2026-06-10";

// Tile count of one (layer, epoch) — 0 if the epoch is absent.
static std::size_t tilesIn(
  const BathymetryStore & store, SourceLayer layer, const std::string & epoch)
{
  const auto & epochs = store.epochs(layer);
  const auto it = epochs.find(epoch);
  return it == epochs.end() ? 0u : it->second.tiles.size();
}

TEST(Store, SetGetRoundTrip)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  // timestamp is int64 nanoseconds since the epoch (1 s = 1e9 ns).
  store.set(SourceLayer::Draft, kEpoch, cell, BathyCell{-30.0, 0.5, 1'000'000'000'000LL});

  const auto got = store.get(SourceLayer::Draft, kEpoch, cell);
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, -30.0);
  EXPECT_DOUBLE_EQ(got->uncertainty, 0.5);
  EXPECT_EQ(got->timestamp, 1'000'000'000'000LL);
  EXPECT_EQ(got->source_index, 0u);   // default unset
}

TEST(Store, SetGetRoundTripWithSourceIndex)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, kEpoch, cell, BathyCell{-30.0, 0.5, 2'000'000'000LL, 3u});

  const auto got = store.get(SourceLayer::Draft, kEpoch, cell);
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, -30.0);
  EXPECT_EQ(got->timestamp, 2'000'000'000LL);
  EXPECT_EQ(got->source_index, 3u);
}

TEST(Store, GetEmptyLayerIsNullopt)
{
  BathymetryStore store(5);
  EXPECT_FALSE(store.get(SourceLayer::Processed, kEpoch, store.cellIndex(43.0, -70.5)).has_value());
}

TEST(Store, LayersAreIndependent)
{
  // A draft write must not touch the processed layer at the same cell.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Processed, kEpoch, cell, BathyCell{-10.0, 0.1, 1LL});
  store.set(SourceLayer::Draft, kEpoch, cell, BathyCell{-12.0, 2.0, 2LL});
  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Processed, kEpoch, cell)->depth, -10.0);
  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Draft, kEpoch, cell)->depth, -12.0);
}

TEST(Store, TilesAllocatedLazilyPerLayer)
{
  BathymetryStore store(5);
  EXPECT_TRUE(store.epochs(SourceLayer::Draft).empty());
  EXPECT_TRUE(store.epochs(SourceLayer::Processed).empty());

  store.set(SourceLayer::Draft, kEpoch, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 1LL});
  EXPECT_EQ(tilesIn(store, SourceLayer::Draft, kEpoch), 1u);
  EXPECT_TRUE(store.epochs(SourceLayer::Processed).empty());
}

TEST(Store, NoDataCellReadsBackAsNoData)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, kEpoch, cell, BathyCell{});  // depth NaN
  const auto got = store.get(SourceLayer::Draft, kEpoch, cell);
  ASSERT_TRUE(got.has_value());      // tile exists
  EXPECT_FALSE(got->hasData());      // but the cell carries no usable depth
}

TEST(Store, MultiLevelTilesCoexist)
{
  // The store is multi-level (ADR-0002 §D2): a cell at a level other than the
  // store's default level is accepted, stored, and read back independently.
  BathymetryStore store(5);
  gggs::Level fine(6);
  const auto fine_cell = fine.cellIndex(gggs::geoPoint(43.0, -70.5));
  const auto default_cell = store.cellIndex(43.0, -70.5);
  ASSERT_NE(fine_cell.level(), default_cell.level());

  EXPECT_NO_THROW(store.set(SourceLayer::Draft, kEpoch, fine_cell, BathyCell{-22.0, 0.3, 1LL}));
  store.set(SourceLayer::Draft, kEpoch, default_cell, BathyCell{-20.0, 0.5, 2LL});

  // Both tiles exist in the same layer/epoch at different levels.
  EXPECT_EQ(tilesIn(store, SourceLayer::Draft, kEpoch), 2u);
  const auto fine_got = store.get(SourceLayer::Draft, kEpoch, fine_cell);
  ASSERT_TRUE(fine_got.has_value());
  EXPECT_DOUBLE_EQ(fine_got->depth, -22.0);
  const auto default_got = store.get(SourceLayer::Draft, kEpoch, default_cell);
  ASSERT_TRUE(default_got.has_value());
  EXPECT_DOUBLE_EQ(default_got->depth, -20.0);
}

TEST(Store, InvalidCellThrows)
{
  BathymetryStore store(5);
  EXPECT_THROW(store.set(SourceLayer::Draft, kEpoch, gggs::CellIndex{}, BathyCell{}),
    std::invalid_argument);
}

TEST(Store, FromCellSizeChoosesAFiniteLevel)
{
  const auto store = BathymetryStore::fromCellSize(30.0f);
  // The chosen level must produce valid cells for a normal position.
  EXPECT_TRUE(store.cellIndex(43.0, -70.5).valid());
}

TEST(Store, ChartIsReadOnlyByDefault)
{
  // The contour prior must be unclobberable by live ingest: set(Chart) throws
  // unless the store was explicitly opened chart_writable (ADR-0002 §D3).
  BathymetryStore store(5);
  EXPECT_FALSE(store.chartWritable());
  EXPECT_THROW(
    store.set(SourceLayer::Chart, kEpoch, store.cellIndex(43.0, -70.5), BathyCell{-10.0, 3.0, 1LL}),
    std::logic_error);
  // Other layers are unaffected by the guard.
  const auto cell = store.cellIndex(43.0, -70.5);
  EXPECT_NO_THROW(store.set(SourceLayer::Draft, kEpoch, cell, BathyCell{-12.0, 2.0, 2LL}));
}

TEST(Store, ChartWritableStoreAllowsChartSet)
{
  // The importer opts in; the converted prior then writes and reads back.
  BathymetryStore store(5, /*chart_writable=*/true);
  EXPECT_TRUE(store.chartWritable());
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Chart, kEpoch, cell, BathyCell{38.58, 3.0, 1000LL});
  const auto got = store.get(SourceLayer::Chart, kEpoch, cell);
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, 38.58);
}

TEST(Store, FromCellSizePropagatesChartWritable)
{
  auto store = BathymetryStore::fromCellSize(30.0f, /*chart_writable=*/true);
  EXPECT_TRUE(store.chartWritable());
  EXPECT_NO_THROW(
    store.set(SourceLayer::Chart, kEpoch, store.cellIndex(43.0, -70.5), BathyCell{40.0, 3.0, 1LL}));
}

TEST(Store, InvalidEpochLabelThrows)
{
  // Epoch labels must be filesystem-safe single components (validateEpochLabel).
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  EXPECT_THROW(store.set(SourceLayer::Draft, "../escape", cell, BathyCell{}),
    std::invalid_argument);
  EXPECT_THROW(store.set(SourceLayer::Draft, "", cell, BathyCell{}), std::invalid_argument);
}

namespace
{
// Build a one-cell tile map for importEpoch from a (cell, value) pair.
std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> oneTile(
  const gggs::CellIndex & cell, const BathyCell & value)
{
  std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
  marine_bathymetry_store::BathymetryTile tile(cell.grid());
  tile.set(cell.row(), cell.column(), value);
  tiles.emplace(cell.grid(), std::move(tile));
  return tiles;
}
}  // namespace

TEST(Store, ImportEpochReplacesWholesale)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  EXPECT_TRUE(store.importEpoch(
      SourceLayer::Draft, kEpoch, oneTile(cell, BathyCell{-30.0, 0.5, 1LL}),
      marine_bathymetry_store::Provenance::Replayed));
  const auto got = store.get(SourceLayer::Draft, kEpoch, cell);
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, -30.0);
}

TEST(Store, ReplayedEpochIsImmutableToLiveWrites)
{
  // ADR-0002 §A1.2: a compacted (Replayed) epoch must not be regressed by a
  // later live (LiveFused) write or import — both are no-ops returning false.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.importEpoch(
    SourceLayer::Draft, kEpoch, oneTile(cell, BathyCell{-30.0, 0.5, 1LL}),
    marine_bathymetry_store::Provenance::Replayed);

  // A live per-cell write is refused (no-op).
  EXPECT_FALSE(store.set(SourceLayer::Draft, kEpoch, cell, BathyCell{-99.0, 9.0, 2LL}));
  // A live-fused wholesale import is refused too.
  EXPECT_FALSE(store.importEpoch(
      SourceLayer::Draft, kEpoch, oneTile(cell, BathyCell{-99.0, 9.0, 2LL}),
      marine_bathymetry_store::Provenance::LiveFused));
  // The replayed value is intact.
  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Draft, kEpoch, cell)->depth, -30.0);

  // A re-compaction (Replayed over Replayed) IS allowed.
  EXPECT_TRUE(store.importEpoch(
      SourceLayer::Draft, kEpoch, oneTile(cell, BathyCell{-28.0, 0.4, 3LL}),
      marine_bathymetry_store::Provenance::Replayed));
  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Draft, kEpoch, cell)->depth, -28.0);
}

TEST(Store, ImportEpochRejectsTileKeyMismatch)
{
  // A tile must be built for the grid it is keyed under (harvested #148 fix).
  BathymetryStore store(5);
  const auto cell_a = store.cellIndex(43.0, -70.5);
  const auto cell_b = store.cellIndex(44.0, -71.0);
  ASSERT_FALSE(cell_a.grid() == cell_b.grid());

  std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
  // Tile built for grid_b but inserted under grid_a's key.
  marine_bathymetry_store::BathymetryTile mismatched(cell_b.grid());
  tiles.emplace(cell_a.grid(), std::move(mismatched));
  EXPECT_THROW(
    store.importEpoch(SourceLayer::Draft, kEpoch, std::move(tiles),
    marine_bathymetry_store::Provenance::Replayed),
    std::invalid_argument);
}
