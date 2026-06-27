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
#include <map>
#include <set>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_tiled_raster_store/tile_catalog.hpp"

namespace mtrs = marine_tiled_raster_store;

namespace
{

// Distinct GGGS grids near Lake Massabesic. Level 13 grids span ~0.001 deg, so
// 0.01-deg spacing yields well-separated, distinct tiles away from the polar
// scaling boundaries.
const gggs::Level kLevel(13);

gggs::GridIndex gridAt(int i)
{
  return kLevel.gridIndex(43.00 + 0.01 * i, -71.40);
}

bool contains(const std::vector<gggs::GridIndex> & v, const gggs::GridIndex & g)
{
  return std::find(v.begin(), v.end(), g) != v.end();
}

}  // namespace

// Sanity: the test grid generator yields distinct, valid indices.
TEST(TileCatalogGrids, DistinctAndValid)
{
  std::set<gggs::GridIndex> seen;
  for (int i = 0; i < 5; ++i) {
    const gggs::GridIndex g = gridAt(i);
    EXPECT_TRUE(g.valid());
    EXPECT_TRUE(seen.insert(g).second) << "grid " << i << " collided";
  }
}

// ---- TileCatalogBuilder ----------------------------------------------------

TEST(TileCatalogBuilder, EmitsCompleteSnapshot)
{
  mtrs::TileCatalogBuilder builder;
  builder.update(gridAt(0), 100);
  builder.update(gridAt(1), 200);
  builder.update(gridAt(2), 150);

  const mtrs::TileCatalog catalog = builder.buildCatalog(300);

  EXPECT_EQ(catalog.generation_time, 300);
  ASSERT_EQ(catalog.entries.size(), 3u);

  std::map<gggs::GridIndex, mtrs::TileVersion> by_index;
  for (const auto & e : catalog.entries) {
    by_index[e.index] = e.version;
  }
  EXPECT_EQ(by_index[gridAt(0)], 100);
  EXPECT_EQ(by_index[gridAt(1)], 200);
  EXPECT_EQ(by_index[gridAt(2)], 150);
}

TEST(TileCatalogBuilder, NewestWins)
{
  mtrs::TileCatalogBuilder builder;
  builder.update(gridAt(0), 100);
  builder.update(gridAt(0), 50);   // older -> ignored
  ASSERT_TRUE(builder.versionOf(gridAt(0)).has_value());
  EXPECT_EQ(*builder.versionOf(gridAt(0)), 100);

  builder.update(gridAt(0), 200);  // newer -> applied
  EXPECT_EQ(*builder.versionOf(gridAt(0)), 200);
  EXPECT_EQ(builder.size(), 1u);
}

TEST(TileCatalogBuilder, RemoveDropsFromSnapshot)
{
  mtrs::TileCatalogBuilder builder;
  builder.update(gridAt(0), 100);
  builder.update(gridAt(1), 100);
  builder.remove(gridAt(0));

  EXPECT_FALSE(builder.versionOf(gridAt(0)).has_value());
  const mtrs::TileCatalog catalog = builder.buildCatalog(200);
  ASSERT_EQ(catalog.entries.size(), 1u);
  EXPECT_EQ(catalog.entries.front().index, gridAt(1));
}

// ---- TileCatalogReconciler: request -----------------------------------------

TEST(TileCatalogReconciler, ColdStartRequestsAll)
{
  mtrs::TileCatalogBuilder builder;
  for (int i = 0; i < 3; ++i) {
    builder.update(gridAt(i), 100 + i);
  }
  const mtrs::TileCatalog catalog = builder.buildCatalog(300);

  mtrs::TileCatalogReconciler reconciler;  // empty
  const mtrs::ReconcileResult res = reconciler.reconcile(catalog);

  EXPECT_EQ(res.to_request.size(), 3u);
  EXPECT_TRUE(res.to_prune.empty());
}

TEST(TileCatalogReconciler, UpToDateIsNoOp)
{
  mtrs::TileCatalogBuilder builder;
  for (int i = 0; i < 3; ++i) {
    builder.update(gridAt(i), 100 + i);
  }
  const mtrs::TileCatalog catalog = builder.buildCatalog(300);

  mtrs::TileCatalogReconciler reconciler;
  for (int i = 0; i < 3; ++i) {
    reconciler.markHave(gridAt(i), 100 + i);
  }
  const mtrs::ReconcileResult res = reconciler.reconcile(catalog);

  EXPECT_TRUE(res.to_request.empty());
  EXPECT_TRUE(res.to_prune.empty());
}

TEST(TileCatalogReconciler, StaleTileRequested)
{
  mtrs::TileCatalogBuilder builder;
  builder.update(gridAt(0), 200);
  const mtrs::TileCatalog catalog = builder.buildCatalog(300);

  mtrs::TileCatalogReconciler reconciler;
  reconciler.markHave(gridAt(0), 100);  // stale: held 100 < catalog 200
  const mtrs::ReconcileResult res = reconciler.reconcile(catalog);

  ASSERT_EQ(res.to_request.size(), 1u);
  EXPECT_EQ(res.to_request.front(), gridAt(0));
  EXPECT_TRUE(res.to_prune.empty());
}

TEST(TileCatalogReconciler, MissingTileRequested)
{
  mtrs::TileCatalogBuilder builder;
  builder.update(gridAt(0), 100);
  builder.update(gridAt(1), 100);
  const mtrs::TileCatalog catalog = builder.buildCatalog(300);

  mtrs::TileCatalogReconciler reconciler;
  reconciler.markHave(gridAt(0), 100);  // has g0; missing g1
  const mtrs::ReconcileResult res = reconciler.reconcile(catalog);

  ASSERT_EQ(res.to_request.size(), 1u);
  EXPECT_EQ(res.to_request.front(), gridAt(1));
}

