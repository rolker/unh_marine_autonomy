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
using marine_bathymetry_store::shallowestReliable;
using marine_bathymetry_store::SourceLayer;

// A single ISO-date epoch label for the single-epoch query tests (ADR-0002 A1).
static const char * const kEpoch = "2026-06-10";

TEST(Query, BestSourcePrefersHigherPriorityLayer)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, kEpoch, cell, BathyCell{-12.0, 2.0, 2LL});
  store.set(SourceLayer::Processed, kEpoch, cell, BathyCell{-10.0, 0.1, 1LL});

  const auto best = bestSource(store, cell);
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->source, SourceLayer::Processed);
  EXPECT_DOUBLE_EQ(best->depth, -10.0);
}

TEST(Query, BestSourceFallsBackToLowerPriority)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, kEpoch, cell, BathyCell{-12.0, 2.0, 2LL});

  const auto best = bestSource(store, cell);
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->source, SourceLayer::Draft);
}

TEST(Query, UnknownCellIsNullopt)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  EXPECT_FALSE(bestSource(store, cell).has_value());

  // A written-but-no-data cell is still unknown (not "deep water").
  store.set(SourceLayer::Draft, kEpoch, cell, BathyCell{});
  EXPECT_FALSE(bestSource(store, cell).has_value());
}

TEST(Query, ShallowestReliablePicksGreatestHeight)
{
  // depth is ellipsoidal height (up-positive): shallower == greater value.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Processed, kEpoch, cell, BathyCell{-30.0, 0.1, 1LL});  // deeper
  store.set(SourceLayer::Draft, kEpoch, cell, BathyCell{-25.0, 0.2, 2LL});      // shallower

  const auto result = shallowestReliable(store, cell, 1.0);
  ASSERT_TRUE(result.has_value());
  EXPECT_DOUBLE_EQ(result->depth, -25.0);
  EXPECT_EQ(result->source, SourceLayer::Draft);
}

TEST(Query, ShallowestReliableExcludesOverUncertain)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Processed, kEpoch, cell, BathyCell{-30.0, 0.1, 1LL});  // reliable, deeper
  // shallower, but too uncertain to be reliable:
  store.set(SourceLayer::Draft, kEpoch, cell, BathyCell{-25.0, 5.0, 2LL});

  const auto result = shallowestReliable(store, cell, 1.0);
  ASSERT_TRUE(result.has_value());
  EXPECT_DOUBLE_EQ(result->depth, -30.0);   // draft excluded
  EXPECT_EQ(result->source, SourceLayer::Processed);
}

TEST(Query, ShallowestReliableTreatsNaNUncertaintyAsUnreliable)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, kEpoch, cell, BathyCell{-25.0, std::nan(""), 2LL});
  EXPECT_FALSE(shallowestReliable(store, cell, 1.0).has_value());
}

TEST(Query, ForEachRegionVisitsCoveredCells)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, kEpoch, cell, BathyCell{-20.0, 0.5, 1LL});

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

TEST(Query, BestSourceFallsThroughToChartPrior)
{
  // Chart is the lowest-priority prior: used only where nothing newer exists,
  // and overridden by Draft/Processed where they do (ADR-0002 §D3).
  BathymetryStore store(5, /*chart_writable=*/true);
  const auto unsurveyed = store.cellIndex(43.0, -70.5);
  const auto surveyed = store.cellIndex(43.0, -70.4);

  store.set(SourceLayer::Chart, kEpoch, unsurveyed, BathyCell{38.0, 3.0, 1LL});
  store.set(SourceLayer::Chart, kEpoch, surveyed, BathyCell{38.0, 3.0, 1LL});
  store.set(SourceLayer::Draft, kEpoch, surveyed, BathyCell{40.0, 0.5, 2LL});

  // Unsurveyed cell falls through to the chart prior.
  const auto a = bestSource(store, unsurveyed);
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->source, SourceLayer::Chart);
  EXPECT_DOUBLE_EQ(a->depth, 38.0);

  // Surveyed cell prefers the live draft over the chart prior.
  const auto b = bestSource(store, surveyed);
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->source, SourceLayer::Draft);
  EXPECT_DOUBLE_EQ(b->depth, 40.0);
}

