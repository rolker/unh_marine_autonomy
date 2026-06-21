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
#include <optional>

#include "marine_autonomy/gggs.h"
#include "marine_mbes_backscatter_store/mbes_store.hpp"
#include "marine_mbes_backscatter_store/query.hpp"

using marine_mbes_backscatter_store::bestSource;
using marine_mbes_backscatter_store::forEachCellBestSource;
using marine_mbes_backscatter_store::IntensitySample;
using marine_mbes_backscatter_store::MbesBackscatterStore;
using marine_mbes_backscatter_store::MbesCell;
using marine_mbes_backscatter_store::SourceLayer;

TEST(Query, BestSourcePrefersProcessedOverDraft)
{
  MbesBackscatterStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, cell, MbesCell{-12.0f, 2.0f, 2LL});
  store.set(SourceLayer::Processed, cell, MbesCell{-10.0f, 0.1f, 1LL});

  const auto best = bestSource(store, cell);
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->source, SourceLayer::Processed);
  EXPECT_FLOAT_EQ(best->intensity, -10.0f);
}

TEST(Query, BestSourceFallsBackToDraft)
{
  MbesBackscatterStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, cell, MbesCell{-12.0f, 2.0f, 2LL});

  const auto best = bestSource(store, cell);
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->source, SourceLayer::Draft);
  EXPECT_FLOAT_EQ(best->intensity, -12.0f);
}

TEST(Query, BestSourceNulloptWhenNoData)
{
  MbesBackscatterStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  EXPECT_FALSE(bestSource(store, cell).has_value());

  // A written-but-no-data cell (NaN intensity) is still unknown.
  store.set(SourceLayer::Draft, cell, MbesCell{});
  EXPECT_FALSE(bestSource(store, cell).has_value());
}

TEST(Query, ForEachCellBestSourceVisitsRegion)
{
  MbesBackscatterStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, cell, MbesCell{-20.0f, 0.5f, 1LL});

  std::size_t visited = 0;
  std::size_t with_data = 0;
  forEachCellBestSource(
    store, gggs::geoPoint(42.999, -70.501), gggs::geoPoint(43.001, -70.499),
    [&](const gggs::CellIndex &, const std::optional<IntensitySample> & sample) {
      ++visited;
      if (sample) {
        ++with_data;
      }
    });

  EXPECT_GT(visited, 0u);
  EXPECT_GE(with_data, 1u);   // the written cell falls inside the box
}

TEST(Query, ForEachCellBestSourceSkipsCellsOutsideBox)
{
  // A cell written well outside the queried box must not be visited.
  MbesBackscatterStore store(5);
  store.set(SourceLayer::Draft, store.cellIndex(45.0, -73.0), MbesCell{-20.0f, 0.5f, 1LL});

  std::size_t with_data = 0;
  forEachCellBestSource(
    store, gggs::geoPoint(42.999, -70.501), gggs::geoPoint(43.001, -70.499),
    [&](const gggs::CellIndex &, const std::optional<IntensitySample> & sample) {
      if (sample) {
        ++with_data;
      }
    });
  EXPECT_EQ(with_data, 0u);   // the only written cell is far outside the box
}
