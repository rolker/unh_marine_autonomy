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
#include "marine_mbes_backscatter_store/mbes_store.hpp"

using marine_mbes_backscatter_store::MbesBackscatterStore;
using marine_mbes_backscatter_store::MbesCell;
using marine_mbes_backscatter_store::SourceLayer;

TEST(Store, SetGetRoundTrip)
{
  MbesBackscatterStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Survey, cell, MbesCell{-18.5f, 0.25f, 0.75f});

  const auto got = store.get(SourceLayer::Survey, cell);
  ASSERT_TRUE(got.has_value());
  EXPECT_FLOAT_EQ(got->mean, -18.5f);
  EXPECT_FLOAT_EQ(got->standard_error, 0.25f);
  EXPECT_FLOAT_EQ(got->sample_sd, 0.75f);
}

TEST(Store, SurveyRecencyNewestWins)
{
  // ADR-0007 D7: newest-valid-wins — a second write at the same cell overwrites
  // the first (the store does no accumulation).
  MbesBackscatterStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Survey, cell, MbesCell{-20.0f, 1.0f, 2.0f});
  store.set(SourceLayer::Survey, cell, MbesCell{-15.0f, 0.5f, 1.0f});

  const auto got = store.get(SourceLayer::Survey, cell);
  ASSERT_TRUE(got.has_value());
  EXPECT_FLOAT_EQ(got->mean, -15.0f);          // second write wins
  EXPECT_FLOAT_EQ(got->standard_error, 0.5f);
  EXPECT_FLOAT_EQ(got->sample_sd, 1.0f);
}

TEST(Store, UnknownCellReturnsNullopt)
{
  MbesBackscatterStore store(5);
  EXPECT_FALSE(store.get(SourceLayer::Survey, store.cellIndex(43.0, -70.5)).has_value());
}

TEST(Store, TilesAllocatedLazily)
{
  MbesBackscatterStore store(5);
  EXPECT_TRUE(store.tiles(SourceLayer::Survey).empty());

  store.set(SourceLayer::Survey, store.cellIndex(43.0, -70.5), MbesCell{-30.0f, 0.5f, 1.0f});
  EXPECT_EQ(store.tiles(SourceLayer::Survey).size(), 1u);
}

TEST(Store, NoDataCellReadsBackAsNoData)
{
  MbesBackscatterStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Survey, cell, MbesCell{});  // mean NaN
  const auto got = store.get(SourceLayer::Survey, cell);
  ASSERT_TRUE(got.has_value());      // tile exists
  EXPECT_FALSE(got->hasData());      // but the cell carries no usable mean
}

TEST(Store, OneSampleSentinelIsData)
{
  // n=1 sentinel (#248 A.1): sample_sd == 0 with a finite mean is real data
  // (n=1, M2=0), distinct from no-data (NaN mean).
  MbesBackscatterStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Survey, cell, MbesCell{-12.0f, 0.0f, 0.0f});
  const auto got = store.get(SourceLayer::Survey, cell);
  ASSERT_TRUE(got.has_value());
  EXPECT_TRUE(got->hasData());
  EXPECT_FLOAT_EQ(got->mean, -12.0f);
  EXPECT_FLOAT_EQ(got->sample_sd, 0.0f);
}

TEST(Store, WrongLevelCellThrows)
{
  MbesBackscatterStore store(5);
  gggs::Level other(6);
  const auto level6_cell = other.cellIndex(gggs::geoPoint(43.0, -70.5));
  EXPECT_THROW(store.set(SourceLayer::Survey, level6_cell, MbesCell{}), std::invalid_argument);
}

TEST(Store, InvalidCellThrows)
{
  MbesBackscatterStore store(5);
  EXPECT_THROW(
    store.set(SourceLayer::Survey, gggs::CellIndex{}, MbesCell{}), std::invalid_argument);
}

TEST(Store, FromCellSizeChoosesAFiniteLevel)
{
  const auto store = MbesBackscatterStore::fromCellSize(5.0f);
  EXPECT_TRUE(store.cellIndex(43.0, -70.5).valid());
}
