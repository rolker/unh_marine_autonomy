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
#include "marine_bathymetry_store/epoch.hpp"
#include "marine_bathymetry_store/registry.hpp"
#include "marine_bathymetry_store/tile_io.hpp"
#include "marine_tiled_raster_store/tile_io.hpp"

using marine_bathymetry_store::BathyCell;
using marine_bathymetry_store::BathymetryStore;
using marine_bathymetry_store::Provenance;
using marine_bathymetry_store::SourceLayer;
using marine_bathymetry_store::SourceRecord;
using marine_bathymetry_store::SourceRegistry;

namespace fs = std::filesystem;

// A single ISO-date epoch label used by the persistence round-trip tests. The
// on-disk layout is `<dir>/<layer>/<epoch>/<level>_<row>_<col>.tif`.
static const char * const kEpoch = "2026-06-10";

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
    SourceLayer::Draft, kEpoch, store.cellIndex(43.0, -70.5),
    BathyCell{-30.123, 0.456, ts_draft, 7u});
  store.set(
    SourceLayer::Processed, kEpoch, store.cellIndex(44.0, -71.0),
    BathyCell{-12.5, 0.2, ts_proc, 0u});

  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 2u);

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 2u);

  const auto draft = reloaded.get(SourceLayer::Draft, kEpoch, reloaded.cellIndex(43.0, -70.5));
  ASSERT_TRUE(draft.has_value());
  EXPECT_DOUBLE_EQ(draft->depth, -30.123);
  EXPECT_DOUBLE_EQ(draft->uncertainty, 0.456);
  // Int64 ns timestamp band: nanosecond stamps survive exactly.
  EXPECT_EQ(draft->timestamp, ts_draft);
  EXPECT_EQ(draft->source_index, 7u);

  const auto pcell = reloaded.cellIndex(44.0, -71.0);
  const auto processed = reloaded.get(SourceLayer::Processed, kEpoch, pcell);
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
    SourceLayer::Draft, kEpoch, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 10LL, 1u});
  store.set(
    SourceLayer::Draft, kEpoch, store.cellIndex(43.0, -70.4), BathyCell{-31.0, 0.5, 11LL, 4242u});
  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 1u);

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 1u);
  EXPECT_EQ(
    reloaded.get(SourceLayer::Draft, kEpoch, reloaded.cellIndex(43.0, -70.5))->source_index, 1u);
  EXPECT_EQ(
    reloaded.get(SourceLayer::Draft, kEpoch, reloaded.cellIndex(43.0, -70.4))->source_index, 4242u);
}

TEST_F(TileIoTest, MissingTimeTileLoadsAsZero)
{
  // Pre-migration single-platform data has no _time.tif. Deleting it before load
  // must not fail — the timestamp band fills with 0 (backward compatibility).
  BathymetryStore store(5);
  store.set(
    SourceLayer::Draft, kEpoch, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 99LL, 2u});
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
  const auto got = reloaded.get(SourceLayer::Draft, kEpoch, reloaded.cellIndex(43.0, -70.5));
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, -30.0);   // value tile still loaded
  EXPECT_EQ(got->timestamp, 0);          // missing time tile -> 0
  EXPECT_EQ(got->source_index, 2u);      // source tile still loaded
}

TEST_F(TileIoTest, MissingSourceTileLoadsAsZero)
{
  BathymetryStore store(5);
  store.set(
    SourceLayer::Draft, kEpoch, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 99LL, 5u});
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
  const auto got = reloaded.get(SourceLayer::Draft, kEpoch, reloaded.cellIndex(43.0, -70.5));
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->timestamp, 99);         // time tile still loaded
  EXPECT_EQ(got->source_index, 0u);      // missing source tile -> 0
}

