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
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/registry.hpp"
#include "marine_bathymetry_store/tile_io.hpp"

using marine_bathymetry_store::BathyCell;
using marine_bathymetry_store::BathymetryStore;
using marine_bathymetry_store::SourceLayer;
using marine_bathymetry_store::SourceRecord;
using marine_bathymetry_store::SourceRegistry;

namespace fs = std::filesystem;

class TileIoTest : public ::testing::Test
{
protected:
  fs::path dir_;

  void SetUp() override
  {
    const std::string name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
    dir_ = fs::temp_directory_path() / ("marine_bathy_store_" + name);
    fs::remove_all(dir_);
  }

  void TearDown() override
  {
    fs::remove_all(dir_);
  }
};

TEST_F(TileIoTest, RoundTripPreservesCells)
{
  BathymetryStore store(5);
  // int64 ns stamps with sub-second precision that a Float64 seconds band could
  // not have represented exactly — the Int64 time tile preserves them bit-exact.
  const int64_t ts_draft = 1'780'000'000'123'456'789LL;
  const int64_t ts_proc = 1'790'000'000'987'654'321LL;
  store.set(
    SourceLayer::Draft, store.cellIndex(43.0, -70.5),
    BathyCell{-30.123, 0.456, ts_draft, 7u});
  store.set(
    SourceLayer::Processed, store.cellIndex(44.0, -71.0),
    BathyCell{-12.5, 0.2, ts_proc, 0u});

  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 2u);

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 2u);

  const auto draft = reloaded.get(SourceLayer::Draft, reloaded.cellIndex(43.0, -70.5));
  ASSERT_TRUE(draft.has_value());
  EXPECT_DOUBLE_EQ(draft->depth, -30.123);
  EXPECT_DOUBLE_EQ(draft->uncertainty, 0.456);
  // Int64 ns timestamp band: nanosecond stamps survive exactly.
  EXPECT_EQ(draft->timestamp, ts_draft);
  EXPECT_EQ(draft->source_index, 7u);

  const auto processed = reloaded.get(SourceLayer::Processed, reloaded.cellIndex(44.0, -71.0));
  ASSERT_TRUE(processed.has_value());
  EXPECT_DOUBLE_EQ(processed->depth, -12.5);
  EXPECT_EQ(processed->timestamp, ts_proc);
}

TEST_F(TileIoTest, RoundTripPreservesSourceIndex)
{
  // The per-cell source index (registry handle, ADR-0005 D2/D8) round-trips
  // through the separate UInt16 _source.tif independently of depth/time.
  BathymetryStore store(5);
  store.set(
    SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 10LL, 1u});
  store.set(
    SourceLayer::Draft, store.cellIndex(43.0, -70.4), BathyCell{-31.0, 0.5, 11LL, 4242u});
  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 1u);

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 1u);
  EXPECT_EQ(
    reloaded.get(SourceLayer::Draft, reloaded.cellIndex(43.0, -70.5))->source_index, 1u);
  EXPECT_EQ(
    reloaded.get(SourceLayer::Draft, reloaded.cellIndex(43.0, -70.4))->source_index, 4242u);
}

TEST_F(TileIoTest, MissingTimeTileLoadsAsZero)
{
  // Pre-migration single-platform data has no _time.tif. Deleting it before load
  // must not fail — the timestamp band fills with 0 (backward compatibility).
  BathymetryStore store(5);
  store.set(
    SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 99LL, 2u});
  marine_bathymetry_store::save(store, dir_.string());

  // Remove every _time.tif under the draft layer.
  for (const auto & e : fs::recursive_directory_iterator(dir_ / "draft")) {
    if (e.is_regular_file() && e.path().filename().string().find("_time.tif") != std::string::npos)
    {
      fs::remove(e.path());
    }
  }

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 1u);
  const auto got = reloaded.get(SourceLayer::Draft, reloaded.cellIndex(43.0, -70.5));
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, -30.0);   // value tile still loaded
  EXPECT_EQ(got->timestamp, 0);          // missing time tile -> 0
  EXPECT_EQ(got->source_index, 2u);      // source tile still loaded
}

TEST_F(TileIoTest, MissingSourceTileLoadsAsZero)
{
  BathymetryStore store(5);
  store.set(
    SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 99LL, 5u});
  marine_bathymetry_store::save(store, dir_.string());

  for (const auto & e : fs::recursive_directory_iterator(dir_ / "draft")) {
    if (e.is_regular_file() &&
      e.path().filename().string().find("_source.tif") != std::string::npos)
    {
      fs::remove(e.path());
    }
  }

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 1u);
  const auto got = reloaded.get(SourceLayer::Draft, reloaded.cellIndex(43.0, -70.5));
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->timestamp, 99);         // time tile still loaded
  EXPECT_EQ(got->source_index, 0u);      // missing source tile -> 0
}

TEST_F(TileIoTest, LoadedTilesAreCleanAndDontResave)
{
  BathymetryStore store(5);
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 1LL});

  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 1u);
  // No changes since the last save -> nothing re-written (incremental save).
  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 0u);

  // Reloaded tiles are clean, so saving the reloaded store writes nothing.
  BathymetryStore reloaded(5);
  marine_bathymetry_store::load(reloaded, dir_.string());
  EXPECT_EQ(marine_bathymetry_store::save(reloaded, dir_.string()), 0u);
}

TEST_F(TileIoTest, WritingAfterSaveRedirties)
{
  BathymetryStore store(5);
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 1LL});
  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 1u);

  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-31.0, 0.4, 2LL});
  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 1u);
}

