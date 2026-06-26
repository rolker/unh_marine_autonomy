// Copyright (c) 2026 Roland Arsenault
// Licensed under BSD license

#include <gtest/gtest.h>

#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>

#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathy_cell.hpp"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/tile_io.hpp"
#include "nav2_costmap_2d/cost_values.hpp"
#include "nav2_costmap_2d/layered_costmap.hpp"
#include "nav2_util/lifecycle_node.hpp"
#include "tf2_ros/buffer.h"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "rclcpp/rclcpp.hpp"

#include "bathymetry_layer.hpp"

using bathymetry_layer::BathymetryLayer;
using marine_bathymetry_store::BathyCell;
using marine_bathymetry_store::BathymetryStore;
using marine_bathymetry_store::SourceLayer;

namespace
{
// Survey position used across the per-cell tests.
constexpr double kLat = 43.0;
constexpr double kLon = -70.5;

// Nanoseconds in one second (timestamp arithmetic is ns since the Unix epoch).
constexpr int64_t kNsPerSec = 1000000000LL;
}  // namespace

// Test subclass exposing the protected store / water-surface seam and the
// two-query decision helpers, so the cost logic can be exercised directly
// against a synthetic store with no costmap or TF pipeline.
class BathymetryLayerForTest : public BathymetryLayer
{
public:
  void setStore(std::unique_ptr<BathymetryStore> store) {store_ = std::move(store);}
  BathymetryStore & store() {return *store_;}
  void setMapTideZ(double z) {map_tide_z_ = z;}
  double getMapTideZ() const {return map_tide_z_;}
  // Explicitly mark the tide as valid (as if a successful TF lookup arrived).
  void setMapTideValid(bool v) {map_tide_valid_ = v;}
  bool isMapTideValid() const {return map_tide_valid_;}
  void setMinimumDepth(double v) {minimum_depth_ = v;}
  void setMaximumCautionDepth(double v) {maximum_caution_depth_ = v;}
  void setMaxUncertainty(double v) {max_uncertainty_ = v;}
  void setMaxAge(double v) {max_age_ = v;}
  void setUnsurveyedIsLethal(bool v) {unsurveyed_is_lethal_ = v;}
  // enabled_ is set from the "enabled" param in onInitialize(); the blit tests
  // skip initialize(), so set it directly.
  void setEnabled(bool v) {enabled_ = v;}

  using BathymetryLayer::computeCost;
  using BathymetryLayer::evaluateCell;
  using BathymetryLayer::isStale;
  using BathymetryLayer::expandUserPath;
  using BathymetryLayer::injectTile;
  using BathymetryLayer::tileSize;
};

// ---------------------------------------------------------------------------
// Test case 0 (#218): store_path "~"/"~/" expansion to $HOME — one portable
// value resolves on both the boat and dev/sim; other paths are untouched.
// ---------------------------------------------------------------------------
TEST(BathymetryLayer, ExpandUserPathHandlesTilde)
{
  ::setenv("HOME", "/home/tester", 1);

  EXPECT_EQ(
    BathymetryLayerForTest::expandUserPath("~/data/stores/bathymetry"),
    "/home/tester/data/stores/bathymetry");
  EXPECT_EQ(BathymetryLayerForTest::expandUserPath("~"), "/home/tester");
  // Absolute, relative, and empty paths are returned unchanged.
  EXPECT_EQ(
    BathymetryLayerForTest::expandUserPath("/home/field/data/stores/bathymetry"),
    "/home/field/data/stores/bathymetry");
  EXPECT_EQ(BathymetryLayerForTest::expandUserPath("relative/path"), "relative/path");
  EXPECT_EQ(BathymetryLayerForTest::expandUserPath(""), "");
  // The unsupported "~user" form is left untouched (not expanded).
  EXPECT_EQ(BathymetryLayerForTest::expandUserPath("~field/x"), "~field/x");
}

