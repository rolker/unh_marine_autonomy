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
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"
#include "marine_bathymetry_store/registry.hpp"
#include "marine_bathymetry_store/tile_io.hpp"
#include "marine_tiled_raster_store/tile_io.hpp"

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

  const auto pcell = reloaded.cellIndex(44.0, -71.0);
  const auto processed = reloaded.get(SourceLayer::Processed, pcell);
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
  const auto pcell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Processed, pcell, BathyCell{-10.0, 0.1, 1LL});
  store.set(SourceLayer::Draft, pcell, BathyCell{-12.0, 0.5, 2LL});
  marine_bathymetry_store::save(store, dir_.string());

  EXPECT_TRUE(fs::is_directory(dir_ / "processed"));
  EXPECT_TRUE(fs::is_directory(dir_ / "draft"));
}

TEST_F(TileIoTest, LoadAcceptsMixedLevelTiles)
{
  // The store is multi-level (ADR-0002 §D2): tiles at different levels share an
  // epoch directory, and load recovers each tile's level from its filename. A
  // store loaded with a DIFFERENT default level still reads them all back.
  BathymetryStore writer(5);
  gggs::Level coarse(4);
  gggs::Level fine(7);
  const auto coarse_cell = coarse.cellIndex(gggs::geoPoint(43.0, -70.5));
  const auto fine_cell = fine.cellIndex(gggs::geoPoint(43.0, -70.5));
  writer.set(SourceLayer::Draft, coarse_cell, BathyCell{-30.0, 0.5, 1LL});
  writer.set(SourceLayer::Draft, fine_cell, BathyCell{-22.0, 0.3, 2LL});
  EXPECT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 2u);

  // Reload into a store whose default level (6) matches NEITHER stored level.
  BathymetryStore reloaded(6);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 2u);
  const auto coarse_got = reloaded.get(SourceLayer::Draft, coarse_cell);
  ASSERT_TRUE(coarse_got.has_value());
  EXPECT_DOUBLE_EQ(coarse_got->depth, -30.0);
  const auto fine_got = reloaded.get(SourceLayer::Draft, fine_cell);
  ASSERT_TRUE(fine_got.has_value());
  EXPECT_DOUBLE_EQ(fine_got->depth, -22.0);
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

  const auto rcell = runtime.cellIndex(43.0, -70.5);
  const auto got = runtime.get(SourceLayer::Chart, rcell);
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, 38.58);
  EXPECT_EQ(got->timestamp, ts);

  // The read-only guard still holds after the prior is loaded.
  EXPECT_THROW(
    runtime.set(SourceLayer::Chart, rcell, BathyCell{1.0, 1.0, 1LL}),
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
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5),
    BathyCell{-30.0, 0.5, 1LL, 1u});
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

// --- Item 1: registry.cpp loadRegistry index validation ---

TEST_F(TileIoTest, LoadRegistryRejectsReorderedIndex)
{
  // A hand-edited registry.json with a swapped/reordered "index" field must be
  // rejected with a clear error (not silently accepted as a contiguous sequence).
  fs::create_directories(dir_);
  // Write a registry with two entries but with their index values swapped
  // (entry 0 claims index=2, entry 1 claims index=1).
  const nlohmann::json bad_registry = {
    {"version", 1},
    {"sources", nlohmann::json::array({
      {{"index", 2}, {"source_id", "a"}, {"platform", "p1"}, {"sensor", "s"},
       {"sensor_class", ""}, {"campaign", ""}, {"datum", ""}},
      {{"index", 1}, {"source_id", "b"}, {"platform", "p2"}, {"sensor", "s"},
       {"sensor_class", ""}, {"campaign", ""}, {"datum", ""}},
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
       {"sensor_class", ""}, {"campaign", ""}, {"datum", ""}},
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
       {"sensor_class", ""}, {"campaign", ""}, {"datum", ""}},
      {{"index", 2}, {"source_id", "dup"}, {"platform", "p2"}, {"sensor", "s"},
       {"sensor_class", ""}, {"campaign", ""}, {"datum", ""}},
    })}
  };
  {
    std::ofstream out(dir_ / "registry.json");
    out << bad_registry.dump(2);
  }
  SourceRegistry reg;
  EXPECT_THROW(reg.loadRegistry(dir_.string()), std::runtime_error);
}

// --- Item 2: tile_io.cpp loadTile — legacy 3-band guard ---

