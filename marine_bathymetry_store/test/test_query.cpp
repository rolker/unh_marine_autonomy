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
#include <limits>
#include <optional>
#include <set>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/query.hpp"

using marine_bathymetry_store::BathyCell;
using marine_bathymetry_store::BathymetryStore;
using marine_bathymetry_store::bestSource;
using marine_bathymetry_store::DepthSample;
using marine_bathymetry_store::forEachCellBestSource;
using marine_bathymetry_store::reliableSamples;
using marine_bathymetry_store::shallowestReliable;
using marine_bathymetry_store::SourceLayer;

TEST(Query, BestSourcePrefersHigherPriorityLayer)
{
  BathymetryStore store(5, /*reference_writable=*/true);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Reference, cell, BathyCell{-12.0, 2.0});
  store.set(SourceLayer::Draft, cell, BathyCell{-10.0, 0.1});

  const auto best = bestSource(store, cell);
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->source, SourceLayer::Draft);
  EXPECT_DOUBLE_EQ(best->depth, -10.0);
}

TEST(Query, BestSourceFallsBackToLowerPriority)
{
  BathymetryStore store(5, /*reference_writable=*/true);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Reference, cell, BathyCell{-12.0, 2.0});

  const auto best = bestSource(store, cell);
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->source, SourceLayer::Reference);
}

TEST(Query, UnknownCellIsNullopt)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  EXPECT_FALSE(bestSource(store, cell).has_value());

  // A written-but-no-data cell is still unknown (not "deep water").
  store.set(SourceLayer::Draft, cell, BathyCell{});
  EXPECT_FALSE(bestSource(store, cell).has_value());
}

TEST(Query, ShallowestReliablePicksGreatestHeight)
{
  // depth is ellipsoidal height (up-positive): shallower == greater value.
  BathymetryStore store(5, /*reference_writable=*/true);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Reference, cell, BathyCell{-30.0, 0.1});  // deeper
  store.set(SourceLayer::Draft, cell, BathyCell{-25.0, 0.2});         // shallower

  const auto result = shallowestReliable(store, cell, 1.0);
  ASSERT_TRUE(result.has_value());
  EXPECT_DOUBLE_EQ(result->depth, -25.0);
  EXPECT_EQ(result->source, SourceLayer::Draft);
}

TEST(Query, ShallowestReliableExcludesOverUncertain)
{
  BathymetryStore store(5, /*reference_writable=*/true);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Reference, cell, BathyCell{-30.0, 0.1});  // reliable, deeper
  // shallower, but too uncertain to be reliable:
  store.set(SourceLayer::Draft, cell, BathyCell{-25.0, 5.0});

  const auto result = shallowestReliable(store, cell, 1.0);
  ASSERT_TRUE(result.has_value());
  EXPECT_DOUBLE_EQ(result->depth, -30.0);   // survey excluded
  EXPECT_EQ(result->source, SourceLayer::Reference);
}

TEST(Query, ShallowestReliableTreatsNaNUncertaintyAsUnreliable)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, cell, BathyCell{-25.0, std::nan("")});
  EXPECT_FALSE(shallowestReliable(store, cell, 1.0).has_value());
}

TEST(Query, ForEachRegionVisitsCoveredCells)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, cell, BathyCell{-20.0, 0.5});

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

TEST(Query, BestSourceFallsThroughToReferencePrior)
{
  // Reference is the lowest-priority prior: used only where nothing newer
  // exists, and overridden by Survey where it does (ADR-0002 §D3).
  BathymetryStore store(5, /*reference_writable=*/true);
  const auto unsurveyed = store.cellIndex(43.0, -70.5);
  const auto surveyed = store.cellIndex(43.0, -70.4);

  store.set(SourceLayer::Reference, unsurveyed, BathyCell{38.0, 3.0});
  store.set(SourceLayer::Reference, surveyed, BathyCell{38.0, 3.0});
  store.set(SourceLayer::Draft, surveyed, BathyCell{40.0, 0.5});

  // Unsurveyed cell falls through to the prior.
  const auto a = bestSource(store, unsurveyed);
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->source, SourceLayer::Reference);
  EXPECT_DOUBLE_EQ(a->depth, 38.0);

  // Surveyed cell prefers the live survey over the prior.
  const auto b = bestSource(store, surveyed);
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->source, SourceLayer::Draft);
  EXPECT_DOUBLE_EQ(b->depth, 40.0);
}

TEST(Query, BestSourcePrefersFinerLevelAcrossLevels)
{
  // Multi-level store (ADR-0002 §D2): a coarse prior and a fine survey grid in
  // the SAME layer at DIFFERENT levels. best-available resolves the finest level
  // present that has data at the query position.
  BathymetryStore store(5, /*reference_writable=*/true);
  gggs::Level coarse(4);
  gggs::Level fine(7);
  const auto pt = gggs::geoPoint(43.0, -70.5);

  store.set(SourceLayer::Reference, coarse.cellIndex(pt), BathyCell{38.0, 3.0});
  store.set(SourceLayer::Reference, fine.cellIndex(pt), BathyCell{40.0, 0.5});

  // Query at the finest present level (its center is closest to the survey
  // point, so the coarse cell also covers it): best-available resolves the
  // finer (level 7) value, not the coarse prior, where both cover the cell.
  const auto best = bestSource(store, fine.cellIndex(pt));
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->source, SourceLayer::Reference);
  EXPECT_DOUBLE_EQ(best->depth, 40.0);   // finer level wins
  EXPECT_EQ(best->level, 7u);
}