// ---------------------------------------------------------------------------
// Test case 1: clearance → cost ramp boundaries (lethal / caution / free).
// ---------------------------------------------------------------------------
TEST(BathymetryLayer, ClearanceRampBoundaries)
{
  BathymetryLayerForTest layer;
  layer.setMinimumDepth(1.0);
  layer.setMaximumCautionDepth(3.0);

  // Below minimum_depth → LETHAL.
  EXPECT_EQ(layer.computeCost(0.5), nav2_costmap_2d::LETHAL_OBSTACLE);
  EXPECT_EQ(layer.computeCost(1.0 - 1e-6), nav2_costmap_2d::LETHAL_OBSTACLE);

  // At / above maximum_caution_depth → FREE_SPACE.
  EXPECT_EQ(layer.computeCost(3.0), nav2_costmap_2d::FREE_SPACE);
  EXPECT_EQ(layer.computeCost(10.0), nav2_costmap_2d::FREE_SPACE);

  // Midpoint of the [1, 3] ramp → half of MAX_NON_OBSTACLE.
  const unsigned char mid = layer.computeCost(2.0);
  const unsigned char expected_mid =
    static_cast<unsigned char>(nav2_costmap_2d::MAX_NON_OBSTACLE * 0.5);
  EXPECT_EQ(mid, expected_mid);

  // Monotonic: shallower clearance must never cost less than deeper clearance.
  EXPECT_GE(layer.computeCost(1.2), layer.computeCost(2.5));

  // Non-finite clearance is conservatively LETHAL.
  EXPECT_EQ(
    layer.computeCost(std::numeric_limits<double>::quiet_NaN()),
    nav2_costmap_2d::LETHAL_OBSTACLE);
}

// ---------------------------------------------------------------------------
// Test case 2: truly-unsurveyed cell → NO_INFORMATION (layer leaves it alone).
// ---------------------------------------------------------------------------
TEST(BathymetryLayer, UnsurveyedCellLeavesMasterUntouched)
{
  BathymetryLayerForTest layer;
  layer.setStore(std::make_unique<BathymetryStore>(5));
  layer.setMapTideZ(0.0);
  layer.setMapTideValid(true);

  // A cell with no data anywhere in the store.
  const auto cell = layer.store().cellIndex(kLat, kLon);
  const auto result = layer.evaluateCell(cell, /*now_ns=*/0);
  EXPECT_FALSE(result.has_value()) << "Unsurveyed cells must not be written.";
}

// ---------------------------------------------------------------------------
// Test case 2b (#216): with unsurveyed_is_lethal enabled, a no-data cell is
// LETHAL instead of skipped — the closed-basin "no data = land" mode.
// ---------------------------------------------------------------------------
TEST(BathymetryLayer, UnsurveyedCellLethalWhenParamEnabled)
{
  BathymetryLayerForTest layer;
  layer.setStore(std::make_unique<BathymetryStore>(5));
  layer.setMapTideZ(48.88);
  layer.setMapTideValid(true);
  layer.setUnsurveyedIsLethal(true);

  const auto cell = layer.store().cellIndex(kLat, kLon);
  const auto result = layer.evaluateCell(cell, /*now_ns=*/0);
  ASSERT_TRUE(result.has_value())
    << "With unsurveyed_is_lethal, a no-data cell must be written.";
  EXPECT_EQ(*result, nav2_costmap_2d::LETHAL_OBSTACLE);
}

// ---------------------------------------------------------------------------
// Test case 2c (#216): unsurveyed_is_lethal stays behind the tide gate (option
// A). Even with the param set, no-data cells are NOT written before a valid
// tide — the layer never emits a half-initialised "lethal land + unknown water"
// costmap at startup.
// ---------------------------------------------------------------------------
TEST(BathymetryLayer, UnsurveyedLethalStillGatedByTide)
{
  BathymetryLayerForTest layer;
  layer.setStore(std::make_unique<BathymetryStore>(5));
  layer.setUnsurveyedIsLethal(true);
  // Deliberately do NOT mark the tide valid (map_tide_valid_ defaults false).

  const auto cell = layer.store().cellIndex(kLat, kLon);
  const auto result = layer.evaluateCell(cell, /*now_ns=*/0);
  EXPECT_FALSE(result.has_value())
    << "No tide yet: unsurveyed-lethal must still be gated (MF1).";
}