TEST_F(TileIoTest, LoadedTilesAreCleanAndDontResave)
{
  BathymetryStore store(5);
  store.set(SourceLayer::Draft, kEpoch, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 1LL});

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
  store.set(SourceLayer::Draft, kEpoch, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 1LL});
  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 1u);

  store.set(SourceLayer::Draft, kEpoch, store.cellIndex(43.0, -70.5), BathyCell{-31.0, 0.4, 2LL});
  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 1u);
}

TEST_F(TileIoTest, LayersWriteToSeparateSubdirectories)
{
  BathymetryStore store(5);
  const auto pcell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Processed, kEpoch, pcell, BathyCell{-10.0, 0.1, 1LL});
  store.set(SourceLayer::Draft, kEpoch, pcell, BathyCell{-12.0, 0.5, 2LL});
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
  writer.set(SourceLayer::Draft, kEpoch, coarse_cell, BathyCell{-30.0, 0.5, 1LL});
  writer.set(SourceLayer::Draft, kEpoch, fine_cell, BathyCell{-22.0, 0.3, 2LL});
  EXPECT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 2u);

  // Reload into a store whose default level (6) matches NEITHER stored level.
  BathymetryStore reloaded(6);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 2u);
  const auto coarse_got = reloaded.get(SourceLayer::Draft, kEpoch, coarse_cell);
  ASSERT_TRUE(coarse_got.has_value());
  EXPECT_DOUBLE_EQ(coarse_got->depth, -30.0);
  const auto fine_got = reloaded.get(SourceLayer::Draft, kEpoch, fine_cell);
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
  writer.set(SourceLayer::Chart, kEpoch, writer.cellIndex(43.0, -70.5), BathyCell{38.58, 3.0, ts});
  EXPECT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 1u);
  EXPECT_TRUE(fs::is_directory(dir_ / "chart"));

  BathymetryStore runtime(5);   // Chart NOT writable
  EXPECT_FALSE(runtime.chartWritable());
  EXPECT_EQ(marine_bathymetry_store::load(runtime, dir_.string()), 1u);

  const auto rcell = runtime.cellIndex(43.0, -70.5);
  const auto got = runtime.get(SourceLayer::Chart, kEpoch, rcell);
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, 38.58);
  EXPECT_EQ(got->timestamp, ts);

  // The read-only guard still holds after the prior is loaded.
  EXPECT_THROW(
    runtime.set(SourceLayer::Chart, kEpoch, rcell, BathyCell{1.0, 1.0, 1LL}),
    std::logic_error);
}