TEST_F(TileIoTest, LayersWriteToSeparateSubdirectories)
{
  BathymetryStore store(5);
  store.set(SourceLayer::Processed, store.cellIndex(43.0, -70.5), BathyCell{-10.0, 0.1, 1LL});
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-12.0, 0.5, 2LL});
  marine_bathymetry_store::save(store, dir_.string());

  EXPECT_TRUE(fs::is_directory(dir_ / "processed"));
  EXPECT_TRUE(fs::is_directory(dir_ / "draft"));
}

TEST_F(TileIoTest, LoadRejectsTilesFromAnotherLevel)
{
  BathymetryStore store(5);
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 1LL});
  marine_bathymetry_store::save(store, dir_.string());

  BathymetryStore wrong_level(6);
  EXPECT_THROW(marine_bathymetry_store::load(wrong_level, dir_.string()), std::runtime_error);
}

TEST_F(TileIoTest, ChartRoundTripsAndLoadsIntoReadOnlyStore)
{
  // The importer writes Chart (chart_writable); the runtime loads it into a
  // default (read-only-Chart) store. load() populates via getOrCreateTile, not
  // set(), so the prior loads even though live set(Chart) stays forbidden.
  const int64_t ts = 1'780'000'000'000'000'000LL;
  BathymetryStore writer(5, /*chart_writable=*/true);
  writer.set(SourceLayer::Chart, writer.cellIndex(43.0, -70.5), BathyCell{38.58, 3.0, ts});
  EXPECT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 1u);
  EXPECT_TRUE(fs::is_directory(dir_ / "chart"));

  BathymetryStore runtime(5);   // Chart NOT writable
  EXPECT_FALSE(runtime.chartWritable());
  EXPECT_EQ(marine_bathymetry_store::load(runtime, dir_.string()), 1u);

  const auto got = runtime.get(SourceLayer::Chart, runtime.cellIndex(43.0, -70.5));
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, 38.58);
  EXPECT_EQ(got->timestamp, ts);

  // The read-only guard still holds after the prior is loaded.
  EXPECT_THROW(
    runtime.set(SourceLayer::Chart, runtime.cellIndex(43.0, -70.5), BathyCell{1.0, 1.0, 1LL}),
    std::logic_error);
}

TEST_F(TileIoTest, ProcessedDraftOnlyStoreLoadsWithoutChartDir)
{
  // Back-compat: a Phase-1 store with no chart/ subdir still saves and loads;
  // the absent chart layer is simply skipped, not an error.
  BathymetryStore store(5);
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 1LL});
  marine_bathymetry_store::save(store, dir_.string());
  EXPECT_FALSE(fs::exists(dir_ / "chart"));   // no spurious empty chart/ dir

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 1u);
  EXPECT_TRUE(reloaded.tiles(SourceLayer::Chart).empty());
}

TEST_F(TileIoTest, RegistryAtomicWrite)
{
  // saveRegistry must publish registry.json atomically: the final file exists
  // and no .tmp scratch file is left behind.
  fs::create_directories(dir_);
  SourceRegistry registry;
  const uint16_t idx = registry.registerSource(
    SourceRecord{"bizzy:m3:0", "bizzy", "m3", "multibeam", "massabesic-2026", "ellipsoid"});
  EXPECT_EQ(idx, 1u);                           // first real index (0 reserved)

  registry.saveRegistry(dir_.string());
  EXPECT_TRUE(fs::is_regular_file(dir_ / "registry.json"));
  EXPECT_FALSE(fs::exists(dir_ / "registry.json.tmp"));   // no leftover scratch

  SourceRegistry loaded;
  loaded.loadRegistry(dir_.string());
  const auto rec = loaded.lookup(1);
  ASSERT_TRUE(rec.has_value());
  EXPECT_EQ(rec->platform, "bizzy");
  EXPECT_EQ(rec->datum, "ellipsoid");
  EXPECT_FALSE(loaded.lookup(SourceRegistry::kUnset).has_value());   // index 0 is unset
}

TEST_F(TileIoTest, RegistryRegisterSourceIsIdempotent)
{
  SourceRegistry registry;
  const uint16_t a = registry.registerSource(SourceRecord{"bizzy:m3:0", "bizzy", "m3", "", "", ""});
  const uint16_t b = registry.registerSource(SourceRecord{"izzy:m3:0", "izzy", "m3", "", "", ""});
  const uint16_t a2 = registry.registerSource(SourceRecord{"bizzy:m3:0", "x", "y", "", "", ""});
  EXPECT_EQ(a, 1u);
  EXPECT_EQ(b, 2u);
  EXPECT_EQ(a2, a);                 // same source_id -> same index, no new entry
  EXPECT_EQ(registry.size(), 2u);
}

TEST_F(TileIoTest, SaveWritesRegistryWhenProvided)
{
  // The store save() persists the registry sidecar once, at the store root.
  BathymetryStore store(5);
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 1LL, 1u});
  SourceRegistry registry;
  registry.registerSource(SourceRecord{"bizzy:m3:0", "bizzy", "m3", "multibeam", "", "ellipsoid"});

  marine_bathymetry_store::save(store, dir_.string(), &registry);
  EXPECT_TRUE(fs::is_regular_file(dir_ / "registry.json"));

  SourceRegistry reloaded;
  marine_bathymetry_store::load(store, dir_.string(), &reloaded);
  EXPECT_EQ(reloaded.size(), 1u);
  ASSERT_TRUE(reloaded.lookup(1).has_value());
  EXPECT_EQ(reloaded.lookup(1)->platform, "bizzy");
}