// ---------------------------------------------------------------------------
// Test case 3: surveyed shallow cell → LETHAL; surveyed deep cell → FREE.
// ---------------------------------------------------------------------------
TEST(BathymetryLayer, SurveyedCellMapsClearanceToCost)
{
  BathymetryLayerForTest layer;
  layer.setMinimumDepth(1.0);
  layer.setMaximumCautionDepth(3.0);
  layer.setMaxUncertainty(0.5);
  layer.setMapTideZ(0.0);  // water surface at ellipsoidal height 0.

  auto store = std::make_unique<BathymetryStore>(5);
  const auto cell = store->cellIndex(kLat, kLon);

  // Shoal: seafloor 0.5 m below the water surface → clearance 0.5 m < 1.0 → LETHAL.
  store->set(SourceLayer::Processed, cell, BathyCell{-0.5, 0.1, 1LL});
  layer.setStore(std::move(store));
  layer.setMapTideValid(true);
  auto shoal = layer.evaluateCell(cell, /*now_ns=*/0);
  ASSERT_TRUE(shoal.has_value());
  EXPECT_EQ(*shoal, nav2_costmap_2d::LETHAL_OBSTACLE);

  // Deep: seafloor 5 m down → clearance 5 m >= 3.0 → FREE_SPACE.
  auto deep_store = std::make_unique<BathymetryStore>(5);
  const auto deep_cell = deep_store->cellIndex(kLat, kLon);
  deep_store->set(SourceLayer::Processed, deep_cell, BathyCell{-5.0, 0.1, 1LL});
  layer.setStore(std::move(deep_store));
  layer.setMapTideValid(true);
  auto deep = layer.evaluateCell(deep_cell, /*now_ns=*/0);
  ASSERT_TRUE(deep.has_value());
  EXPECT_EQ(*deep, nav2_costmap_2d::FREE_SPACE);
}

// ---------------------------------------------------------------------------
// Test case 4 (review M1): a surveyed-but-over-uncertain cell → LETHAL, NOT
// treated as unsurveyed. This is the safety-critical two-query case.
// ---------------------------------------------------------------------------
TEST(BathymetryLayer, OverUncertainSurveyedCellIsLethal)
{
  BathymetryLayerForTest layer;
  layer.setMaxUncertainty(0.5);
  layer.setMapTideZ(0.0);

  auto store = std::make_unique<BathymetryStore>(5);
  const auto cell = store->cellIndex(kLat, kLon);
  // Data present, but uncertainty 5.0 m exceeds max_uncertainty 0.5 m: the cell
  // HAS data (bestSource non-null) but fails the reliability gate
  // (shallowestReliable nullopt).
  store->set(SourceLayer::Processed, cell, BathyCell{-10.0, 5.0, 1LL});
  layer.setStore(std::move(store));
  layer.setMapTideValid(true);

  const auto result = layer.evaluateCell(cell, /*now_ns=*/0);
  ASSERT_TRUE(result.has_value())
    << "Over-uncertain surveyed cell must be written (not skipped as unsurveyed).";
  EXPECT_EQ(*result, nav2_costmap_2d::LETHAL_OBSTACLE);
}

// ---------------------------------------------------------------------------
// Test case 5 (extra coverage of test case 4's sibling): a reliable but STALE
// cell → LETHAL regardless of clearance.
// ---------------------------------------------------------------------------
TEST(BathymetryLayer, StaleCellIsLethal)
{
  BathymetryLayerForTest layer;
  layer.setMinimumDepth(1.0);
  layer.setMaximumCautionDepth(3.0);
  layer.setMaxUncertainty(0.5);
  layer.setMaxAge(1.0);          // 1-second staleness window.
  layer.setMapTideZ(0.0);

  auto store = std::make_unique<BathymetryStore>(5);
  const auto cell = store->cellIndex(kLat, kLon);
  // Deep + reliable (clearance would be FREE), but timestamp is 1 ns — ancient.
  store->set(SourceLayer::Processed, cell, BathyCell{-5.0, 0.1, 1LL});
  layer.setStore(std::move(store));
  layer.setMapTideValid(true);

  const int64_t now_ns = 10 * kNsPerSec;  // 10 s now, max_age 1 s → stale.
  const auto stale = layer.evaluateCell(cell, now_ns);
  ASSERT_TRUE(stale.has_value());
  EXPECT_EQ(*stale, nav2_costmap_2d::LETHAL_OBSTACLE);

  // With the staleness gate disabled (max_age 0), the same deep reliable cell
  // is FREE_SPACE — confirming the stale verdict came from the age gate.
  layer.setMaxAge(0.0);
  const auto fresh = layer.evaluateCell(cell, now_ns);
  ASSERT_TRUE(fresh.has_value());
  EXPECT_EQ(*fresh, nav2_costmap_2d::FREE_SPACE);
}

