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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"
#include "marine_bathymetry_store/registry.hpp"
#include "marine_bathymetry_store/tile_io.hpp"

using marine_bathymetry_store::BathyCell;
using marine_bathymetry_store::BathymetryStore;
using marine_bathymetry_store::SourceLayer;
using marine_bathymetry_store::StoreMetadata;

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
  BathymetryStore store(5, /*reference_writable=*/true);
  // A non-trivial uncertainty checks lossless Float64 round-trip of the value tile.
  store.set(SourceLayer::Survey, store.cellIndex(43.0, -70.5), BathyCell{-30.123, 0.456789});
  store.set(
    SourceLayer::Reference, store.cellIndex(44.0, -71.0), BathyCell{-12.5, 0.2});

  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 2u);

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 2u);

  const auto survey = reloaded.get(SourceLayer::Survey, reloaded.cellIndex(43.0, -70.5));
  ASSERT_TRUE(survey.has_value());
  EXPECT_DOUBLE_EQ(survey->depth, -30.123);
  EXPECT_DOUBLE_EQ(survey->uncertainty, 0.456789);   // uncertainty round-trips exactly

  const auto pre = reloaded.get(SourceLayer::Reference, reloaded.cellIndex(44.0, -71.0));
  ASSERT_TRUE(pre.has_value());
  EXPECT_DOUBLE_EQ(pre->depth, -12.5);
  EXPECT_DOUBLE_EQ(pre->uncertainty, 0.2);
}

TEST_F(TileIoTest, LoadedTilesAreCleanAndDontResave)
{
  BathymetryStore store(5);
  store.set(SourceLayer::Survey, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5});

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
  store.set(SourceLayer::Survey, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5});
  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 1u);

  store.set(SourceLayer::Survey, store.cellIndex(43.0, -70.5), BathyCell{-31.0, 0.4});
  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 1u);
}

TEST_F(TileIoTest, LayersWriteToSeparateSubdirectories)
{
  BathymetryStore store(5, /*reference_writable=*/true);
  const auto pcell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Reference, pcell, BathyCell{-10.0, 0.1});
  store.set(SourceLayer::Survey, pcell, BathyCell{-12.0, 0.5});
  marine_bathymetry_store::save(store, dir_.string());

  EXPECT_TRUE(fs::is_directory(dir_ / "reference"));
  EXPECT_TRUE(fs::is_directory(dir_ / "survey"));
}

TEST_F(TileIoTest, LoadAcceptsMixedLevelTiles)
{
  // The store is multi-level (ADR-0002 §D2): tiles at different levels share a
  // layer directory, and load recovers each tile's level from its filename. A
  // store loaded with a DIFFERENT default level still reads them all back.
  BathymetryStore writer(5);
  gggs::Level coarse(4);
  gggs::Level fine(7);
  const auto coarse_cell = coarse.cellIndex(gggs::geoPoint(43.0, -70.5));
  const auto fine_cell = fine.cellIndex(gggs::geoPoint(43.0, -70.5));
  writer.set(SourceLayer::Survey, coarse_cell, BathyCell{-30.0, 0.5});
  writer.set(SourceLayer::Survey, fine_cell, BathyCell{-22.0, 0.3});
  EXPECT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 2u);

  // Reload into a store whose default level (6) matches NEITHER stored level.
  BathymetryStore reloaded(6);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 2u);
  const auto coarse_got = reloaded.get(SourceLayer::Survey, coarse_cell);
  ASSERT_TRUE(coarse_got.has_value());
  EXPECT_DOUBLE_EQ(coarse_got->depth, -30.0);
  const auto fine_got = reloaded.get(SourceLayer::Survey, fine_cell);
  ASSERT_TRUE(fine_got.has_value());
  EXPECT_DOUBLE_EQ(fine_got->depth, -22.0);
}

TEST_F(TileIoTest, ReferenceRoundTripsAndLoadsIntoReadOnlyStore)
{
  // The importer writes Reference (reference_writable); the runtime loads it
  // into a default (read-only-prior) store. load() populates via getOrCreateTile,
  // not set(), so the prior loads even though live set(Reference) stays
  // forbidden.
  BathymetryStore writer(5, /*reference_writable=*/true);
  writer.set(SourceLayer::Reference, writer.cellIndex(43.0, -70.5), BathyCell{38.58, 3.0});
  EXPECT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 1u);
  EXPECT_TRUE(fs::is_directory(dir_ / "reference"));

  BathymetryStore runtime(5);   // Reference NOT writable
  EXPECT_FALSE(runtime.referenceWritable());
  EXPECT_EQ(marine_bathymetry_store::load(runtime, dir_.string()), 1u);

  const auto rcell = runtime.cellIndex(43.0, -70.5);
  const auto got = runtime.get(SourceLayer::Reference, rcell);
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, 38.58);

  // The read-only guard still holds after the prior is loaded.
  EXPECT_THROW(
    runtime.set(SourceLayer::Reference, rcell, BathyCell{1.0, 1.0}),
    std::logic_error);
}