TEST_F(TileIoTest, LoadTileRejectsLegacyThreeBandValueTile)
{
  // A value tile with exactly 3 bands (the pre-#178 depth/uncertainty/Float64-
  // seconds layout) must be rejected with a clear error on load rather than
  // silently loading 2 bands and discarding the third.
  namespace mtrs = marine_tiled_raster_store;
  fs::create_directories(dir_ / "draft");

  // Create a fake 3-band Float64 tile that mimics the old layout.
  const gggs::Level level(5);
  const gggs::GridIndex grid = level.gridIndex(43.0, -70.5);
  const std::string filename = marine_bathymetry_store::tileFilename(grid);
  const std::string path = (dir_ / "draft" / filename).string();

  mtrs::TiledRasterTile<double> legacy_tile(grid, 3, 0.0);
  legacy_tile.set(10, 20, 0, -12.5);    // depth
  legacy_tile.set(10, 20, 1, 0.3);      // uncertainty
  legacy_tile.set(10, 20, 2, 1.78e9);   // old Float64 seconds timestamp
  mtrs::saveTile<double>(legacy_tile, path, {std::nullopt, std::nullopt, std::nullopt});

  // loadTile must throw, not silently drop band 3.
  BathymetryStore store(5);
  EXPECT_THROW(marine_bathymetry_store::load(store, dir_.string()), std::runtime_error);
}

// --- Item 4: tile_io.cpp loadTile — GridIndex consistency ---

TEST_F(TileIoTest, LoadTileRejectsCompanionWithWrongGrid)
{
  // If a _time.tif companion is manually replaced with a file from a different
  // grid (simulating file tampering or mis-rename), loadTile must throw a clear
  // error rather than silently combining cell-for-cell data from two different
  // geographic locations.
  namespace mtrs = marine_tiled_raster_store;
  fs::create_directories(dir_ / "draft");

  const gggs::Level level(5);
  // Two grids in different locations.
  const gggs::GridIndex grid_a = level.gridIndex(43.0, -70.5);
  const gggs::GridIndex grid_b = level.gridIndex(44.0, -71.0);
  ASSERT_FALSE(grid_a == grid_b);

  // Save a normal tile for grid_a.
  BathymetryStore store(5);
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5),
    BathyCell{-30.0, 0.5, 42LL, 1u});
  marine_bathymetry_store::save(store, dir_.string());

  // Replace grid_a's _time companion with a time tile written for grid_b.
  // The companion filename is derived from the value tile stem (grid_a filename
  // + "_time.tif"), but we write a file whose geotransform encodes grid_b.
  const std::string value_filename = marine_bathymetry_store::tileFilename(grid_a);
  const fs::path time_path =
    dir_ / "draft" / (fs::path(value_filename).stem().string() + "_time.tif");
  // Write a time tile for grid_b at that path (overwrites any existing companion).
  mtrs::TiledRasterTile<int64_t> wrong_time(grid_b, 1, int64_t{0});
  wrong_time.set(0, 0, 0, 99LL);
  mtrs::saveTile<int64_t>(wrong_time, time_path.string(), {std::nullopt});

  BathymetryStore reloaded(5);
  EXPECT_THROW(marine_bathymetry_store::load(reloaded, dir_.string()), std::runtime_error);
}

// --- Flat layout persistence (#221: per-day epoch subdirectory dropped) ---

TEST_F(TileIoTest, SaveWritesFlatLayoutNoEpochSubdir)
{
  // Value tiles persist directly under <dir>/<layer>/ — no per-day epoch
  // subdirectory and no provenance marker file (#221).
  BathymetryStore writer(5);
  const auto cell = writer.cellIndex(43.0, -70.5);
  writer.set(SourceLayer::Draft, cell, BathyCell{-30.0, 0.5, 1LL});
  EXPECT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 1u);

  // The value tile sits directly in draft/, and no subdirectory or provenance
  // marker exists.
  const std::string filename =
    marine_bathymetry_store::tileFilename(cell.grid());
  EXPECT_TRUE(fs::is_regular_file(dir_ / "draft" / filename));
  EXPECT_FALSE(fs::exists(dir_ / "draft" / "provenance"));
  for (const auto & e : fs::directory_iterator(dir_ / "draft")) {
    EXPECT_FALSE(e.is_directory()) << "no epoch subdirectory expected: " << e.path();
  }

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 1u);
  EXPECT_DOUBLE_EQ(reloaded.get(SourceLayer::Draft, cell)->depth, -30.0);
}