// ---------------------------------------------------------------------------
// Test case 6 (MF1): invalid tide → evaluateCell returns nullopt even for a
// well-surveyed, reliable, fresh cell. This pins the safety contract: the
// layer must not compute clearance from an uninitialised tide value.
// ---------------------------------------------------------------------------
TEST(BathymetryLayer, InvalidTideYieldsNoInformation)
{
  BathymetryLayerForTest layer;
  layer.setMinimumDepth(1.0);
  layer.setMaximumCautionDepth(3.0);
  layer.setMaxUncertainty(0.5);
  // Deliberately do NOT call setMapTideValid(true) — map_tide_valid_ defaults false.
  layer.setMapTideZ(52.3);  // Even a "correct" z must be rejected without a valid flag.

  auto store = std::make_unique<BathymetryStore>(5);
  const auto cell = store->cellIndex(kLat, kLon);
  // A perfectly good surveyed cell (deep, reliable, fresh) must still be skipped.
  store->set(SourceLayer::Processed, cell, BathyCell{47.0, 0.1, 1LL});
  layer.setStore(std::move(store));

  const auto result = layer.evaluateCell(cell, /*now_ns=*/0);
  EXPECT_FALSE(result.has_value())
    << "No tide yet: layer must not write any cost (MF1 safety gate).";
}

// ---------------------------------------------------------------------------
// Test case 7 (S1/MF3, rewritten for #221): single fused surface — no prior
// epoch to fall back to. With the per-day epoch dimension dropped, a cell whose
// (only) record is over-uncertain has no older confident epoch to fall through
// to: shallowestReliable returns nullopt and the cell is LETHAL (the deliberate
// fallback-loss tradeoff recorded in ADR-0002 A1's supersession). This pins
// that an over-uncertain cell is LETHAL even when bestSource finds fresh data,
// that a confident-but-ancient record is stale (MF3 age-checks the reliable
// record), and that the same confident record when fresh is FREE.
// ---------------------------------------------------------------------------
TEST(BathymetryLayer, OverUncertainCellIsLethalWithNoPriorFallback)
{
  BathymetryLayerForTest layer;
  layer.setMinimumDepth(1.0);
  layer.setMaximumCautionDepth(3.0);
  layer.setMaxUncertainty(0.5);
  layer.setMaxAge(1.0);   // 1-second window.
  layer.setMapTideZ(0.0);
  layer.setMapTideValid(true);

  const int64_t now_ns = 10 * kNsPerSec;  // "now" = 10 s.

  // Single fused surface: one record per cell. A deep but OVER-UNCERTAIN + FRESH
  // record. bestSource finds it (it has data) but shallowestReliable returns
  // nullopt — and there is no prior epoch to fall through to, so the cell is
  // LETHAL despite the fresh timestamp.
  {
    auto store = std::make_unique<BathymetryStore>(5);
    const auto cell = store->cellIndex(kLat, kLon);
    const int64_t fresh_ns = 9 * kNsPerSec;  // 1 s ago — within max_age.
    store->set(SourceLayer::Processed, cell, BathyCell{-5.0, 5.0, fresh_ns});
    layer.setStore(std::move(store));

    const auto result = layer.evaluateCell(cell, now_ns);
    ASSERT_TRUE(result.has_value())
      << "Cell has data → must write something (not skip as unsurveyed).";
    EXPECT_EQ(*result, nav2_costmap_2d::LETHAL_OBSTACLE)
      << "Over-uncertain with no prior fallback → LETHAL (#221 tradeoff).";
  }

  // A confident but ANCIENT record is also LETHAL — MF3 age-checks the reliable
  // record shallowestReliable returns.
  {
    auto store = std::make_unique<BathymetryStore>(5);
    const auto cell = store->cellIndex(kLat, kLon);
    const int64_t ancient_ns = 1LL;  // nanosecond 1 — very old.
    store->set(SourceLayer::Processed, cell, BathyCell{-5.0, 0.1, ancient_ns});
    layer.setStore(std::move(store));

    const auto result = layer.evaluateCell(cell, now_ns);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, nav2_costmap_2d::LETHAL_OBSTACLE)
      << "Reliable but ancient record is stale → LETHAL (MF3).";
  }

  // The same confident record, fresh, is FREE — confirming the LETHAL verdicts
  // above came from the reliability/age gates, not from the data being absent.
  {
    auto store = std::make_unique<BathymetryStore>(5);
    const auto cell = store->cellIndex(kLat, kLon);
    store->set(SourceLayer::Processed, cell, BathyCell{-5.0, 0.1, now_ns});
    layer.setStore(std::move(store));

    const auto result = layer.evaluateCell(cell, now_ns);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, nav2_costmap_2d::FREE_SPACE)
      << "Deep, reliable, fresh record → FREE.";
  }
}