TEST_F(TileIoTest, SurveyOnlyStoreLoadsWithoutReferenceDir)
{
  // A store with no reference/ subdir still saves and loads; the absent prior
  // layer is simply skipped, not an error.
  BathymetryStore store(5);
  store.set(SourceLayer::Survey, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5});
  marine_bathymetry_store::save(store, dir_.string());
  EXPECT_FALSE(fs::exists(dir_ / "reference"));   // no spurious empty dir

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 1u);
  EXPECT_TRUE(reloaded.tiles(SourceLayer::Reference).empty());
}

// --- Coarse StoreMetadata (registry.json) round-trip (#248) ---

TEST_F(TileIoTest, StoreMetadataAtomicRoundTrip)
{
  // StoreMetadata is written atomically (no leftover .tmp) and reloads exactly.
  fs::create_directories(dir_);
  StoreMetadata md{"bizzy", "m3", "massabesic-2026", "2026-06-30"};
  md.save(dir_.string());
  EXPECT_TRUE(fs::is_regular_file(dir_ / "registry.json"));
  EXPECT_FALSE(fs::exists(dir_ / "registry.json.tmp"));   // no leftover scratch

  StoreMetadata loaded;
  loaded.load(dir_.string());
  EXPECT_EQ(loaded.platform, "bizzy");
  EXPECT_EQ(loaded.sensor, "m3");
  EXPECT_EQ(loaded.survey, "massabesic-2026");
  EXPECT_EQ(loaded.date, "2026-06-30");
}

TEST_F(TileIoTest, MetadataLoadWarnsOnUnrecognizedSchemaVersion)
{
  // A pre-#248 (or foreign) registry.json whose "version" != the current schema
  // would otherwise load as all-empty without a trace. load() must warn.
  fs::create_directories(dir_);
  {std::ofstream(dir_ / "registry.json") << R"({"version": 1, "site": "old"})";}

  StoreMetadata md;
  std::ostringstream captured;
  std::streambuf * prev = std::cerr.rdbuf(captured.rdbuf());
  md.load(dir_.string());
  std::cerr.rdbuf(prev);

  EXPECT_NE(captured.str().find("unrecognized schema version"), std::string::npos)
    << "expected a version-mismatch warning, got: " << captured.str();
  EXPECT_TRUE(md.empty());   // unknown-schema fields do not map — loads empty
}

TEST_F(TileIoTest, MetadataLoadIsSilentOnCurrentSchemaVersion)
{
  // A registry.json written by the current code (version == schema) loads
  // without any warning.
  fs::create_directories(dir_);
  StoreMetadata md{"bizzy", "m3", "massabesic-2026", "2026-06-30"};
  md.save(dir_.string());

  StoreMetadata loaded;
  std::ostringstream captured;
  std::streambuf * prev = std::cerr.rdbuf(captured.rdbuf());
  loaded.load(dir_.string());
  std::cerr.rdbuf(prev);

  EXPECT_TRUE(captured.str().empty()) << "current-schema load must be silent";
  EXPECT_EQ(loaded.survey, "massabesic-2026");
}

TEST_F(TileIoTest, SaveWritesMetadataWhenProvided)
{
  // The store save() persists the metadata sidecar once, at the store root.
  BathymetryStore store(5);
  store.set(SourceLayer::Survey, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5});
  StoreMetadata md{"bizzy", "m3", "massabesic-2026", "2026-06-30"};

  marine_bathymetry_store::save(store, dir_.string(), &md);
  EXPECT_TRUE(fs::is_regular_file(dir_ / "registry.json"));

  StoreMetadata reloaded;
  marine_bathymetry_store::load(store, dir_.string(), &reloaded);
  EXPECT_EQ(reloaded.platform, "bizzy");
  EXPECT_EQ(reloaded.survey, "massabesic-2026");
}