TEST_F(TileIoTest, ProcessedDraftOnlyStoreLoadsWithoutChartDir)
{
  // Back-compat: a Phase-1 store with no chart/ subdir still saves and loads;
  // the absent chart layer is simply skipped, not an error.
  BathymetryStore store(5);
  store.set(SourceLayer::Draft, kEpoch, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 1LL});
  marine_bathymetry_store::save(store, dir_.string());
  EXPECT_FALSE(fs::exists(dir_ / "chart"));   // no spurious empty chart/ dir

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 1u);
  EXPECT_TRUE(reloaded.epochs(SourceLayer::Chart).empty());
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
  store.set(SourceLayer::Draft, kEpoch, store.cellIndex(43.0, -70.5),
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

// --- Item 2: tile_io.cpp loadTile — legacy 3-band guard ---

TEST_F(TileIoTest, LoadTileRejectsLegacyThreeBandValueTile)
{
  // A value tile with exactly 3 bands (the pre-#178 depth/uncertainty/Float64-
  // seconds layout) must be rejected with a clear error on load rather than
  // silently loading 2 bands and discarding the third.
  namespace mtrs = marine_tiled_raster_store;
  fs::create_directories(dir_ / "draft" / kEpoch);

  // Create a fake 3-band Float64 tile that mimics the old layout.
  const gggs::Level level(5);
  const gggs::GridIndex grid = level.gridIndex(43.0, -70.5);
  const std::string filename = marine_bathymetry_store::tileFilename(grid);
  const std::string path = (dir_ / "draft" / kEpoch / filename).string();

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
  fs::create_directories(dir_ / "draft" / kEpoch);

  const gggs::Level level(5);
  // Two grids in different locations.
  const gggs::GridIndex grid_a = level.gridIndex(43.0, -70.5);
  const gggs::GridIndex grid_b = level.gridIndex(44.0, -71.0);
  ASSERT_FALSE(grid_a == grid_b);

  // Save a normal tile for grid_a.
  BathymetryStore store(5);
  store.set(SourceLayer::Draft, kEpoch, store.cellIndex(43.0, -70.5),
    BathyCell{-30.0, 0.5, 42LL, 1u});
  marine_bathymetry_store::save(store, dir_.string());

  // Replace grid_a's _time companion with a time tile written for grid_b.
  // The companion filename is derived from the value tile stem (grid_a filename
  // + "_time.tif"), but we write a file whose geotransform encodes grid_b.
  const std::string value_filename = marine_bathymetry_store::tileFilename(grid_a);
  const fs::path time_path =
    dir_ / "draft" / kEpoch / (fs::path(value_filename).stem().string() + "_time.tif");
  // Write a time tile for grid_b at that path (overwrites any existing companion).
  mtrs::TiledRasterTile<int64_t> wrong_time(grid_b, 1, int64_t{0});
  wrong_time.set(0, 0, 0, 99LL);
  mtrs::saveTile<int64_t>(wrong_time, time_path.string(), {std::nullopt});

  BathymetryStore reloaded(5);
  EXPECT_THROW(marine_bathymetry_store::load(reloaded, dir_.string()), std::runtime_error);
}

// --- Epoch persistence (ADR-0002 Amendment A1) ---

TEST_F(TileIoTest, EpochProvenanceRoundTrips)
{
  // A Replayed epoch's provenance must survive save/load via the marker file,
  // so a reloaded compacted epoch stays immutable to live writes.
  BathymetryStore writer(5);
  const auto cell = writer.cellIndex(43.0, -70.5);
  std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
  marine_bathymetry_store::BathymetryTile tile(cell.grid());
  tile.set(cell.row(), cell.column(), BathyCell{-30.0, 0.5, 1LL});
  tiles.emplace(cell.grid(), std::move(tile));
  ASSERT_TRUE(
    writer.importEpoch(SourceLayer::Draft, kEpoch, std::move(tiles), Provenance::Replayed));
  marine_bathymetry_store::save(writer, dir_.string());

  // The provenance marker exists in the epoch dir.
  EXPECT_TRUE(fs::is_regular_file(dir_ / "draft" / kEpoch / "provenance"));

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 1u);
  // The reloaded epoch is Replayed -> a live write is refused (no-op).
  EXPECT_FALSE(reloaded.set(SourceLayer::Draft, kEpoch, cell, BathyCell{-99.0, 9.0, 2LL}));
  EXPECT_DOUBLE_EQ(reloaded.get(SourceLayer::Draft, kEpoch, cell)->depth, -30.0);
}

TEST_F(TileIoTest, ReadProvenanceMarkerIsCrlfSafe)
{
  // A CRLF-terminated provenance marker (Windows / mixed checkout) must not be
  // mis-parsed as garbage and silently downgraded to LiveFused — that would let
  // live writes regress a Replayed epoch after a reload (harvested #148 fix).
  BathymetryStore writer(5);
  const auto cell = writer.cellIndex(43.0, -70.5);
  std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
  marine_bathymetry_store::BathymetryTile tile(cell.grid());
  tile.set(cell.row(), cell.column(), BathyCell{-30.0, 0.5, 1LL});
  tiles.emplace(cell.grid(), std::move(tile));
  writer.importEpoch(SourceLayer::Draft, kEpoch, std::move(tiles), Provenance::Replayed);
  marine_bathymetry_store::save(writer, dir_.string());

  // Rewrite the provenance marker with a CRLF line ending.
  {
    std::ofstream out(dir_ / "draft" / kEpoch / "provenance", std::ios::trunc | std::ios::binary);
    out << "replayed\r\n";
  }

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 1u);
  // Still recognized as Replayed despite the CRLF -> live write refused.
  EXPECT_FALSE(reloaded.set(SourceLayer::Draft, kEpoch, cell, BathyCell{-99.0, 9.0, 2LL}));
}

