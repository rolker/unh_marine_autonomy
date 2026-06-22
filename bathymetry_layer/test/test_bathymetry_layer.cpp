// Copyright (c) 2026 Roland Arsenault
// Licensed under BSD license

#include <gtest/gtest.h>

#include <unistd.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>

#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathy_cell.hpp"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/tile_io.hpp"
#include "nav2_costmap_2d/cost_values.hpp"

#include "bathymetry_layer.hpp"

using bathymetry_layer::BathymetryLayer;
using marine_bathymetry_store::BathyCell;
using marine_bathymetry_store::BathymetryStore;
using marine_bathymetry_store::SourceLayer;

namespace
{
// A single ISO-date epoch label (ADR-0002 A1).
const char * const kEpoch = "2026-06-10";

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
  void setMinimumDepth(double v) {minimum_depth_ = v;}
  void setMaximumCautionDepth(double v) {maximum_caution_depth_ = v;}
  void setMaxUncertainty(double v) {max_uncertainty_ = v;}
  void setMaxAge(double v) {max_age_ = v;}

  using BathymetryLayer::computeCost;
  using BathymetryLayer::evaluateCell;
  using BathymetryLayer::isStale;
};

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

  // A cell with no data anywhere in the store.
  const auto cell = layer.store().cellIndex(kLat, kLon);
  const auto result = layer.evaluateCell(cell, /*now_ns=*/0);
  EXPECT_FALSE(result.has_value()) << "Unsurveyed cells must not be written.";
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
  store->set(SourceLayer::Processed, kEpoch, cell, BathyCell{-0.5, 0.1, 1LL});
  layer.setStore(std::move(store));
  auto shoal = layer.evaluateCell(cell, /*now_ns=*/0);
  ASSERT_TRUE(shoal.has_value());
  EXPECT_EQ(*shoal, nav2_costmap_2d::LETHAL_OBSTACLE);

  // Deep: seafloor 5 m down → clearance 5 m >= 3.0 → FREE_SPACE.
  auto deep_store = std::make_unique<BathymetryStore>(5);
  const auto deep_cell = deep_store->cellIndex(kLat, kLon);
  deep_store->set(SourceLayer::Processed, kEpoch, deep_cell, BathyCell{-5.0, 0.1, 1LL});
  layer.setStore(std::move(deep_store));
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
  store->set(SourceLayer::Processed, kEpoch, cell, BathyCell{-10.0, 5.0, 1LL});
  layer.setStore(std::move(store));

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
  store->set(SourceLayer::Processed, kEpoch, cell, BathyCell{-5.0, 0.1, 1LL});
  layer.setStore(std::move(store));

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
// Test case 6 (acceptance criterion): memory-bound windowed residency. After
// loading a small window and then evicting outside a small box, the resident
// tile count stays bounded — the layer never holds the whole store.
// ---------------------------------------------------------------------------
namespace
{
// Sum the resident tiles across all layers and epochs of a store.
std::size_t residentTileCount(const BathymetryStore & store)
{
  std::size_t total = 0;
  for (const auto layer : marine_bathymetry_store::source_layers_by_priority) {
    for (const auto & epoch_pair : store.epochs(layer)) {
      total += epoch_pair.second.tiles.size();
    }
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
      store.set(SourceLayer::Chart, kEpoch, cell, BathyCell{-10.0, 0.2, 1LL});
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