TEST_F(TileIoTest, LoadWithoutMetadataFileLeavesMetadataEmpty)
{
  // A store with no registry.json loads cleanly; StoreMetadata stays empty.
  BathymetryStore store(5);
  store.set(SourceLayer::Survey, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5});
  marine_bathymetry_store::save(store, dir_.string());   // no metadata passed
  EXPECT_FALSE(fs::exists(dir_ / "registry.json"));

  BathymetryStore reloaded(5);
  StoreMetadata md;
  marine_bathymetry_store::load(reloaded, dir_.string(), &md);
  EXPECT_TRUE(md.empty());
}

// --- Flat layout persistence (#221: per-day epoch subdirectory dropped) ---

TEST_F(TileIoTest, SaveWritesFlatLayoutNoEpochSubdir)
{
  // Value tiles persist directly under <dir>/<layer>/ — no per-day epoch
  // subdirectory and no provenance marker file (#221).
  BathymetryStore writer(5);
  const auto cell = writer.cellIndex(43.0, -70.5);
  writer.set(SourceLayer::Survey, cell, BathyCell{-30.0, 0.5});
  EXPECT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 1u);

  // The value tile sits directly in survey/, and no subdirectory or provenance
  // marker exists.
  const std::string filename =
    marine_bathymetry_store::tileFilename(cell.grid());
  EXPECT_TRUE(fs::is_regular_file(dir_ / "survey" / filename));
  EXPECT_FALSE(fs::exists(dir_ / "survey" / "provenance"));
  for (const auto & e : fs::directory_iterator(dir_ / "survey")) {
    EXPECT_FALSE(e.is_directory()) << "no epoch subdirectory expected: " << e.path();
  }

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 1u);
  EXPECT_DOUBLE_EQ(reloaded.get(SourceLayer::Survey, cell)->depth, -30.0);
}

TEST_F(TileIoTest, LoadIgnoresEpochSubdirectories)
{
  // A stray subdirectory under <layer>/ (e.g. a leftover old-style epoch dir)
  // is ignored by load, not flattened — there is no production data to migrate
  // (#221). Only the flat value tiles load.
  BathymetryStore writer(5);
  const auto cell = writer.cellIndex(43.0, -70.5);
  writer.set(SourceLayer::Survey, cell, BathyCell{-30.0, 0.5});
  marine_bathymetry_store::save(writer, dir_.string());

  // Drop an old-style epoch subdirectory containing a (would-be) tile file.
  const fs::path epoch_dir = dir_ / "survey" / "2026-06-10";
  fs::create_directories(epoch_dir);
  {
    std::ofstream(epoch_dir / "5_0_0.tif") << "stale";
  }

  BathymetryStore reloaded(5);
  // Only the flat tile loads; the subdirectory's contents are ignored (not
  // mis-loaded as a tile, which would have thrown on the bogus file).
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 1u);
  EXPECT_DOUBLE_EQ(reloaded.get(SourceLayer::Survey, cell)->depth, -30.0);
}

TEST_F(TileIoTest, LoadWarnsOnUnrecognizedStoreLayout)
{
  // A store dir written in the pre-#248 taxonomy (chart/draft/processed) has no
  // survey/ or reference/ subdir, so load() reaches no tiles and returns empty.
  // Because an empty bathy prior reads as navigable (unsurveyed_is_lethal default
  // false), that silent-empty load must warn — matching the stale-subdir idiom.
  fs::create_directories(dir_ / "chart");
  {std::ofstream(dir_ / "chart" / "5_0_0.tif") << "old-layout tile";}

  BathymetryStore reloaded(5);
  std::ostringstream captured;
  std::streambuf * prev = std::cerr.rdbuf(captured.rdbuf());
  const std::size_t n = marine_bathymetry_store::load(reloaded, dir_.string());
  std::cerr.rdbuf(prev);

  EXPECT_EQ(n, 0u);
  EXPECT_NE(captured.str().find("no recognized"), std::string::npos)
    << "expected an old-layout warning, got: " << captured.str();
}

TEST_F(TileIoTest, LoadDoesNotWarnOnFreshOrMetadataOnlyStore)
{
  // A genuinely fresh store — absent, empty, or holding only the registry.json
  // metadata sidecar with no tiles yet — is NOT an old-layout store, so load()
  // stays silent (no spurious navigation-safety warning).
  auto load_stderr = [this]() {
      BathymetryStore reloaded(5);
      std::ostringstream captured;
      std::streambuf * prev = std::cerr.rdbuf(captured.rdbuf());
      marine_bathymetry_store::load(reloaded, dir_.string());
      std::cerr.rdbuf(prev);
      return captured.str();
    };

  // Absent store dir.
  EXPECT_TRUE(load_stderr().empty());

  // Empty store dir.
  fs::create_directories(dir_);
  EXPECT_TRUE(load_stderr().empty());

  // Metadata-only store (registry.json, no tiles).
  StoreMetadata md;
  md.survey = "massabesic-2026";
  md.save(dir_.string());
  ASSERT_TRUE(fs::is_regular_file(dir_ / "registry.json"));
  EXPECT_TRUE(load_stderr().empty())
    << "metadata-only store must not warn";
}