TEST(Query, BestSourceFallsToCoarseLevelWhereFineAbsent)
{
  // Where only the coarse level covers a cell, the query falls through to it.
  BathymetryStore store(5, /*reference_writable=*/true);
  gggs::Level coarse(4);
  gggs::Level fine(7);
  const auto surveyed = gggs::geoPoint(43.0, -70.5);
  const auto unsurveyed = gggs::geoPoint(43.0, -70.4);

  store.set(SourceLayer::Reference, coarse.cellIndex(surveyed), BathyCell{38.0, 3.0});
  store.set(SourceLayer::Reference, coarse.cellIndex(unsurveyed), BathyCell{37.0, 3.0});
  store.set(SourceLayer::Reference, fine.cellIndex(surveyed), BathyCell{40.0, 0.5});

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
  store.set(SourceLayer::Draft, fine.cellIndex(pt), BathyCell{-20.0, 5.0});
  store.set(SourceLayer::Draft, coarse.cellIndex(pt), BathyCell{-30.0, 0.2});

  const auto result = shallowestReliable(store, store.cellIndex(43.0, -70.5), 1.0);
  ASSERT_TRUE(result.has_value());
  EXPECT_DOUBLE_EQ(result->depth, -30.0);   // fine excluded by uncertainty
  EXPECT_EQ(result->level, 4u);
}

TEST(Query, ShallowestReliableGatesReferenceByUncertainty)
{
  // The coarse prior carries a fixed import uncertainty (3.0 m). It feeds the
  // safety query shallowestReliable, so the uncertainty gate must admit it only
  // when the caller's tolerance allows — both sides of the threshold.
  BathymetryStore store(5, /*reference_writable=*/true);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Reference, cell, BathyCell{38.0, 3.0});

  // Tolerance below the prior uncertainty excludes it -> cell reads as unknown.
  EXPECT_FALSE(shallowestReliable(store, cell, 2.9).has_value());

  // Tolerance at the prior uncertainty (comparison is strictly >) admits it.
  const auto at = shallowestReliable(store, cell, 3.0);
  ASSERT_TRUE(at.has_value());
  EXPECT_EQ(at->source, SourceLayer::Reference);
  EXPECT_DOUBLE_EQ(at->depth, 38.0);
}

TEST(Query, ShallowestReliableWithNoReliableDataReturnsNullopt)
{
  // #221 deliberate tradeoff: with one fused surface there is no prior epoch to
  // fall through to. If the only data over a cell is over-uncertain, the safety
  // query returns nullopt — the caller treats that as unknown → obstacle
  // (ADR-0002 §D7).
  BathymetryStore store(5, /*reference_writable=*/true);
  const auto cell = store.cellIndex(43.0, -70.5);
  // Over-uncertain across both layers that hold this cell.
  store.set(SourceLayer::Reference, cell, BathyCell{-30.0, 5.0});
  store.set(SourceLayer::Draft, cell, BathyCell{-25.0, 9.0});

  EXPECT_FALSE(shallowestReliable(store, cell, 1.0).has_value());

  // bestSource (quality-blind) still resolves it — only the reliability gate
  // rejects it. This confirms the nullopt is the gate, not absent data.
  EXPECT_TRUE(bestSource(store, cell).has_value());
}