// ---------------------------------------------------------------------------
// Test case 8 (S4): computeCost boundary guard — degenerate ramp
// (maximum_caution_depth_ == minimum_depth_) must not divide by zero or
// produce a NaN cast. Also pins computeCost(minimum_depth_) == LETHAL.
// ---------------------------------------------------------------------------
TEST(BathymetryLayer, ComputeCostDegenerateRampAndBoundary)
{
  BathymetryLayerForTest layer;
  layer.setMinimumDepth(2.0);
  layer.setMaximumCautionDepth(2.0);  // degenerate: range == 0

  // Degenerate range: any clearance < maximum_caution_depth_ falls through to
  // the ramp but the range guard returns LETHAL (S4).
  EXPECT_EQ(layer.computeCost(1.9), nav2_costmap_2d::LETHAL_OBSTACLE)
    << "Below the (degenerate) boundary → LETHAL";
  // At exactly the boundary both the < minimum_depth_ check and the range guard
  // fire in sequence; the result must be LETHAL or FREE — not UB.
  const unsigned char at_boundary = layer.computeCost(2.0);
  EXPECT_TRUE(
    at_boundary == nav2_costmap_2d::LETHAL_OBSTACLE ||
    at_boundary == nav2_costmap_2d::FREE_SPACE)
    << "At minimum_depth_ with degenerate range: must be LETHAL or FREE, not UB.";

  // With a normal ramp, computeCost(minimum_depth_) must equal MAX_NON_OBSTACLE
  // (the top of the caution band, just inside LETHAL).
  layer.setMinimumDepth(1.0);
  layer.setMaximumCautionDepth(3.0);
  EXPECT_EQ(layer.computeCost(1.0), nav2_costmap_2d::MAX_NON_OBSTACLE)
    << "At minimum_depth_ on a normal ramp → MAX_NON_OBSTACLE (highest caution).";
}

// ---------------------------------------------------------------------------
// Test case 9 (acceptance criterion): memory-bound windowed residency. After
// loading a small window and then evicting outside a small box, the resident
// tile count stays bounded — the layer never holds the whole store.
// ---------------------------------------------------------------------------
namespace
{
// Sum the resident tiles across all layers of a store (one fused surface per
// layer since #221).
std::size_t residentTileCount(const BathymetryStore & store)
{
  std::size_t total = 0;
  for (const auto layer : marine_bathymetry_store::source_layers_by_priority) {
    total += store.tiles(layer).size();
  }
  return total;
}
}  // namespace