TEST(Query, ShallowestReliableUnaffectedBySourceIndex)
{
  // ADR-0005 D5 safety carve-out: the navigation-safety query ignores the
  // source-index / registry priority axis entirely. Two cells in the same layer
  // with DIFFERENT source indices must produce exactly the same shallowest-
  // reliable result as if source_index were unset — selection is by depth +
  // uncertainty only, never by which platform/sensor contributed the cell.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);

  // Deeper cell from a "high" source index; shallower from a "low" one. If the
  // query ever consulted source_index, the answer could flip — it must not.
  store.set(SourceLayer::Processed, kEpoch, cell, BathyCell{-30.0, 0.1, 1LL, 9000u});  // deeper
  store.set(SourceLayer::Draft, kEpoch, cell, BathyCell{-25.0, 0.2, 2LL, 1u});         // shallower

  const auto result = shallowestReliable(store, cell, 1.0);
  ASSERT_TRUE(result.has_value());
  EXPECT_DOUBLE_EQ(result->depth, -25.0);          // shallowest, regardless of source
  EXPECT_EQ(result->source, SourceLayer::Draft);

  // Same geometry with source indices swapped -> identical result.
  BathymetryStore swapped(5);
  swapped.set(SourceLayer::Processed, kEpoch, cell, BathyCell{-30.0, 0.1, 1LL, 1u});
  swapped.set(SourceLayer::Draft, kEpoch, cell, BathyCell{-25.0, 0.2, 2LL, 9000u});
  const auto result2 = shallowestReliable(swapped, cell, 1.0);
  ASSERT_TRUE(result2.has_value());
  EXPECT_DOUBLE_EQ(result2->depth, -25.0);
  EXPECT_EQ(result2->source, SourceLayer::Draft);
}

TEST(Query, BestSourcePrefersFinerLevelAcrossLevels)
{
  // Multi-level store (ADR-0002 §D2): a coarse chart prior and a fine survey
  // grid in the SAME layer at DIFFERENT levels. best-available resolves the
  // finest level present that has data at the query position.
  BathymetryStore store(5, /*chart_writable=*/true);
  gggs::Level coarse(4);
  gggs::Level fine(7);
  const auto pt = gggs::geoPoint(43.0, -70.5);

  store.set(SourceLayer::Chart, kEpoch, coarse.cellIndex(pt), BathyCell{38.0, 3.0, 1LL});
  store.set(SourceLayer::Chart, kEpoch, fine.cellIndex(pt), BathyCell{40.0, 0.5, 2LL});

  // Query at the finest present level (its center is closest to the survey
  // point, so the coarse cell also covers it): best-available resolves the
  // finer (level 7) value, not the coarse prior, where both cover the cell.
  const auto best = bestSource(store, fine.cellIndex(pt));
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->source, SourceLayer::Chart);
  EXPECT_DOUBLE_EQ(best->depth, 40.0);   // finer level wins
  EXPECT_EQ(best->level, 7u);
}

TEST(Query, BestSourceFallsToCoarseLevelWhereFineAbsent)
{
  // Where only the coarse level covers a cell, the query falls through to it.
  BathymetryStore store(5, /*chart_writable=*/true);
  gggs::Level coarse(4);
  gggs::Level fine(7);
  const auto surveyed = gggs::geoPoint(43.0, -70.5);
  const auto unsurveyed = gggs::geoPoint(43.0, -70.4);

  store.set(SourceLayer::Chart, kEpoch, coarse.cellIndex(surveyed), BathyCell{38.0, 3.0, 1LL});
  store.set(SourceLayer::Chart, kEpoch, coarse.cellIndex(unsurveyed), BathyCell{37.0, 3.0, 1LL});
  store.set(SourceLayer::Chart, kEpoch, fine.cellIndex(surveyed), BathyCell{40.0, 0.5, 2LL});

  // Cell with no fine-level data: only the coarse level covers it -> resolves
  // to the coarse value.
  const auto best = bestSource(store, store.cellIndex(43.0, -70.4));
  ASSERT_TRUE(best.has_value());
  EXPECT_DOUBLE_EQ(best->depth, 37.0);
  EXPECT_EQ(best->level, 4u);
}

TEST(Query, ShallowestReliableConsidersAllLevels)
{
  // The safety query must examine every level present: a coarse level can be
  // both shallower and reliable where a finer one is too uncertain.
  BathymetryStore store(5);
  gggs::Level coarse(4);
  gggs::Level fine(7);
  const auto pt = gggs::geoPoint(43.0, -70.5);

  // Fine level: shallower but too uncertain. Coarse: deeper but reliable.
  store.set(SourceLayer::Draft, kEpoch, fine.cellIndex(pt), BathyCell{-20.0, 5.0, 2LL});
  store.set(SourceLayer::Draft, kEpoch, coarse.cellIndex(pt), BathyCell{-30.0, 0.2, 1LL});

  const auto result = shallowestReliable(store, store.cellIndex(43.0, -70.5), 1.0);
  ASSERT_TRUE(result.has_value());
  EXPECT_DOUBLE_EQ(result->depth, -30.0);   // fine excluded by uncertainty
  EXPECT_EQ(result->level, 4u);
}