TEST(Query, ReliableSamplesCollectsAllLayersDropsNaNRetainsInfiniteAtInfinity)
{
  // Direct coverage for the public safety API introduced in #276. Unlike
  // shallowestReliable (which collapses to one shallowest pick), reliableSamples
  // returns EVERY passing sample so the caller can cost each and take the most
  // hazardous (ADR-0010 §D7). Three properties are pinned:
  //   1. multi-layer collection — one sample per covering layer, all returned;
  //   2. NaN-σ drop — an unreliable sample is excluded;
  //   3. σ=∞ retained at ∞ — passing max_uncertainty=∞ keeps a literal σ=∞
  //      sample (∞ > ∞ is false), so the layer's own isfinite guard, not the
  //      store gate, is what buckets σ=∞ as unknown quality.

  // 1. Multi-layer collection: Survey/Reference/Chart all cover the cell within
  //    tolerance → every one is returned (not just the shallowest).
  {
    BathymetryStore store(5, /*reference_writable=*/true, /*chart_staging_writable=*/true);
    const auto cell = store.cellIndex(43.0, -70.5);
    store.set(SourceLayer::Chart, cell, BathyCell{-20.0, 1.5});
    store.set(SourceLayer::Reference, cell, BathyCell{-12.0, 2.0});
    store.set(SourceLayer::Draft, cell, BathyCell{-10.0, 0.1});

    const auto samples = reliableSamples(store, cell, 3.0);
    ASSERT_EQ(samples.size(), 3u);
    std::set<SourceLayer> sources;
    for (const auto & s : samples) {
      sources.insert(s.source);
    }
    EXPECT_EQ(sources.count(SourceLayer::Draft), 1u);
    EXPECT_EQ(sources.count(SourceLayer::Reference), 1u);
    EXPECT_EQ(sources.count(SourceLayer::Chart), 1u);
  }

  // 2. NaN-σ drop: the NaN sample is never reliable and must be excluded; the
  //    co-located reliable sample survives.
  {
    BathymetryStore store(5, /*reference_writable=*/true);
    const auto cell = store.cellIndex(43.0, -70.5);
    store.set(SourceLayer::Draft, cell, BathyCell{-10.0, std::nan("")});
    store.set(SourceLayer::Reference, cell, BathyCell{-12.0, 2.0});

    const auto samples = reliableSamples(store, cell, 3.0);
    ASSERT_EQ(samples.size(), 1u);
    EXPECT_EQ(samples.front().source, SourceLayer::Reference);
  }

  // 3. σ=∞ retained at ∞, dropped under a finite gate. Called with ∞ the gate
  //    admits a literal σ=∞ sample (∞ > ∞ is false); a finite tolerance rejects
  //    it. This is the contract evaluateCell relies on to short-circuit σ=∞ to
  //    conservative LETHAL in its own isfinite branch, not via the store gate.
  {
    BathymetryStore store(5);
    const auto cell = store.cellIndex(43.0, -70.5);
    store.set(
      SourceLayer::Draft, cell,
      BathyCell{-10.0, std::numeric_limits<double>::infinity()});

    const auto at_inf = reliableSamples(store, cell, std::numeric_limits<double>::infinity());
    ASSERT_EQ(at_inf.size(), 1u);
    EXPECT_TRUE(std::isinf(at_inf.front().uncertainty));

    EXPECT_TRUE(reliableSamples(store, cell, 3.0).empty());   // finite gate drops σ=∞
  }
}

TEST(Query, BestSourcePrefersProcessedOverDraft)
{
  // ADR-0010 D8: Processed (authoritative offline re-run) outranks Draft (live
  // CUBE) at the same cell. The priority walk returns the Processed sample even
  // though the Draft sample is present and shallower — proving the split's core
  // guarantee that a live pass never degrades a re-run cell in the query overlay.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, cell, BathyCell{-9.0, 0.1});
  store.set(SourceLayer::Processed, cell, BathyCell{-11.0, 0.3});

  const auto best = bestSource(store, cell);
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->source, SourceLayer::Processed);
  EXPECT_DOUBLE_EQ(best->depth, -11.0);

  // Where Processed has no data, the walk falls through to Draft.
  const auto draft_only = store.cellIndex(43.0, -70.4);
  store.set(SourceLayer::Draft, draft_only, BathyCell{-7.0, 0.2});
  const auto b = bestSource(store, draft_only);
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->source, SourceLayer::Draft);
}

TEST(Query, BestSourceFullPriorityOrderProcessedDraftReferenceChart)
{
  // ADR-0010 D4/D8 ordering: processed > draft > reference > chart. With all four
  // present the highest-priority layer wins; removing layers walks down.
  BathymetryStore store(5, /*reference_writable=*/true, /*chart_staging_writable=*/true);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Chart, cell, BathyCell{-20.0, 1.5});
  store.set(SourceLayer::Reference, cell, BathyCell{-12.0, 2.0});
  store.set(SourceLayer::Draft, cell, BathyCell{-10.0, 0.1});
  store.set(SourceLayer::Processed, cell, BathyCell{-11.0, 0.2});

  auto best = bestSource(store, cell);
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->source, SourceLayer::Processed);

  // Drop Processed: Draft wins.
  const auto cell_d = store.cellIndex(43.5, -70.6);
  store.set(SourceLayer::Chart, cell_d, BathyCell{-20.0, 1.5});
  store.set(SourceLayer::Reference, cell_d, BathyCell{-12.0, 2.0});
  store.set(SourceLayer::Draft, cell_d, BathyCell{-10.0, 0.1});
  best = bestSource(store, cell_d);
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->source, SourceLayer::Draft);

  const auto cell2 = store.cellIndex(44.0, -71.0);
  store.set(SourceLayer::Chart, cell2, BathyCell{-21.0, 1.5});
  store.set(SourceLayer::Reference, cell2, BathyCell{-13.0, 2.0});
  best = bestSource(store, cell2);
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->source, SourceLayer::Reference);

  const auto cell3 = store.cellIndex(45.0, -69.0);
  store.set(SourceLayer::Chart, cell3, BathyCell{-22.0, 1.5});
  best = bestSource(store, cell3);
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->source, SourceLayer::Chart);
  EXPECT_DOUBLE_EQ(best->depth, -22.0);
}