TEST(BathymetryLayer, WindowedResidencyStaysBounded)
{
  namespace fs = std::filesystem;
  const fs::path dir =
    fs::temp_directory_path() /
    ("bathymetry_layer_window_" + std::to_string(::getpid()));
  fs::remove_all(dir);
  fs::create_directories(dir);

  // Build a store spanning a wide geographic strip so its tiles land in
  // distinct GGGS grids, then persist it to disk. chart_writable=true so the
  // synthetic prior can be written on the read-only Chart layer.
  {
    BathymetryStore store(12, /*chart_writable=*/true);
    for (int k = 0; k < 8; ++k) {
      const double lon = -70.5 + 0.05 * static_cast<double>(k);
      const auto cell = store.cellIndex(43.0, lon);
      store.set(SourceLayer::Chart, cell, BathyCell{-10.0, 0.2, 1LL});
    }
    const std::size_t written = marine_bathymetry_store::save(store, dir.string(), nullptr);
    ASSERT_GT(written, 0u);
  }

  // A fresh consumer store loads only a small window, leaving most tiles on disk.
  BathymetryStore consumer(12, /*chart_writable=*/false);
  const auto small_min = gggs::geoPoint(42.999, -70.51);
  const auto small_max = gggs::geoPoint(43.001, -70.49);
  marine_bathymetry_store::loadWindow(consumer, dir.string(), small_min, small_max);
  const std::size_t small_resident = residentTileCount(consumer);
  ASSERT_GT(small_resident, 0u) << "small window should load at least one tile";

  // Expand the window to cover the whole strip, then evict back to the small
  // box. Residency must shrink back to the small-window count — the consumer is
  // never forced to hold the full store at once.
  const auto big_min = gggs::geoPoint(42.99, -70.55);
  const auto big_max = gggs::geoPoint(43.01, -70.05);
  marine_bathymetry_store::loadWindow(consumer, dir.string(), big_min, big_max);
  const std::size_t big_resident = residentTileCount(consumer);
  EXPECT_GT(big_resident, small_resident)
    << "expanding the window should load additional tiles";

  marine_bathymetry_store::evictOutside(consumer, small_min, small_max);
  const std::size_t evicted_resident = residentTileCount(consumer);
  EXPECT_LE(evicted_resident, small_resident)
    << "eviction must drop residency back to the small-window bound";
  EXPECT_LT(evicted_resident, big_resident)
    << "eviction must actually free the out-of-window tiles";

  fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Cost-tile cache / blit (#226). updateCosts no longer queries the store per
// cell every cycle; it blits pre-rendered, world-anchored tiles into the master
// grid. These exercise the blit (mapping + max-combine + cheapness) directly via
// the injectTile seam, without the store/TF/projection pipeline (generateTile's
// projection reuses the already-tested worldToLatLon + evaluateCell).
// ---------------------------------------------------------------------------

TEST(BathymetryLayer, TileBlitMaxCombineAndOffsets)
{
  BathymetryLayerForTest layer;
  layer.setStore(std::make_unique<BathymetryStore>(10));  // non-null for the guard
  layer.setEnabled(true);

  const int ts = layer.tileSize();
  // Tile at grid (0,0): world origin (0,0), 1 m cells, NO_INFORMATION default.
  auto tile = std::make_shared<nav2_costmap_2d::Costmap2D>(
    ts, ts, 1.0, 0.0, 0.0, nav2_costmap_2d::NO_INFORMATION);
  tile->setCost(5, 5, 100);
  tile->setCost(10, 20, 200);
  tile->setCost(7, 7, 80);
  tile->setCost(50, 50, nav2_costmap_2d::FREE_SPACE);
  // (40, 40) left NO_INFORMATION.
  layer.injectTile(0, 0, tile);

  nav2_costmap_2d::Costmap2D master(60, 60, 1.0, 0.0, 0.0, nav2_costmap_2d::FREE_SPACE);
  master.setCost(5, 5, 150);   // higher than the tile's 100 -> keep existing
  master.setCost(10, 20, 50);  // lower than the tile's 200 -> tile wins
  master.setCost(50, 50, nav2_costmap_2d::NO_INFORMATION);  // tile FREE fills it

  layer.updateCosts(master, 0, 0, 60, 60);

  EXPECT_EQ(static_cast<int>(master.getCost(5, 5)), 150)
    << "max-combine must keep the higher existing master cost";
  EXPECT_EQ(static_cast<int>(master.getCost(10, 20)), 200)
    << "tile raises a lower master cost (and the (10,20) offset maps correctly)";
  EXPECT_EQ(static_cast<int>(master.getCost(7, 7)), 80)
    << "tile cost fills over FREE_SPACE";
  EXPECT_EQ(
    static_cast<int>(master.getCost(40, 40)),
    static_cast<int>(nav2_costmap_2d::FREE_SPACE))
    << "tile NO_INFORMATION is skipped -- master left unchanged";
  EXPECT_EQ(
    static_cast<int>(master.getCost(50, 50)),
    static_cast<int>(nav2_costmap_2d::FREE_SPACE))
    << "tile cost fills a NO_INFORMATION master cell";
}

TEST(BathymetryLayer, TileBlitTracksRollingMasterOrigin)
{
  BathymetryLayerForTest layer;
  layer.setStore(std::make_unique<BathymetryStore>(10));
  layer.setEnabled(true);

  const int ts = layer.tileSize();
  auto tile = std::make_shared<nav2_costmap_2d::Costmap2D>(
    ts, ts, 1.0, 0.0, 0.0, nav2_costmap_2d::NO_INFORMATION);
  tile->setCost(35, 25, 170);
  layer.injectTile(0, 0, tile);

  // Master origin shifted to (30, 10): master cell (5, 15) is world ~(35.5,25.5),
  // i.e. tile cell (35, 25). Exercises the tile_offset_* index math (#226).
  nav2_costmap_2d::Costmap2D master(40, 40, 1.0, 30.0, 10.0, nav2_costmap_2d::FREE_SPACE);
  layer.updateCosts(master, 0, 0, 40, 40);

  EXPECT_EQ(static_cast<int>(master.getCost(5, 15)), 170)
    << "tile cell (35,25) must blit to master cell (5,15) under a (30,10) origin";
  EXPECT_EQ(
    static_cast<int>(master.getCost(6, 15)),
    static_cast<int>(nav2_costmap_2d::FREE_SPACE))
    << "the neighbouring master cell maps to an un-set tile cell";
}

TEST(BathymetryLayer, TileBlitIsCheapOverLargeGrid)
{
  BathymetryLayerForTest layer;
  layer.setStore(std::make_unique<BathymetryStore>(10));
  layer.setEnabled(true);

  const int ts = layer.tileSize();
  const int n = 500;  // 500x500 = 250k master cells
  const int tiles_per_side = (n + ts - 1) / ts;
  for (int ti = 0; ti < tiles_per_side; ++ti) {
    for (int tj = 0; tj < tiles_per_side; ++tj) {
      auto tile = std::make_shared<nav2_costmap_2d::Costmap2D>(
        ts, ts, 1.0,
        static_cast<double>(ti) * ts, static_cast<double>(tj) * ts,
        nav2_costmap_2d::FREE_SPACE);
      layer.injectTile(ti, tj, tile);
    }
  }

  nav2_costmap_2d::Costmap2D master(n, n, 1.0, 0.0, 0.0, nav2_costmap_2d::NO_INFORMATION);

  const auto t0 = std::chrono::steady_clock::now();
  layer.updateCosts(master, 0, 0, n, n);
  const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - t0).count();

  // A memory-copy blit of 250k cells is sub-ms to a few ms; the old per-cell path
  // (~35 us/cell) would be ~8.75 s. A 1 s bound flags a regression to per-cell
  // work without being flaky on a loaded CI host.
  std::cout << "[ PERF     ] tile blit over " << (n * n) << " cells: "
            << dt << " ms" << std::endl;
  EXPECT_LT(dt, 1000)
    << "updateCosts must be an O(cells) blit, not a per-cell store query";
  EXPECT_EQ(
    static_cast<int>(master.getCost(250, 250)),
    static_cast<int>(nav2_costmap_2d::FREE_SPACE))
    << "interior master cell got the tile's cost";
}

// ---------------------------------------------------------------------------
// Test case (#220): the water-surface height is read from map_frame, NOT the
// costmap's global frame. bizzy's costmaps render in map_tide, so referencing
// the global frame was a degenerate self-lookup (identity → z=0) that read the
// whole survey LETHAL. These tests pin the frame the tide is measured against.
// ---------------------------------------------------------------------------

// Publish a map_frame -> map_tide transform with the sea surface at the given
// ellipsoidal height (z of map_tide in map). map_frame's z=0 is the ellipsoid.
static void publishMapTide(
  std::shared_ptr<tf2_ros::Buffer> tf_buffer,
  const std::string & map_frame,
  double surface_z)
{
  static int64_t stamp_ns = 1;
  geometry_msgs::msg::TransformStamped mt;
  mt.header.stamp = rclcpp::Time(stamp_ns++);
  mt.header.frame_id = map_frame;
  mt.child_frame_id = "map_tide";
  mt.transform.translation.z = surface_z;
  mt.transform.rotation.w = 1.0;
  tf_buffer->setTransform(mt, "test_authority", false);
}

class TideFrameTest : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    // ExpandUserPathHandlesTilde overwrites HOME to a non-existent path; without
    // a writable log directory rclcpp::init() throws while configuring logging.
    // Pin ROS_LOG_DIR to a writable temp dir so init succeeds regardless of the
    // HOME another test may have left behind (test-ordering isolation).
    ::setenv("ROS_LOG_DIR", "/tmp/bathymetry_layer_test_log", 1);
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
    }
  }
  static void TearDownTestSuite()
  {
    if (rclcpp::ok()) {
      rclcpp::shutdown();
    }
  }
};