TEST_F(TileIoTest, LoadIgnoresEpochSubdirectories)
{
  // A stray subdirectory under <layer>/ (e.g. a leftover old-style epoch dir)
  // is ignored by load, not flattened — there is no production data to migrate
  // (#221). Only the flat value tiles load.
  BathymetryStore writer(5);
  const auto cell = writer.cellIndex(43.0, -70.5);
  writer.set(SourceLayer::Draft, cell, BathyCell{-30.0, 0.5, 1LL});
  marine_bathymetry_store::save(writer, dir_.string());

  // Drop an old-style epoch subdirectory containing a (would-be) tile file.
  const fs::path epoch_dir = dir_ / "draft" / "2026-06-10";
  fs::create_directories(epoch_dir);
  {
    std::ofstream(epoch_dir / "5_0_0.tif") << "stale";
  }

  BathymetryStore reloaded(5);
  // Only the flat tile loads; the subdirectory's contents are ignored (not
  // mis-loaded as a tile, which would have thrown on the bogus file).
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 1u);
  EXPECT_DOUBLE_EQ(reloaded.get(SourceLayer::Draft, cell)->depth, -30.0);
}

TEST_F(TileIoTest, ImportTilesPersistAndReload)
{
  // The bulk-insert path persists its tiles to the flat layout and reloads.
  BathymetryStore writer(5);
  const auto cell_a = writer.cellIndex(43.0, -70.5);
  const auto cell_b = writer.cellIndex(44.0, -71.0);
  ASSERT_FALSE(cell_a.grid() == cell_b.grid());

  std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
  for (const auto & c : {cell_a, cell_b}) {
    marine_bathymetry_store::BathymetryTile t(c.grid());
    t.set(c.row(), c.column(), BathyCell{-30.0, 0.5, 1LL});
    tiles.emplace(c.grid(), std::move(t));
  }
  EXPECT_EQ(writer.importTiles(SourceLayer::Draft, std::move(tiles)), 2u);
  EXPECT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 2u);

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 2u);
  EXPECT_TRUE(reloaded.get(SourceLayer::Draft, cell_a).has_value());
  EXPECT_TRUE(reloaded.get(SourceLayer::Draft, cell_b).has_value());
}

