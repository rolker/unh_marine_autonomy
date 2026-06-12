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

#include <cmath>
#include <cstddef>
#include <optional>

#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/query.hpp"

using marine_bathymetry_store::BathyCell;
using marine_bathymetry_store::BathymetryStore;
using marine_bathymetry_store::bestSource;
using marine_bathymetry_store::DepthSample;
using marine_bathymetry_store::forEachCellBestSource;
using marine_bathymetry_store::forEachChangedCell;
using marine_bathymetry_store::shallowestReliable;
using marine_bathymetry_store::SourceLayer;

namespace
{

constexpr const char * kDay1 = "2026-06-09";
constexpr const char * kDay2 = "2026-06-10";
constexpr const char * kDay3 = "2026-06-11";

}  // namespace

TEST(Query, BestSourcePrefersHigherPriorityLayer)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, kDay1, cell, BathyCell{-12.0, 2.0, 2.0});
  store.set(SourceLayer::Processed, kDay1, cell, BathyCell{-10.0, 0.1, 1.0});

  const auto best = bestSource(store, cell);
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->source, SourceLayer::Processed);
  EXPECT_DOUBLE_EQ(best->depth, -10.0);
}

TEST(Query, BestSourceFallsBackToLowerPriority)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, kDay1, cell, BathyCell{-12.0, 2.0, 2.0});

  const auto best = bestSource(store, cell);
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->source, SourceLayer::Draft);
  EXPECT_EQ(best->epoch, kDay1);
}

TEST(Query, BestSourcePicksNewestEpochWithinLayer)
{
  // Default resolution: latest epoch per cell (ADR-0002 §A1.3).
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, kDay1, cell, BathyCell{-30.0, 0.5, 1.0});
  store.set(SourceLayer::Draft, kDay2, cell, BathyCell{-28.0, 0.6, 2.0});

  const auto best = bestSource(store, cell);
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->epoch, kDay2);
  EXPECT_DOUBLE_EQ(best->depth, -28.0);
}

TEST(Query, BestSourceFallsThroughEpochWithoutCoverage)
{
  // A newer epoch that didn't cover this cell falls through to an older one —
  // coverage gaps don't hide prior knowledge.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  const auto elsewhere = store.cellIndex(43.2, -70.2);
  store.set(SourceLayer::Draft, kDay1, cell, BathyCell{-30.0, 0.5, 1.0});
  store.set(SourceLayer::Draft, kDay2, elsewhere, BathyCell{-5.0, 0.5, 2.0});

  const auto best = bestSource(store, cell);
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->epoch, kDay1);
  EXPECT_DOUBLE_EQ(best->depth, -30.0);
}

TEST(Query, UnknownCellIsNullopt)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  EXPECT_FALSE(bestSource(store, cell).has_value());

  // A written-but-no-data cell is still unknown (not "deep water").
  store.set(SourceLayer::Draft, kDay1, cell, BathyCell{});
  EXPECT_FALSE(bestSource(store, cell).has_value());
}

TEST(Query, ShallowestReliablePicksGreatestHeight)
{
  // depth is ellipsoidal height (up-positive): shallower == greater value.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Processed, kDay1, cell, BathyCell{-30.0, 0.1, 1.0});  // deeper
  store.set(SourceLayer::Draft, kDay1, cell, BathyCell{-25.0, 0.2, 2.0});      // shallower

  const auto result = shallowestReliable(store, cell, 1.0);
  ASSERT_TRUE(result.has_value());
  EXPECT_DOUBLE_EQ(result->depth, -25.0);
  EXPECT_EQ(result->source, SourceLayer::Draft);
}

TEST(Query, ShallowestReliableExcludesOverUncertain)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  // reliable, deeper:
  store.set(SourceLayer::Processed, kDay1, cell, BathyCell{-30.0, 0.1, 1.0});
  // shallower, too uncertain:
  store.set(SourceLayer::Draft, kDay1, cell, BathyCell{-25.0, 5.0, 2.0});

  const auto result = shallowestReliable(store, cell, 1.0);
  ASSERT_TRUE(result.has_value());
  EXPECT_DOUBLE_EQ(result->depth, -30.0);   // draft excluded
  EXPECT_EQ(result->source, SourceLayer::Processed);
}

TEST(Query, ShallowestReliableFallsThroughNoisyFreshEpoch)
{
  // The §A1.3 safety walk: yesterday saw a confident 2 m shoal; today's first
  // pass over the same cell is noisy. The noisy value fails the gate and the
  // query falls through to yesterday — the shoal keeps protecting navigation.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, kDay1, cell, BathyCell{-2.0, 0.1, 1.0});   // confident shoal
  store.set(SourceLayer::Draft, kDay2, cell, BathyCell{-4.0, 3.0, 2.0});   // noisy fresh pass

  const auto result = shallowestReliable(store, cell, 1.0);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->epoch, kDay1);
  EXPECT_DOUBLE_EQ(result->depth, -2.0);

  // Once today's pass becomes confident, today governs.
  store.set(SourceLayer::Draft, kDay2, cell, BathyCell{-4.0, 0.2, 3.0});
  const auto later = shallowestReliable(store, cell, 1.0);
  ASSERT_TRUE(later.has_value());
  EXPECT_EQ(later->epoch, kDay2);
  EXPECT_DOUBLE_EQ(later->depth, -4.0);
}

