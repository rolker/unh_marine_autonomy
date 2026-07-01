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

TEST(Query, BestSourcePrefersHigherPriorityLayer)
{
  BathymetryStore store(5, /*reference_writable=*/true);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Reference, cell, BathyCell{-12.0, 2.0});
  store.set(SourceLayer::Survey, cell, BathyCell{-10.0, 0.1});

  const auto best = bestSource(store, cell);
  ASSERT_TRUE(best.has_value());
  EXPECT_EQ(best->source, SourceLayer::Survey);
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
  store.set(SourceLayer::Survey, cell, BathyCell{});
  EXPECT_FALSE(bestSource(store, cell).has_value());
}

TEST(Query, ShallowestReliablePicksGreatestHeight)
{
  // depth is ellipsoidal height (up-positive): shallower == greater value.
  BathymetryStore store(5, /*reference_writable=*/true);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Reference, cell, BathyCell{-30.0, 0.1});  // deeper
  store.set(SourceLayer::Survey, cell, BathyCell{-25.0, 0.2});         // shallower

  const auto result = shallowestReliable(store, cell, 1.0);
  ASSERT_TRUE(result.has_value());
  EXPECT_DOUBLE_EQ(result->depth, -25.0);
  EXPECT_EQ(result->source, SourceLayer::Survey);
}

TEST(Query, ShallowestReliableExcludesOverUncertain)
{
  BathymetryStore store(5, /*reference_writable=*/true);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Reference, cell, BathyCell{-30.0, 0.1});  // reliable, deeper
  // shallower, but too uncertain to be reliable:
  store.set(SourceLayer::Survey, cell, BathyCell{-25.0, 5.0});

  const auto result = shallowestReliable(store, cell, 1.0);
  ASSERT_TRUE(result.has_value());
  EXPECT_DOUBLE_EQ(result->depth, -30.0);   // survey excluded
  EXPECT_EQ(result->source, SourceLayer::Reference);
}

TEST(Query, ShallowestReliableTreatsNaNUncertaintyAsUnreliable)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Survey, cell, BathyCell{-25.0, std::nan("")});
  EXPECT_FALSE(shallowestReliable(store, cell, 1.0).has_value());
}

TEST(Query, ForEachRegionVisitsCoveredCells)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Survey, cell, BathyCell{-20.0, 0.5});

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
  store.set(SourceLayer::Survey, surveyed, BathyCell{40.0, 0.5});

  // Unsurveyed cell falls through to the prior.
  const auto a = bestSource(store, unsurveyed);
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->source, SourceLayer::Reference);
  EXPECT_DOUBLE_EQ(a->depth, 38.0);

  // Surveyed cell prefers the live survey over the prior.
  const auto b = bestSource(store, surveyed);
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->source, SourceLayer::Survey);
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
  store.set(SourceLayer::Survey, fine.cellIndex(pt), BathyCell{-20.0, 5.0});
  store.set(SourceLayer::Survey, coarse.cellIndex(pt), BathyCell{-30.0, 0.2});

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
  store.set(SourceLayer::Survey, cell, BathyCell{-25.0, 9.0});

  EXPECT_FALSE(shallowestReliable(store, cell, 1.0).has_value());

  // bestSource (quality-blind) still resolves it — only the reliability gate
  // rejects it. This confirms the nullopt is the gate, not absent data.
  EXPECT_TRUE(bestSource(store, cell).has_value());
}