TEST_F(TileIoTest, LevelFromTileFilenameParsesPrefix)
{
  EXPECT_EQ(marine_bathymetry_store::levelFromTileFilename("7_12_34.tif"), 7u);
  EXPECT_EQ(marine_bathymetry_store::levelFromTileFilename("12_0_0.tif"), 12u);
  EXPECT_THROW(marine_bathymetry_store::levelFromTileFilename("foo.tif"), std::runtime_error);
  EXPECT_THROW(marine_bathymetry_store::levelFromTileFilename("99_0_0.tif"), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Helpers for loadWindow / evictOutside tests
// ---------------------------------------------------------------------------

namespace
{
// Make a GeoPoint from lat/lon (altitude 0).
geographic_msgs::msg::GeoPoint makeGeoPoint(double lat, double lon)
{
  geographic_msgs::msg::GeoPoint p;
  p.latitude = lat;
  p.longitude = lon;
  p.altitude = 0.0;
  return p;
}
}  // namespace

// ---------------------------------------------------------------------------
// loadWindow tests
// ---------------------------------------------------------------------------

TEST_F(TileIoTest, LoadWindowLoadsOnlyOverlappingTiles)
{
  // Three tiles at level 5: one inside the box, one outside, one at the edge
  // (boundary straddle).  Write them to separate directories (save merges tiles
  // in the same layer/epoch), then check that loadWindow loads the two that
  // overlap.
  const uint8_t lvl = 5;
  gggs::Level lev(lvl);

  // inside box: 43.0°N, -70.5°W
  const gggs::GridIndex inside_grid = lev.gridIndex(43.0, -70.5);
  // outside box: 50.0°N, -40.0°W (well outside any reasonable test box) —
  // referenced by geoPoint only, GridIndex not needed at this scope.
  // boundary straddle: a tile whose south or west edge touches the box min
  // We will use the exact inside_grid's south edge as the box min, so the grid
  // itself is the straddler — easiest way to guarantee edge-touch.
  // Use a second nearby grid as the boundary case: choose a tile one row south
  // of inside_grid, set the box min to exactly that tile's north edge.
  const gggs::GridIndex straddle_grid = lev.gridIndex(
    inside_grid.southLatitude() - 0.001,  // one small step south into the next tile
    inside_grid.westLongitude() + 0.001);

  // Write all three tiles to disk.
  {
    BathymetryStore writer(lvl);
    const auto c_in = lev.cellIndex(gggs::geoPoint(43.0, -70.5));
    writer.set(SourceLayer::Draft, c_in, BathyCell{-10.0, 0.5, 1LL});
    const auto c_out = lev.cellIndex(gggs::geoPoint(50.0, -40.0));
    writer.set(SourceLayer::Draft, c_out, BathyCell{-20.0, 0.5, 2LL});
    const auto c_str = lev.cellIndex(
      gggs::geoPoint(
        inside_grid.southLatitude() - 0.001,
        inside_grid.westLongitude() + 0.001));
    writer.set(SourceLayer::Draft, c_str, BathyCell{-30.0, 0.5, 3LL});
    ASSERT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 3u);
  }

  // Box: from straddle_grid's north latitude (so the straddle tile's north edge
  // exactly touches min_pt.latitude) to well above inside_grid.
  // Actually simpler: box just covers inside_grid and straddle_grid but not
  // outside_grid.  The straddle_grid is south of inside_grid.
  const auto min_pt = makeGeoPoint(straddle_grid.southLatitude(), -72.0);
  const auto max_pt = makeGeoPoint(inside_grid.northLatitude(), -70.0);

  BathymetryStore result(lvl);
  const std::size_t n = marine_bathymetry_store::loadWindow(
    result, dir_.string(), min_pt, max_pt);

  // Both inside_grid and straddle_grid overlap the box; outside_grid does not —
  // so exactly two tiles load (proves "only overlapping", not just "at least one").
  EXPECT_EQ(n, 2u);
  // outside_grid must not have been loaded.
  EXPECT_FALSE(result.get(SourceLayer::Draft, lev.cellIndex(
    gggs::geoPoint(50.0, -40.0))).has_value());
}

TEST_F(TileIoTest, LoadWindowBoundaryStraddle)
{
  // A tile whose south edge exactly equals max_pt.latitude must be included
  // (inclusive boundary semantics).
  const uint8_t lvl = 5;
  gggs::Level lev(lvl);
  const gggs::GridIndex grid = lev.gridIndex(43.0, -70.5);

  {
    BathymetryStore writer(lvl);
    const auto cell = lev.cellIndex(gggs::geoPoint(43.0, -70.5));
    writer.set(SourceLayer::Draft, cell, BathyCell{-15.0, 0.5, 1LL});
    ASSERT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 1u);
  }

  // Box whose north edge is exactly the tile's south edge.
  const auto min_pt = makeGeoPoint(grid.southLatitude() - 1.0, grid.westLongitude());
  const auto max_pt = makeGeoPoint(grid.southLatitude(), grid.eastLongitude());

  BathymetryStore result(lvl);
  const std::size_t n = marine_bathymetry_store::loadWindow(
    result, dir_.string(), min_pt, max_pt);
  EXPECT_EQ(n, 1u);
  EXPECT_TRUE(result.get(SourceLayer::Draft,
    lev.cellIndex(gggs::geoPoint(43.0, -70.5))).has_value());
}

TEST_F(TileIoTest, LoadWindowIdempotent)
{
  // Calling loadWindow twice on the same box must not double-load tiles.
  const uint8_t lvl = 5;
  gggs::Level lev(lvl);

  {
    BathymetryStore writer(lvl);
    const auto cell = lev.cellIndex(gggs::geoPoint(43.0, -70.5));
    writer.set(SourceLayer::Draft, cell, BathyCell{-10.0, 0.5, 1LL});
    ASSERT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 1u);
  }

  const gggs::GridIndex grid = lev.gridIndex(43.0, -70.5);
  const auto min_pt = makeGeoPoint(grid.southLatitude(), grid.westLongitude());
  const auto max_pt = makeGeoPoint(grid.northLatitude(), grid.eastLongitude());

  BathymetryStore result(lvl);
  const std::size_t first = marine_bathymetry_store::loadWindow(
    result, dir_.string(), min_pt, max_pt);
  EXPECT_EQ(first, 1u);
  const std::size_t second = marine_bathymetry_store::loadWindow(
    result, dir_.string(), min_pt, max_pt);
  // Second call should skip the already-resident tile.
  EXPECT_EQ(second, 0u);
  // Tile count in store is still 1 (not doubled).
  EXPECT_EQ(result.tiles(SourceLayer::Draft).size(), 1u);
}