// The costmap global frame is map_tide (as on bizzy), map_frame is the distinct
// ellipsoid frame. updateBounds must read the surface height from map_frame ->
// map_tide (≈ +48.88 m), not from a global-frame self-lookup (which would be 0).
TEST_F(TideFrameTest, MapTideZReadFromMapFrameNotGlobalFrame)
{
  auto node = std::make_shared<nav2_util::LifecycleNode>("test_tide_frame");
  node->declare_parameter("bathymetry_layer.map_frame", std::string("map"));
  node->declare_parameter("bathymetry_layer.map_tide_frame", std::string("map_tide"));

  auto tf_buffer = std::make_shared<tf2_ros::Buffer>(node->get_clock());
  publishMapTide(tf_buffer, "map", 48.88);

  // Global frame is map_tide — the exact deployment that triggered #220.
  nav2_costmap_2d::LayeredCostmap layered_costmap("map_tide", false, false);
  layered_costmap.resizeMap(10, 10, 1.0, 0.0, 0.0);

  BathymetryLayerForTest layer;
  layer.initialize(&layered_costmap, "bathymetry_layer", tf_buffer.get(), node, nullptr);

  double minx = 1e30, miny = 1e30, maxx = -1e30, maxy = -1e30;
  layer.updateBounds(0.0, 0.0, 0.0, &minx, &miny, &maxx, &maxy);

  EXPECT_TRUE(layer.isMapTideValid())
    << "a valid map_frame -> map_tide transform must open the tide gate";
  EXPECT_NEAR(layer.getMapTideZ(), 48.88, 1e-6)
    << "map_tide_z_ must be the surface height in map_frame, not 0 from a "
       "global-frame self-lookup";
}