TEST_F(TileIoTest, LoadSkipsStrayCompanionRasters)
{
  // A dropped pre-#248 companion (`*_time.tif`/`*_source.tif`) may linger inside
  // a new-layout survey/ dir on a store written by old code. It is not a value
  // tile; feeding it to loadTile() would throw and abort the WHOLE load. It must
  // be skipped so the valid value tiles still load.
  BathymetryStore writer(5);
  const auto cell = writer.cellIndex(43.0, -70.5);
  writer.set(SourceLayer::Survey, cell, BathyCell{-30.0, 0.5});
  marine_bathymetry_store::save(writer, dir_.string());

  const std::string tile = marine_bathymetry_store::tileFilename(cell.grid());
  const std::string stem = tile.substr(0, tile.size() - 4);   // drop ".tif"
  {std::ofstream(dir_ / "survey" / (stem + "_source.tif")) << "stale companion";}
  {std::ofstream(dir_ / "survey" / (stem + "_time.tif")) << "stale companion";}

  BathymetryStore reloaded(5);
  std::ostringstream captured;
  std::streambuf * prev = std::cerr.rdbuf(captured.rdbuf());
  const std::size_t n = marine_bathymetry_store::load(reloaded, dir_.string());
  std::cerr.rdbuf(prev);

  EXPECT_EQ(n, 1u) << "the valid value tile must still load past the companions";
  EXPECT_DOUBLE_EQ(reloaded.get(SourceLayer::Survey, cell)->depth, -30.0);
  EXPECT_NE(captured.str().find("companion raster"), std::string::npos);
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
    t.set(c.row(), c.column(), BathyCell{-30.0, 0.5});
    tiles.emplace(c.grid(), std::move(t));
  }
  EXPECT_EQ(writer.importTiles(SourceLayer::Survey, std::move(tiles)), 2u);
  EXPECT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 2u);

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 2u);
  EXPECT_TRUE(reloaded.get(SourceLayer::Survey, cell_a).has_value());
  EXPECT_TRUE(reloaded.get(SourceLayer::Survey, cell_b).has_value());
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
  // (boundary straddle). loadWindow loads only the two that overlap.
  const uint8_t lvl = 5;
  gggs::Level lev(lvl);

  // inside box: 43.0°N, -70.5°W
  const gggs::GridIndex inside_grid = lev.gridIndex(43.0, -70.5);
  // boundary straddle: a tile one row south of inside_grid.
  const gggs::GridIndex straddle_grid = lev.gridIndex(
    inside_grid.southLatitude() - 0.001,
    inside_grid.westLongitude() + 0.001);

  // Write all three tiles to disk.
  {
    BathymetryStore writer(lvl);
    const auto c_in = lev.cellIndex(gggs::geoPoint(43.0, -70.5));
    writer.set(SourceLayer::Survey, c_in, BathyCell{-10.0, 0.5});
    const auto c_out = lev.cellIndex(gggs::geoPoint(50.0, -40.0));
    writer.set(SourceLayer::Survey, c_out, BathyCell{-20.0, 0.5});
    const auto c_str = lev.cellIndex(
      gggs::geoPoint(
        inside_grid.southLatitude() - 0.001,
        inside_grid.westLongitude() + 0.001));
    writer.set(SourceLayer::Survey, c_str, BathyCell{-30.0, 0.5});
    ASSERT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 3u);
  }

  const auto min_pt = makeGeoPoint(straddle_grid.southLatitude(), -72.0);
  const auto max_pt = makeGeoPoint(inside_grid.northLatitude(), -70.0);

  BathymetryStore result(lvl);
  const std::size_t n = marine_bathymetry_store::loadWindow(
    result, dir_.string(), min_pt, max_pt);

  // Both inside_grid and straddle_grid overlap the box; outside_grid does not —
  // so exactly two tiles load (proves "only overlapping", not just "at least one").
  EXPECT_EQ(n, 2u);
  // outside_grid must not have been loaded.
  EXPECT_FALSE(result.get(SourceLayer::Survey, lev.cellIndex(
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
    writer.set(SourceLayer::Survey, cell, BathyCell{-15.0, 0.5});
    ASSERT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 1u);
  }

  // Box whose north edge is exactly the tile's south edge.
  const auto min_pt = makeGeoPoint(grid.southLatitude() - 1.0, grid.westLongitude());
  const auto max_pt = makeGeoPoint(grid.southLatitude(), grid.eastLongitude());

  BathymetryStore result(lvl);
  const std::size_t n = marine_bathymetry_store::loadWindow(
    result, dir_.string(), min_pt, max_pt);
  EXPECT_EQ(n, 1u);
  EXPECT_TRUE(result.get(SourceLayer::Survey,
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
    writer.set(SourceLayer::Survey, cell, BathyCell{-10.0, 0.5});
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
  EXPECT_EQ(result.tiles(SourceLayer::Survey).size(), 1u);
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
      SourceLayer::Survey, lev.cellIndex(gggs::geoPoint(43.0, -70.5)),
      BathyCell{-10.0, 0.5});
    ASSERT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 1u);
  }
  // Drop a bogus value-tile name (level 99) into the same layer dir.
  const std::filesystem::path bogus = dir_ / "survey" / "99_0_0.tif";
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
  const uint8_t lvl = 5;
  gggs::Level lev(lvl);
  const auto cell_in = lev.cellIndex(gggs::geoPoint(43.0, -70.5));
  const auto cell_out = lev.cellIndex(gggs::geoPoint(50.0, -40.0));
  ASSERT_FALSE(cell_in.grid() == cell_out.grid());

  {
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    marine_bathymetry_store::BathymetryTile t_in(cell_in.grid());
    t_in.set(cell_in.row(), cell_in.column(), BathyCell{-10.0, 0.5});
    tiles.emplace(cell_in.grid(), std::move(t_in));
    marine_bathymetry_store::BathymetryTile t_out(cell_out.grid());
    t_out.set(cell_out.row(), cell_out.column(), BathyCell{-20.0, 0.5});
    tiles.emplace(cell_out.grid(), std::move(t_out));

    BathymetryStore store(lvl);
    ASSERT_EQ(store.importTiles(SourceLayer::Survey, std::move(tiles)), 2u);
    // Save so tiles are clean (importTiles marks them dirty; we want to test
    // eviction of clean tiles here — the dirty-guard test is separate).
    marine_bathymetry_store::save(store, dir_.string());
    ASSERT_TRUE(store.get(SourceLayer::Survey, cell_in).has_value());
    ASSERT_TRUE(store.get(SourceLayer::Survey, cell_out).has_value());

    const gggs::GridIndex grid_in = cell_in.grid();
    const auto min_pt =
      makeGeoPoint(grid_in.southLatitude(), grid_in.westLongitude());
    const auto max_pt =
      makeGeoPoint(grid_in.northLatitude(), grid_in.eastLongitude());

    const std::size_t evicted = marine_bathymetry_store::evictOutside(
      store, min_pt, max_pt);

    EXPECT_EQ(evicted, 1u);
    EXPECT_TRUE(store.get(SourceLayer::Survey, cell_in).has_value());
    EXPECT_FALSE(store.get(SourceLayer::Survey, cell_out).has_value());
  }
}