// ---- TileCatalogReconciler: prune ------------------------------------------

TEST(TileCatalogReconciler, PruneOnAbsence)
{
  mtrs::TileCatalogBuilder builder;
  builder.update(gridAt(0), 100);
  const mtrs::TileCatalog catalog = builder.buildCatalog(300);

  mtrs::TileCatalogReconciler reconciler;
  reconciler.markHave(gridAt(0), 100);
  reconciler.markHave(gridAt(1), 100);  // absent from catalog, old (100 < 300)
  const mtrs::ReconcileResult res = reconciler.reconcile(catalog);

  EXPECT_TRUE(res.to_request.empty());
  ASSERT_EQ(res.to_prune.size(), 1u);
  EXPECT_EQ(res.to_prune.front(), gridAt(1));
}

// The load-bearing invariant: a late/reordered catalog must not delete a
// just-pushed fresh tile (held version >= catalog generation_time).
TEST(TileCatalogReconciler, PruneIsTimestampGated)
{
  mtrs::TileCatalogBuilder builder;  // empty source view at this (stale) catalog
  const mtrs::TileCatalog stale_catalog = builder.buildCatalog(300);

  mtrs::TileCatalogReconciler reconciler;
  reconciler.markHave(gridAt(0), 500);  // fresh: pushed after the catalog's time
  reconciler.markHave(gridAt(1), 100);  // old: predates the catalog

  const mtrs::ReconcileResult res = reconciler.reconcile(stale_catalog);

  // g0 (500 >= gen 300) survives; only g1 (100 < 300) is pruned.
  EXPECT_TRUE(res.to_request.empty());
  ASSERT_EQ(res.to_prune.size(), 1u);
  EXPECT_EQ(res.to_prune.front(), gridAt(1));
  EXPECT_FALSE(contains(res.to_prune, gridAt(0)));
}

TEST(TileCatalogReconciler, ReorderedStalePushIgnored)
{
  mtrs::TileCatalogReconciler reconciler;
  reconciler.markHave(gridAt(0), 200);
  reconciler.markHave(gridAt(0), 100);  // reordered older arrival -> ignored

  ASSERT_TRUE(reconciler.versionOf(gridAt(0)).has_value());
  EXPECT_EQ(*reconciler.versionOf(gridAt(0)), 200);
}

// ---- Convergence -----------------------------------------------------------

// A fresh boat advertises a small catalog with a new generation time; the
// operator prunes its stale tiles and converges to exactly the new set.
TEST(TileCatalogReconciler, BoatResetConverges)
{
  // Operator holds five old tiles.
  mtrs::TileCatalogReconciler reconciler;
  for (int i = 0; i < 5; ++i) {
    reconciler.markHave(gridAt(i), 100 + i);
  }

  // Boat reset: a small fresh catalog (just g0) at a much newer time.
  mtrs::TileCatalogBuilder fresh;
  fresh.update(gridAt(0), 1000);
  const mtrs::TileCatalog catalog = fresh.buildCatalog(2000);

  mtrs::ReconcileResult res = reconciler.reconcile(catalog);
  // g1..g4 pruned (old, absent); g0 requested (held 100 < 1000).
  EXPECT_EQ(res.to_prune.size(), 4u);
  ASSERT_EQ(res.to_request.size(), 1u);
  EXPECT_EQ(res.to_request.front(), gridAt(0));

  // Apply the actions, then re-reconcile: converged, no further work.
  for (const auto & g : res.to_prune) {
    reconciler.drop(g);
  }
  reconciler.markHave(gridAt(0), 1000);

  res = reconciler.reconcile(catalog);
  EXPECT_TRUE(res.to_request.empty());
  EXPECT_TRUE(res.to_prune.empty());
  EXPECT_EQ(reconciler.size(), 1u);
}

// Anti-entropy under loss/reorder: even if only one requested tile arrives per
// round and arrivals are reordered, repeated reconciles converge to the catalog.
TEST(TileCatalogReconciler, ConvergesUnderLossAndReorder)
{
  mtrs::TileCatalogBuilder builder;
  const int n = 6;
  for (int i = 0; i < n; ++i) {
    builder.update(gridAt(i), 500 + i);
  }
  const mtrs::TileCatalog catalog = builder.buildCatalog(1000);

  mtrs::TileCatalogReconciler reconciler;  // starts empty

  int rounds = 0;
  const int max_rounds = n + 5;  // generous bound; should converge in ~n
  while (rounds++ < max_rounds) {
    const mtrs::ReconcileResult res = reconciler.reconcile(catalog);
    if (res.to_request.empty() && res.to_prune.empty()) {
      break;
    }
    EXPECT_TRUE(res.to_prune.empty());  // nothing to prune: catalog is the truth
    ASSERT_FALSE(res.to_request.empty());

    // Lossy + reordered link: deliver only the LAST requested tile this round.
    const gggs::GridIndex & arrived = res.to_request.back();
    auto v = builder.versionOf(arrived);
    ASSERT_TRUE(v.has_value());
    reconciler.markHave(arrived, *v);
  }

  EXPECT_LE(rounds, max_rounds);
  EXPECT_EQ(reconciler.size(), static_cast<std::size_t>(n));
  const mtrs::ReconcileResult final_res = reconciler.reconcile(catalog);
  EXPECT_TRUE(final_res.to_request.empty());
  EXPECT_TRUE(final_res.to_prune.empty());
}