// A degenerate config (map_frame == map_tide_frame) must NOT be accepted: the
// lookup would be an identity (z=0). The tide gate stays closed so no bogus
// LETHAL flood is written — fail loud, do not paper over it.
TEST_F(TideFrameTest, DegenerateTideFrameConfigRejected)
{
  auto node = std::make_shared<nav2_util::LifecycleNode>("test_tide_frame_degenerate");
  node->declare_parameter("bathymetry_layer.map_frame", std::string("map_tide"));
  node->declare_parameter("bathymetry_layer.map_tide_frame", std::string("map_tide"));

  auto tf_buffer = std::make_shared<tf2_ros::Buffer>(node->get_clock());

  nav2_costmap_2d::LayeredCostmap layered_costmap("map_tide", false, false);
  layered_costmap.resizeMap(10, 10, 1.0, 0.0, 0.0);

  BathymetryLayerForTest layer;
  layer.initialize(&layered_costmap, "bathymetry_layer", tf_buffer.get(), node, nullptr);

  double minx = 1e30, miny = 1e30, maxx = -1e30, maxy = -1e30;
  layer.updateBounds(0.0, 0.0, 0.0, &minx, &miny, &maxx, &maxy);

  EXPECT_FALSE(layer.isMapTideValid())
    << "map_frame == map_tide_frame is a degenerate identity lookup and must be "
       "rejected as an invalid tide";
}