TEST_F(TileIoTest, EvictOutsideDirtyTileNotEvicted)
{
  // A dirty (unsaved) Survey tile outside the keep-box must NOT be evicted.
  // importTiles marks tiles dirty, so we skip save() intentionally.
  const uint8_t lvl = 5;
  gggs::Level lev(lvl);
  const auto cell_out = lev.cellIndex(gggs::geoPoint(50.0, -40.0));

  std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
  marine_bathymetry_store::BathymetryTile t(cell_out.grid());
  t.set(cell_out.row(), cell_out.column(), BathyCell{-20.0, 0.5});
  tiles.emplace(cell_out.grid(), std::move(t));

  BathymetryStore store(lvl);
  ASSERT_EQ(store.importTiles(SourceLayer::Survey, std::move(tiles)), 1u);
  // Tile is dirty (importTiles marks it dirty); do NOT save here.
  ASSERT_TRUE(store.get(SourceLayer::Survey, cell_out)->depth == -20.0);

  // Keep-box is far from the tile's position.
  const auto min_pt = makeGeoPoint(42.0, -71.0);
  const auto max_pt = makeGeoPoint(44.0, -70.0);

  const std::size_t evicted = marine_bathymetry_store::evictOutside(
    store, min_pt, max_pt);

  // Dirty-tile guard: evicted count must be 0 (tile was outside but dirty).
  EXPECT_EQ(evicted, 0u);
  // Tile must still be present.
  EXPECT_TRUE(store.get(SourceLayer::Survey, cell_out).has_value());
}