TEST_F(TileIoTest, LoadWindowRejectsMalformedTileFilename)
{
  // A stray tile whose level prefix is out of range (99 > max level 20) must
  // throw a clean std::runtime_error, NOT index the fixed-size gggs::levels[]
  // table out of bounds (UB).  Guards the filename parser reached before GDAL.
  const uint8_t lvl = 5;
  gggs::Level lev(lvl);
  {
    BathymetryStore writer(lvl);
    writer.set(
      SourceLayer::Draft, lev.cellIndex(gggs::geoPoint(43.0, -70.5)),
      BathyCell{-10.0, 0.5, 1LL});
    ASSERT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 1u);
  }
  // Drop a bogus value-tile name (level 99) into the same layer/epoch dir.
  const std::filesystem::path bogus = dir_ / "draft" / "99_0_0.tif";
  {
    std::ofstream(bogus) << "not a tile";
  }

  BathymetryStore result(lvl);
  const auto min_pt = makeGeoPoint(-90.0, -180.0);
  const auto max_pt = makeGeoPoint(90.0, 180.0);
  EXPECT_THROW(
    marine_bathymetry_store::loadWindow(result, dir_.string(), min_pt, max_pt),
    std::runtime_error);
}

// ---------------------------------------------------------------------------
// evictOutside tests
// ---------------------------------------------------------------------------

TEST_F(TileIoTest, EvictOutsideDropsOutsideKeepsInside)
{
  // Two tiles: one inside the keep-box, one outside.
  // Populate in-memory via importTiles (no disk I/O needed for evictOutside).
  const uint8_t lvl = 5;
  gggs::Level lev(lvl);
  const auto cell_in = lev.cellIndex(gggs::geoPoint(43.0, -70.5));
  const auto cell_out = lev.cellIndex(gggs::geoPoint(50.0, -40.0));
  ASSERT_FALSE(cell_in.grid() == cell_out.grid());

  {
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    marine_bathymetry_store::BathymetryTile t_in(cell_in.grid());
    t_in.set(cell_in.row(), cell_in.column(), BathyCell{-10.0, 0.5, 1LL});
    tiles.emplace(cell_in.grid(), std::move(t_in));
    marine_bathymetry_store::BathymetryTile t_out(cell_out.grid());
    t_out.set(cell_out.row(), cell_out.column(), BathyCell{-20.0, 0.5, 2LL});
    tiles.emplace(cell_out.grid(), std::move(t_out));

    BathymetryStore store(lvl);
    ASSERT_EQ(store.importTiles(SourceLayer::Draft, std::move(tiles)), 2u);
    // Save so tiles are clean (importTiles marks them dirty; we want to test
    // eviction of clean tiles here — the dirty-guard test is separate).
    marine_bathymetry_store::save(store, dir_.string());
    ASSERT_TRUE(store.get(SourceLayer::Draft, cell_in).has_value());
    ASSERT_TRUE(store.get(SourceLayer::Draft, cell_out).has_value());

    const gggs::GridIndex grid_in = cell_in.grid();
    const auto min_pt =
      makeGeoPoint(grid_in.southLatitude(), grid_in.westLongitude());
    const auto max_pt =
      makeGeoPoint(grid_in.northLatitude(), grid_in.eastLongitude());

    const std::size_t evicted = marine_bathymetry_store::evictOutside(
      store, min_pt, max_pt);

    EXPECT_EQ(evicted, 1u);
    EXPECT_TRUE(store.get(SourceLayer::Draft, cell_in).has_value());
    EXPECT_FALSE(store.get(SourceLayer::Draft, cell_out).has_value());
  }
}

TEST_F(TileIoTest, EvictOutsideDirtyTileNotEvicted)
{
  // A dirty (unsaved) Draft tile outside the keep-box must NOT be evicted.
  // importTiles marks tiles dirty, so we skip save() intentionally.
  const uint8_t lvl = 5;
  gggs::Level lev(lvl);
  const auto cell_out = lev.cellIndex(gggs::geoPoint(50.0, -40.0));

  std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
  marine_bathymetry_store::BathymetryTile t(cell_out.grid());
  t.set(cell_out.row(), cell_out.column(), BathyCell{-20.0, 0.5, 1LL});
  tiles.emplace(cell_out.grid(), std::move(t));

  BathymetryStore store(lvl);
  ASSERT_EQ(store.importTiles(SourceLayer::Draft, std::move(tiles)), 1u);
  // Tile is dirty (importTiles marks it dirty); do NOT save here.
  ASSERT_TRUE(store.get(SourceLayer::Draft, cell_out)->depth == -20.0);

  // Keep-box is far from the tile's position.
  const auto min_pt = makeGeoPoint(42.0, -71.0);
  const auto max_pt = makeGeoPoint(44.0, -70.0);

  const std::size_t evicted = marine_bathymetry_store::evictOutside(
    store, min_pt, max_pt);

  // Dirty-tile guard: evicted count must be 0 (tile was outside but dirty).
  EXPECT_EQ(evicted, 0u);
  // Tile must still be present.
  EXPECT_TRUE(store.get(SourceLayer::Draft, cell_out).has_value());
}
