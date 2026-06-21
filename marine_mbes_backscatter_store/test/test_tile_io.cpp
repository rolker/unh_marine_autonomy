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

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "marine_mbes_backscatter_store/mbes_store.hpp"
#include "marine_mbes_backscatter_store/registry.hpp"
#include "marine_mbes_backscatter_store/tile_io.hpp"
#include "marine_tiled_raster_store/tile_io.hpp"
#include "marine_tiled_raster_store/tiled_raster_tile.hpp"

using marine_mbes_backscatter_store::MbesBackscatterStore;
using marine_mbes_backscatter_store::MbesCell;
using marine_mbes_backscatter_store::SourceLayer;
using marine_mbes_backscatter_store::SourceRecord;
using marine_mbes_backscatter_store::SourceRegistry;

namespace fs = std::filesystem;

class TileIoTest : public ::testing::Test
{
protected:
  fs::path dir_;

  void SetUp() override
  {
    const std::string name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
    dir_ = fs::temp_directory_path() / ("marine_mbes_store_" + name);
    fs::remove_all(dir_);
  }

  void TearDown() override
  {
    fs::remove_all(dir_);
  }
};

TEST_F(TileIoTest, RoundTripPreservesCells)
{
  MbesBackscatterStore store(5);
  // ns stamps with sub-second precision the Int64 time tile preserves exactly.
  const int64_t ts_draft = 1'780'000'000'123'456'789LL;
  const int64_t ts_proc = 1'790'000'000'987'654'321LL;
  store.set(
    SourceLayer::Draft, store.cellIndex(43.0, -70.5),
    MbesCell{-18.5f, 0.375f, ts_draft, 7u});
  store.set(
    SourceLayer::Processed, store.cellIndex(44.0, -71.0),
    MbesCell{-12.5f, 0.125f, ts_proc, 0u});

  EXPECT_EQ(marine_mbes_backscatter_store::save(store, dir_.string()), 2u);

  MbesBackscatterStore reloaded(5);
  EXPECT_EQ(marine_mbes_backscatter_store::load(reloaded, dir_.string()), 2u);

  const auto draft = reloaded.get(SourceLayer::Draft, reloaded.cellIndex(43.0, -70.5));
  ASSERT_TRUE(draft.has_value());
  EXPECT_FLOAT_EQ(draft->intensity, -18.5f);
  EXPECT_FLOAT_EQ(draft->intensity_variance, 0.375f);
  EXPECT_EQ(draft->timestamp, ts_draft);       // int64 ns survives exactly
  EXPECT_EQ(draft->source_index, 7u);

  const auto processed = reloaded.get(SourceLayer::Processed, reloaded.cellIndex(44.0, -71.0));
  ASSERT_TRUE(processed.has_value());
  EXPECT_FLOAT_EQ(processed->intensity, -12.5f);
  EXPECT_EQ(processed->timestamp, ts_proc);
}

TEST_F(TileIoTest, RoundTripPreservesSourceIndex)
{
  // The per-cell source index round-trips through the separate UInt16 _source.tif
  // independently of intensity / time.
  MbesBackscatterStore store(5);
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), MbesCell{-30.0f, 0.5f, 10LL, 1u});
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.4), MbesCell{-31.0f, 0.5f, 11LL, 4242u});
  EXPECT_EQ(marine_mbes_backscatter_store::save(store, dir_.string()), 1u);

  MbesBackscatterStore reloaded(5);
  EXPECT_EQ(marine_mbes_backscatter_store::load(reloaded, dir_.string()), 1u);
  EXPECT_EQ(
    reloaded.get(SourceLayer::Draft, reloaded.cellIndex(43.0, -70.5))->source_index, 1u);
  EXPECT_EQ(
    reloaded.get(SourceLayer::Draft, reloaded.cellIndex(43.0, -70.4))->source_index, 4242u);
}

TEST_F(TileIoTest, MissingTimeTileLoadsAsZero)
{
  // A missing _time.tif companion (e.g. a partial crashed write) must not fail —
  // the timestamp band fills with 0 (degraded-mode recovery).
  MbesBackscatterStore store(5);
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), MbesCell{-30.0f, 0.5f, 99LL, 2u});
  marine_mbes_backscatter_store::save(store, dir_.string());

  for (const auto & e : fs::recursive_directory_iterator(dir_ / "draft")) {
    if (e.is_regular_file() &&
      e.path().filename().string().find("_time.tif") != std::string::npos)
    {
      fs::remove(e.path());
    }
  }

  MbesBackscatterStore reloaded(5);
  EXPECT_EQ(marine_mbes_backscatter_store::load(reloaded, dir_.string()), 1u);
  const auto got = reloaded.get(SourceLayer::Draft, reloaded.cellIndex(43.0, -70.5));
  ASSERT_TRUE(got.has_value());
  EXPECT_FLOAT_EQ(got->intensity, -30.0f);   // value tile still loaded
  EXPECT_EQ(got->timestamp, 0);              // missing time tile -> 0
  EXPECT_EQ(got->source_index, 2u);          // source tile still loaded
}