TEST(Query, ShallowestReliableTreatsNaNUncertaintyAsUnreliable)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, kDay1, cell, BathyCell{-25.0, std::nan(""), 2.0});
  EXPECT_FALSE(shallowestReliable(store, cell, 1.0).has_value());
}

TEST(Query, ForEachRegionVisitsCoveredCells)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, kDay1, cell, BathyCell{-20.0, 0.5, 1.0});

  std::size_t visited = 0;
  std::size_t with_data = 0;
  forEachCellBestSource(
    store, gggs::geoPoint(42.999, -70.501), gggs::geoPoint(43.001, -70.499),
    [&](const gggs::CellIndex &, const std::optional<DepthSample> & sample) {
      ++visited;
      if (sample) {
        ++with_data;
      }
    });

  EXPECT_GT(visited, 0u);
  EXPECT_GE(with_data, 1u);   // the written cell falls inside the box
}

TEST(Query, ChangedCellsVisitsOnlyDoublyObserved)
{
  // The change map (ADR-0002 §A1.1): cells observed in both epochs are
  // visited with both records; single-epoch cells are coverage, not change.
  BathymetryStore store(5);
  const auto both = store.cellIndex(43.0, -70.5);
  const auto only_day1 = store.cellIndex(43.2, -70.2);
  store.set(SourceLayer::Draft, kDay1, both, BathyCell{-30.0, 0.5, 1.0});
  store.set(SourceLayer::Draft, kDay1, only_day1, BathyCell{-20.0, 0.5, 1.0});
  store.set(SourceLayer::Draft, kDay2, both, BathyCell{-28.0, 0.4, 2.0});

  std::size_t visited = 0;
  double delta = 0.0;
  const auto count = forEachChangedCell(
    store, SourceLayer::Draft, kDay1, kDay2,
    [&](const gggs::CellIndex &, const BathyCell & a, const BathyCell & b) {
      ++visited;
      delta = b.depth - a.depth;
    });

  EXPECT_EQ(count, visited);
  EXPECT_EQ(visited, 1u);          // only the doubly-observed cell
  EXPECT_DOUBLE_EQ(delta, 2.0);    // shoaled by 2 m between the days
}

TEST(Query, ChartLayerRanksBelowDraftAndFillsGaps)
{
  // The chart prior must never shadow survey data, but must answer where
  // nothing else does.
  BathymetryStore store(5);
  const auto surveyed = store.cellIndex(43.0, -70.5);
  const auto unsurveyed = store.cellIndex(43.2, -70.2);
  store.set(SourceLayer::Chart, kDay1, surveyed, BathyCell{-10.0, 3.0, 1.0});
  store.set(SourceLayer::Chart, kDay1, unsurveyed, BathyCell{-8.0, 3.0, 1.0});
  store.set(SourceLayer::Draft, kDay2, surveyed, BathyCell{-12.0, 0.3, 2.0});

  const auto at_surveyed = bestSource(store, surveyed);
  ASSERT_TRUE(at_surveyed.has_value());
  EXPECT_EQ(at_surveyed->source, SourceLayer::Draft);   // survey beats prior
  EXPECT_DOUBLE_EQ(at_surveyed->depth, -12.0);

  const auto at_unsurveyed = bestSource(store, unsurveyed);
  ASSERT_TRUE(at_unsurveyed.has_value());
  EXPECT_EQ(at_unsurveyed->source, SourceLayer::Chart);  // prior fills the gap
  EXPECT_DOUBLE_EQ(at_unsurveyed->depth, -8.0);

  // Under a strict gate the high-uncertainty prior is not "reliable".
  EXPECT_FALSE(shallowestReliable(store, unsurveyed, 1.0).has_value());
  // Under a gate that admits it, it answers.
  EXPECT_TRUE(shallowestReliable(store, unsurveyed, 5.0).has_value());
}

TEST(Query, ChangedCellsMissingEpochIsEmpty)
{
  BathymetryStore store(5);
  store.set(SourceLayer::Draft, kDay1, store.cellIndex(43.0, -70.5),
    BathyCell{-30.0, 0.5, 1.0});
  const auto count = forEachChangedCell(
    store, SourceLayer::Draft, kDay1, kDay3,
    [](const gggs::CellIndex &, const BathyCell &, const BathyCell &) {
      FAIL() << "no cells should be visited when an epoch is absent";
    });
  EXPECT_EQ(count, 0u);
}