TEST(Query, ShallowestReliableGatesChartByUncertainty)
{
  // The coarse Chart prior carries a fixed import uncertainty (3.0 m). It feeds
  // the safety query shallowestReliable, so the uncertainty gate must admit it
  // only when the caller's tolerance allows — both sides of the threshold.
  BathymetryStore store(5, /*chart_writable=*/true);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Chart, kEpoch, cell, BathyCell{38.0, 3.0, 1LL});

  // Tolerance below the chart uncertainty excludes it -> cell reads as unknown.
  EXPECT_FALSE(shallowestReliable(store, cell, 2.9).has_value());

  // Tolerance at the chart uncertainty (comparison is strictly >) admits it.
  const auto at = shallowestReliable(store, cell, 3.0);
  ASSERT_TRUE(at.has_value());
  EXPECT_EQ(at->source, SourceLayer::Chart);
  EXPECT_DOUBLE_EQ(at->depth, 38.0);
}

TEST(Query, BestSourceResolvesNewestEpochFirst)
{
  // Within a layer, the default query walks epochs newest-first (ADR-0002
  // §A1.3) — a later day's value supersedes an earlier day's without fusion.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, "2026-06-09", cell, BathyCell{-30.0, 0.5, 1LL});
  store.set(SourceLayer::Draft, "2026-06-11", cell, BathyCell{-28.0, 0.5, 2LL});

  const auto best = bestSource(store, cell);
  ASSERT_TRUE(best.has_value());
  EXPECT_DOUBLE_EQ(best->depth, -28.0);          // newest epoch wins
  EXPECT_EQ(best->epoch, "2026-06-11");
}

TEST(Query, EpochsAreNeverFused)
{
  // The point of epochs: N days surveyed keep N records, never averaged. Each
  // epoch reads back its own value exactly; the default query is the newest,
  // not a blend.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, "2026-06-09", cell, BathyCell{-30.0, 0.5, 1LL});
  store.set(SourceLayer::Draft, "2026-06-10", cell, BathyCell{-20.0, 0.5, 2LL});
  store.set(SourceLayer::Draft, "2026-06-11", cell, BathyCell{-25.0, 0.5, 3LL});

  // Three distinct epochs, each holding its own depth (no fusion to a mean).
  EXPECT_EQ(store.epochs(SourceLayer::Draft).size(), 3u);
  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Draft, "2026-06-09", cell)->depth, -30.0);
  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Draft, "2026-06-10", cell)->depth, -20.0);
  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Draft, "2026-06-11", cell)->depth, -25.0);
  // The newest is the default query result, not the mean (-25 vs a fused -25
  // would be ambiguous, so the older two are deliberately not -25's average).
  EXPECT_DOUBLE_EQ(bestSource(store, cell)->depth, -25.0);
}

TEST(Query, ShallowestReliableFallsThroughNoisyNewestEpoch)
{
  // §A1.3 safety walk: a fresh-but-noisy epoch fails the gate and falls through
  // to the prior epoch's confident value — no cross-epoch fusion.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, "2026-06-09", cell, BathyCell{-30.0, 0.2, 1LL});  // confident
  store.set(SourceLayer::Draft, "2026-06-11", cell, BathyCell{-20.0, 5.0, 2LL});  // noisy, newer

  const auto result = shallowestReliable(store, cell, 1.0);
  ASSERT_TRUE(result.has_value());
  EXPECT_DOUBLE_EQ(result->depth, -30.0);        // noisy newest skipped
  EXPECT_EQ(result->epoch, "2026-06-09");
}

TEST(Query, ForEachChangedCellDiffsTwoEpochs)
{
  // The change map: cells observed in BOTH epochs are visited with both
  // records; cells in only one are coverage change, not visited (ADR-0002 A1.1).
  BathymetryStore store(5);
  const auto both = store.cellIndex(43.0, -70.5);
  const auto only_a = store.cellIndex(43.0, -70.4);
  store.set(SourceLayer::Draft, "2026-06-09", both, BathyCell{-30.0, 0.5, 1LL});
  store.set(SourceLayer::Draft, "2026-06-09", only_a, BathyCell{-31.0, 0.5, 1LL});
  store.set(SourceLayer::Draft, "2026-06-11", both, BathyCell{-28.5, 0.5, 2LL});

  std::size_t visited = 0;
  double delta = 0.0;
  const auto n = marine_bathymetry_store::forEachChangedCell(
    store, SourceLayer::Draft, "2026-06-09", "2026-06-11",
    [&](const gggs::CellIndex &, const BathyCell & a, const BathyCell & b) {
      ++visited;
      delta = b.depth - a.depth;
    });

  EXPECT_EQ(n, 1u);          // only the doubly-observed cell
  EXPECT_EQ(visited, 1u);
  EXPECT_DOUBLE_EQ(delta, 1.5);   // -28.5 - (-30.0)
}