TEST_F(TileIoTest, MissingSourceTileLoadsAsZero)
{
  MbesBackscatterStore store(5);
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), MbesCell{-30.0f, 0.5f, 99LL, 5u});
  marine_mbes_backscatter_store::save(store, dir_.string());

  for (const auto & e : fs::recursive_directory_iterator(dir_ / "draft")) {
    if (e.is_regular_file() &&
      e.path().filename().string().find("_source.tif") != std::string::npos)
    {
      fs::remove(e.path());
    }
  }

  MbesBackscatterStore reloaded(5);
  EXPECT_EQ(marine_mbes_backscatter_store::load(reloaded, dir_.string()), 1u);
  const auto got = reloaded.get(SourceLayer::Draft, reloaded.cellIndex(43.0, -70.5));
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->timestamp, 99);         // time tile still loaded
  EXPECT_EQ(got->source_index, 0u);      // missing source tile -> 0
}

TEST_F(TileIoTest, LayersWriteToSeparateSubdirectories)
{
  MbesBackscatterStore store(5);
  store.set(SourceLayer::Processed, store.cellIndex(43.0, -70.5), MbesCell{-10.0f, 0.1f, 1LL});
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), MbesCell{-12.0f, 0.5f, 2LL});
  marine_mbes_backscatter_store::save(store, dir_.string());

  EXPECT_TRUE(fs::is_directory(dir_ / "processed"));
  EXPECT_TRUE(fs::is_directory(dir_ / "draft"));
}

TEST_F(TileIoTest, LoadedTilesAreCleanAndDontResave)
{
  MbesBackscatterStore store(5);
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), MbesCell{-30.0f, 0.5f, 1LL});

  EXPECT_EQ(marine_mbes_backscatter_store::save(store, dir_.string()), 1u);
  EXPECT_EQ(marine_mbes_backscatter_store::save(store, dir_.string()), 0u);  // incremental

  MbesBackscatterStore reloaded(5);
  marine_mbes_backscatter_store::load(reloaded, dir_.string());
  EXPECT_EQ(marine_mbes_backscatter_store::save(reloaded, dir_.string()), 0u);
}

TEST_F(TileIoTest, LoadRejectsTilesFromAnotherLevel)
{
  MbesBackscatterStore store(5);
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), MbesCell{-30.0f, 0.5f, 1LL});
  marine_mbes_backscatter_store::save(store, dir_.string());

  MbesBackscatterStore wrong_level(6);
  EXPECT_THROW(
    marine_mbes_backscatter_store::load(wrong_level, dir_.string()), std::runtime_error);
}

TEST_F(TileIoTest, LoadRejectsCompanionWithWrongGrid)
{
  // If a _time.tif companion is replaced with a file from a different grid
  // (tampering / mis-rename), loadTile must throw rather than silently combining
  // cell-for-cell data from two geographic locations.
  namespace mtrs = marine_tiled_raster_store;
  const gggs::Level level(5);
  const gggs::GridIndex grid_a = level.gridIndex(43.0, -70.5);
  const gggs::GridIndex grid_b = level.gridIndex(44.0, -71.0);
  ASSERT_FALSE(grid_a == grid_b);

  MbesBackscatterStore store(5);
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), MbesCell{-30.0f, 0.5f, 42LL, 1u});
  marine_mbes_backscatter_store::save(store, dir_.string());

  const std::string value_filename = marine_mbes_backscatter_store::tileFilename(grid_a);
  const fs::path time_path =
    dir_ / "draft" / (fs::path(value_filename).stem().string() + "_time.tif");
  mtrs::TiledRasterTile<int64_t> wrong_time(grid_b, 1, int64_t{0});
  wrong_time.set(0, 0, 0, 99LL);
  mtrs::saveTile<int64_t>(wrong_time, time_path.string(), {std::nullopt});

  MbesBackscatterStore reloaded(5);
  EXPECT_THROW(
    marine_mbes_backscatter_store::load(reloaded, dir_.string()), std::runtime_error);
}

