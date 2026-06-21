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

#include <stdexcept>

#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"

using marine_bathymetry_store::BathyCell;
using marine_bathymetry_store::BathymetryStore;
using marine_bathymetry_store::SourceLayer;

TEST(Store, SetGetRoundTrip)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  // timestamp is int64 nanoseconds since the epoch (1 s = 1e9 ns).
  store.set(SourceLayer::Draft, cell, BathyCell{-30.0, 0.5, 1'000'000'000'000LL});

  const auto got = store.get(SourceLayer::Draft, cell);
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
  store.set(SourceLayer::Draft, cell, BathyCell{-30.0, 0.5, 2'000'000'000LL, 3u});

  const auto got = store.get(SourceLayer::Draft, cell);
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, -30.0);
  EXPECT_EQ(got->timestamp, 2'000'000'000LL);
  EXPECT_EQ(got->source_index, 3u);
}

TEST(Store, GetEmptyLayerIsNullopt)
{
  BathymetryStore store(5);
  EXPECT_FALSE(store.get(SourceLayer::Processed, store.cellIndex(43.0, -70.5)).has_value());
}

TEST(Store, LayersAreIndependent)
{
  // A draft write must not touch the processed layer at the same cell.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Processed, cell, BathyCell{-10.0, 0.1, 1LL});
  store.set(SourceLayer::Draft, cell, BathyCell{-12.0, 2.0, 2LL});
  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Processed, cell)->depth, -10.0);
  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Draft, cell)->depth, -12.0);
}

TEST(Store, TilesAllocatedLazilyPerLayer)
{
  BathymetryStore store(5);
  EXPECT_TRUE(store.tiles(SourceLayer::Draft).empty());
  EXPECT_TRUE(store.tiles(SourceLayer::Processed).empty());

  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 1LL});
  EXPECT_EQ(store.tiles(SourceLayer::Draft).size(), 1u);
  EXPECT_TRUE(store.tiles(SourceLayer::Processed).empty());
}

TEST(Store, NoDataCellReadsBackAsNoData)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, cell, BathyCell{});  // depth NaN
  const auto got = store.get(SourceLayer::Draft, cell);
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

  EXPECT_NO_THROW(store.set(SourceLayer::Draft, fine_cell, BathyCell{-22.0, 0.3, 1LL}));
  store.set(SourceLayer::Draft, default_cell, BathyCell{-20.0, 0.5, 2LL});

  // Both tiles exist in the same layer at different levels.
  EXPECT_EQ(store.tiles(SourceLayer::Draft).size(), 2u);
  const auto fine_got = store.get(SourceLayer::Draft, fine_cell);
  ASSERT_TRUE(fine_got.has_value());
  EXPECT_DOUBLE_EQ(fine_got->depth, -22.0);
  const auto default_got = store.get(SourceLayer::Draft, default_cell);
  ASSERT_TRUE(default_got.has_value());
  EXPECT_DOUBLE_EQ(default_got->depth, -20.0);
}

TEST(Store, InvalidCellThrows)
{
  BathymetryStore store(5);
  EXPECT_THROW(store.set(SourceLayer::Draft, gggs::CellIndex{}, BathyCell{}),
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
    store.set(SourceLayer::Chart, store.cellIndex(43.0, -70.5), BathyCell{-10.0, 3.0, 1LL}),
    std::logic_error);
  // Other layers are unaffected by the guard.
  EXPECT_NO_THROW(
    store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-12.0, 2.0, 2LL}));
}

TEST(Store, ChartWritableStoreAllowsChartSet)
{
  // The importer opts in; the converted prior then writes and reads back.
  BathymetryStore store(5, /*chart_writable=*/true);
  EXPECT_TRUE(store.chartWritable());
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Chart, cell, BathyCell{38.58, 3.0, 1000LL});
  const auto got = store.get(SourceLayer::Chart, cell);
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, 38.58);
}

TEST(Store, FromCellSizePropagatesChartWritable)
{
  auto store = BathymetryStore::fromCellSize(30.0f, /*chart_writable=*/true);
  EXPECT_TRUE(store.chartWritable());
  EXPECT_NO_THROW(
    store.set(SourceLayer::Chart, store.cellIndex(43.0, -70.5), BathyCell{40.0, 3.0, 1LL}));
}