TEST_F(TileIoTest, SupersedesDiskClearsStaleTiles)
{
  // A wholesale re-import (importEpoch) that covers fewer grids must remove the
  // stale tile files of the grids it no longer covers, so they aren't
  // resurrected on the next load (supersedes_disk).
  BathymetryStore writer(5);
  const auto cell_a = writer.cellIndex(43.0, -70.5);
  const auto cell_b = writer.cellIndex(44.0, -71.0);
  ASSERT_FALSE(cell_a.grid() == cell_b.grid());

  // First import covers two grids.
  std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> first;
  for (const auto & c : {cell_a, cell_b}) {
    marine_bathymetry_store::BathymetryTile t(c.grid());
    t.set(c.row(), c.column(), BathyCell{-30.0, 0.5, 1LL});
    first.emplace(c.grid(), std::move(t));
  }
  writer.importEpoch(SourceLayer::Draft, kEpoch, std::move(first), Provenance::Replayed);
  EXPECT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 2u);

  // Re-import the SAME epoch covering only grid_a.
  std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> second;
  {
    marine_bathymetry_store::BathymetryTile t(cell_a.grid());
    t.set(cell_a.row(), cell_a.column(), BathyCell{-28.0, 0.4, 2LL});
    second.emplace(cell_a.grid(), std::move(t));
  }
  writer.importEpoch(SourceLayer::Draft, kEpoch, std::move(second), Provenance::Replayed);
  marine_bathymetry_store::save(writer, dir_.string());

  // grid_b's tile files must be gone; a fresh load sees only grid_a.
  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 1u);
  EXPECT_TRUE(reloaded.get(SourceLayer::Draft, kEpoch, cell_a).has_value());
  EXPECT_FALSE(reloaded.get(SourceLayer::Draft, kEpoch, cell_b).has_value());
}

TEST_F(TileIoTest, EpochsPersistToSeparateDirectories)
{
  // Two epochs of one layer save to distinct subdirectories and both reload.
  BathymetryStore writer(5);
  const auto cell = writer.cellIndex(43.0, -70.5);
  writer.set(SourceLayer::Draft, "2026-06-09", cell, BathyCell{-30.0, 0.5, 1LL});
  writer.set(SourceLayer::Draft, "2026-06-11", cell, BathyCell{-28.0, 0.5, 2LL});
  EXPECT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 2u);

  EXPECT_TRUE(fs::is_directory(dir_ / "draft" / "2026-06-09"));
  EXPECT_TRUE(fs::is_directory(dir_ / "draft" / "2026-06-11"));

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 2u);
  EXPECT_EQ(reloaded.epochs(SourceLayer::Draft).size(), 2u);
  EXPECT_DOUBLE_EQ(reloaded.get(SourceLayer::Draft, "2026-06-09", cell)->depth, -30.0);
  EXPECT_DOUBLE_EQ(reloaded.get(SourceLayer::Draft, "2026-06-11", cell)->depth, -28.0);
}

TEST_F(TileIoTest, LevelFromTileFilenameParsesPrefix)
{
  EXPECT_EQ(marine_bathymetry_store::levelFromTileFilename("7_12_34.tif"), 7u);
  EXPECT_EQ(marine_bathymetry_store::levelFromTileFilename("12_0_0.tif"), 12u);
  EXPECT_THROW(marine_bathymetry_store::levelFromTileFilename("foo.tif"), std::runtime_error);
  EXPECT_THROW(marine_bathymetry_store::levelFromTileFilename("99_0_0.tif"), std::runtime_error);
}