TEST_F(TileIoTest, RegistryWriteInternRoundTrip)
{
  // Register two sources, save with the store, reload into a fresh registry, and
  // verify intern idempotency, dense 1-based indices, and the round-trip.
  MbesBackscatterStore store(5);
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), MbesCell{-20.0f, 0.5f, 1LL, 1u});

  SourceRegistry registry;
  const uint16_t a = registry.registerSource(
    SourceRecord{"bizzy:norbit-m3:0", "bizzyboat", "norbit-m3", "mbes-backscatter",
      "massabesic-2026", ""});
  const uint16_t b = registry.registerSource(
    SourceRecord{"izzy:norbit-m3:0", "izzyboat", "norbit-m3", "mbes-backscatter", "", ""});
  const uint16_t a2 = registry.registerSource(
    SourceRecord{"bizzy:norbit-m3:0", "x", "y", "", "", ""});   // idempotent
  EXPECT_EQ(a, 1u);                  // first real index (0 reserved)
  EXPECT_EQ(b, 2u);
  EXPECT_EQ(a2, a);                  // same source_id -> same index, no new entry
  EXPECT_EQ(registry.size(), 2u);
  EXPECT_FALSE(registry.lookup(SourceRegistry::kUnset).has_value());  // index 0 is unset

  marine_mbes_backscatter_store::save(store, dir_.string(), &registry);
  EXPECT_TRUE(fs::is_regular_file(dir_ / "registry.json"));
  EXPECT_FALSE(fs::exists(dir_ / "registry.json.tmp"));   // atomic publish, no scratch

  SourceRegistry reloaded;
  marine_mbes_backscatter_store::load(store, dir_.string(), &reloaded);
  EXPECT_EQ(reloaded.size(), 2u);
  const auto rec = reloaded.lookup(1);
  ASSERT_TRUE(rec.has_value());
  EXPECT_EQ(rec->platform, "bizzyboat");
  EXPECT_EQ(rec->sensor_class, "mbes-backscatter");
  EXPECT_EQ(rec->campaign, "massabesic-2026");
}

// --- Fix #1: registry.cpp loadRegistry index and duplicate source_id validation ---

TEST_F(TileIoTest, LoadRegistryRejectsReorderedIndex)
{
  // A hand-edited registry.json with a swapped/reordered "index" field must be
  // rejected with a clear error (not silently accepted as a contiguous sequence).
  fs::create_directories(dir_);
  const nlohmann::json bad_registry = {
    {"version", 1},
    {"sources", nlohmann::json::array({
      {{"index", 2}, {"source_id", "a"}, {"platform", "p1"}, {"sensor", "s"},
       {"sensor_class", ""}, {"campaign", ""}, {"calibration_ref", ""}},
      {{"index", 1}, {"source_id", "b"}, {"platform", "p2"}, {"sensor", "s"},
       {"sensor_class", ""}, {"campaign", ""}, {"calibration_ref", ""}},
    })}
  };
  {
    std::ofstream out(dir_ / "registry.json");
    out << bad_registry.dump(2);
  }
  SourceRegistry reg;
  EXPECT_THROW(reg.loadRegistry(dir_.string()), std::runtime_error);
}

TEST_F(TileIoTest, LoadRegistryRejectsMissingIndexField)
{
  // An entry that lacks the "index" field entirely must also be rejected.
  fs::create_directories(dir_);
  const nlohmann::json bad_registry = {
    {"version", 1},
    {"sources", nlohmann::json::array({
      // "index" field deliberately omitted
      {{"source_id", "a"}, {"platform", "p1"}, {"sensor", "s"},
       {"sensor_class", ""}, {"campaign", ""}, {"calibration_ref", ""}},
    })}
  };
  {
    std::ofstream out(dir_ / "registry.json");
    out << bad_registry.dump(2);
  }
  SourceRegistry reg;
  EXPECT_THROW(reg.loadRegistry(dir_.string()), std::runtime_error);
}

TEST_F(TileIoTest, LoadRegistryRejectsDuplicateSourceId)
{
  // Two entries with the same source_id at distinct contiguous indices must be
  // rejected: duplicate source_id desynchronises by_index_ and by_source_id_.
  fs::create_directories(dir_);
  const nlohmann::json bad_registry = {
    {"version", 1},
    {"sources", nlohmann::json::array({
      {{"index", 1}, {"source_id", "dup"}, {"platform", "p1"}, {"sensor", "s"},
       {"sensor_class", ""}, {"campaign", ""}, {"calibration_ref", ""}},
      {{"index", 2}, {"source_id", "dup"}, {"platform", "p2"}, {"sensor", "s"},
       {"sensor_class", ""}, {"campaign", ""}, {"calibration_ref", ""}},
    })}
  };
  {
    std::ofstream out(dir_ / "registry.json");
    out << bad_registry.dump(2);
  }
  SourceRegistry reg;
  EXPECT_THROW(reg.loadRegistry(dir_.string()), std::runtime_error);
}
