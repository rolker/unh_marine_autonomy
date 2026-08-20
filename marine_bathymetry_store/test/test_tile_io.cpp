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

#include <unistd.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
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

/// RAII guard that restores a directory's permissions on scope exit.
///
/// Tests that lock a directory to force an EACCES must unlock it again no matter
/// how the scope is left — an escaping exception of an unexpected type, or a
/// fatal assertion, would otherwise leave an `r-x` directory behind that makes
/// `TearDown()`'s `remove_all` (and the NEXT run's `SetUp()` `remove_all`) throw,
/// masking the original failure and wedging the test until /tmp is cleaned by
/// hand. The destructor uses the `error_code` overload so it can never throw.
class ScopedPermissions
{
public:
  explicit ScopedPermissions(fs::path dir, fs::perms restore = fs::perms::owner_all)
  : dir_(std::move(dir)), restore_(restore) {}

  ScopedPermissions(const ScopedPermissions &) = delete;
  ScopedPermissions & operator=(const ScopedPermissions &) = delete;

  ~ScopedPermissions()
  {
    std::error_code ec;
    fs::permissions(dir_, restore_, fs::perm_options::replace, ec);
  }

private:
  fs::path dir_;
  fs::perms restore_;
};


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
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-30.123, 0.456789});
  store.set(
    SourceLayer::Reference, store.cellIndex(44.0, -71.0), BathyCell{-12.5, 0.2});

  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 2u);

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 2u);

  const auto survey = reloaded.get(SourceLayer::Draft, reloaded.cellIndex(43.0, -70.5));
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
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5});

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
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5});
  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 1u);

  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-31.0, 0.4});
  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 1u);
}

TEST_F(TileIoTest, LayersWriteToSeparateSubdirectories)
{
  BathymetryStore store(5, /*reference_writable=*/true);
  const auto pcell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Reference, pcell, BathyCell{-10.0, 0.1});
  store.set(SourceLayer::Draft, pcell, BathyCell{-12.0, 0.5});
  marine_bathymetry_store::save(store, dir_.string());

  EXPECT_TRUE(fs::is_directory(dir_ / "reference"));
  EXPECT_TRUE(fs::is_directory(dir_ / "draft"));
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
  writer.set(SourceLayer::Draft, coarse_cell, BathyCell{-30.0, 0.5});
  writer.set(SourceLayer::Draft, fine_cell, BathyCell{-22.0, 0.3});
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

TEST_F(TileIoTest, DraftOnlyStoreLoadsWithoutReferenceDir)
{
  // A store with no reference/ subdir still saves and loads; the absent prior
  // layer is simply skipped, not an error.
  BathymetryStore store(5);
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5});
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
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5});
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
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5});
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
  writer.set(SourceLayer::Draft, cell, BathyCell{-30.0, 0.5});
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
  writer.set(SourceLayer::Draft, cell, BathyCell{-30.0, 0.5});
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

TEST_F(TileIoTest, LoadWarnsOnUnrecognizedStoreLayout)
{
  // A store dir written in an obsolete taxonomy has no recognized layer
  // subdir, so load() reaches no tiles and returns empty. Because an empty
  // bathy prior reads as navigable (unsurveyed_is_lethal default false), that
  // silent-empty load must warn — matching the stale-subdir idiom. (The
  // fixture used `chart/` as its old-layout example until #275 made chart a
  // real layer; `old_unknown_layer/` is taxonomy-proof.)
  fs::create_directories(dir_ / "old_unknown_layer");
  {std::ofstream(dir_ / "old_unknown_layer" / "5_0_0.tif") << "old-layout tile";}

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
  // a new-layout draft/ dir on a store written by old code. It is not a value
  // tile; feeding it to loadTile() would throw and abort the WHOLE load. It must
  // be skipped so the valid value tiles still load.
  BathymetryStore writer(5);
  const auto cell = writer.cellIndex(43.0, -70.5);
  writer.set(SourceLayer::Draft, cell, BathyCell{-30.0, 0.5});
  marine_bathymetry_store::save(writer, dir_.string());

  const std::string tile = marine_bathymetry_store::tileFilename(cell.grid());
  const std::string stem = tile.substr(0, tile.size() - 4);   // drop ".tif"
  {std::ofstream(dir_ / "draft" / (stem + "_source.tif")) << "stale companion";}
  {std::ofstream(dir_ / "draft" / (stem + "_time.tif")) << "stale companion";}

  BathymetryStore reloaded(5);
  std::ostringstream captured;
  std::streambuf * prev = std::cerr.rdbuf(captured.rdbuf());
  const std::size_t n = marine_bathymetry_store::load(reloaded, dir_.string());
  std::cerr.rdbuf(prev);

  EXPECT_EQ(n, 1u) << "the valid value tile must still load past the companions";
  EXPECT_DOUBLE_EQ(reloaded.get(SourceLayer::Draft, cell)->depth, -30.0);
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

// --- Legacy survey/ -> processed/ auto-migration (ADR-0010 D8) ---

TEST_F(TileIoTest, LayerDirNames)
{
  EXPECT_EQ(marine_bathymetry_store::layerDirName(SourceLayer::Processed), "processed");
  EXPECT_EQ(marine_bathymetry_store::layerDirName(SourceLayer::Draft), "draft");
  EXPECT_EQ(marine_bathymetry_store::layerDirName(SourceLayer::Reference), "reference");
  EXPECT_EQ(marine_bathymetry_store::layerDirName(SourceLayer::Chart), "chart");
}

TEST_F(TileIoTest, MigrationSurveyToProcessed)
{
  // A pre-D8 store on disk carries a fused `survey/` layer. load() auto-migrates
  // it wholesale to `processed/` (single rename, the atomic commit point) and the
  // tiles read back under Processed. The legacy dir is gone afterward.
  BathymetryStore writer(5);
  const auto cell = writer.cellIndex(43.0, -70.5);
  writer.set(SourceLayer::Draft, cell, BathyCell{-30.0, 0.5});
  marine_bathymetry_store::save(writer, dir_.string());   // writes draft/
  // Rename the freshly written layer dir to the LEGACY name to simulate a pre-D8
  // store on disk.
  fs::rename(dir_ / "draft", dir_ / "survey");
  ASSERT_TRUE(fs::is_directory(dir_ / "survey"));

  BathymetryStore reloaded(5);
  std::ostringstream captured;
  std::streambuf * prev = std::cerr.rdbuf(captured.rdbuf());
  const std::size_t n = marine_bathymetry_store::load(reloaded, dir_.string());
  std::cerr.rdbuf(prev);

  EXPECT_EQ(n, 1u);
  EXPECT_FALSE(fs::exists(dir_ / "survey"));            // migrated away
  EXPECT_TRUE(fs::is_directory(dir_ / "processed"));    // to the new name
  const auto got = reloaded.get(SourceLayer::Processed, cell);
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, -30.0);
  EXPECT_NE(captured.str().find("migrated legacy 'survey/'"), std::string::npos)
    << "migration must log clearly, got: " << captured.str();
}

TEST_F(TileIoTest, MigrationRefuseBothExist)
{
  // An ambiguous store with BOTH survey/ and processed/ must be refused loudly
  // (throw) rather than silently merging or dropping the authoritative surface.
  fs::create_directories(dir_ / "survey");
  fs::create_directories(dir_ / "processed");

  BathymetryStore reloaded(5);
  EXPECT_THROW(marine_bathymetry_store::load(reloaded, dir_.string()), std::runtime_error);
  // Both dirs survive the refusal (nothing was moved).
  EXPECT_TRUE(fs::is_directory(dir_ / "survey"));
  EXPECT_TRUE(fs::is_directory(dir_ / "processed"));
}

TEST_F(TileIoTest, MigrationRefusesSymlinkedSurveyDir)
{
  // is_directory() follows symlinks, so a symlinked `survey/` would pass the
  // "has survey" probe — but rename() moves the LINK, leaving `processed/` a
  // symlink into out-of-store data. Refuse loudly (throw) and leave both the link
  // and its target untouched, mirroring replaceChartLayer's symlink guard.
  const fs::path real_target = dir_ / "real_survey_target";
  fs::create_directories(real_target);
  fs::create_directory_symlink(real_target, dir_ / "survey");
  ASSERT_TRUE(fs::is_symlink(dir_ / "survey"));

  BathymetryStore reloaded(5);
  EXPECT_THROW(marine_bathymetry_store::load(reloaded, dir_.string()), std::runtime_error);
  // Nothing was moved: the link survives and no real `processed/` was created.
  EXPECT_TRUE(fs::is_symlink(dir_ / "survey"));
  EXPECT_TRUE(fs::is_directory(real_target));
  EXPECT_FALSE(fs::exists(dir_ / "processed"));
}

TEST_F(TileIoTest, MigrationIdempotentReopen)
{
  // After a first load migrates survey/ -> processed/, a second open of the same
  // store dir finds only processed/ and is a clean no-op migration.
  BathymetryStore writer(5);
  const auto cell = writer.cellIndex(43.0, -70.5);
  writer.set(SourceLayer::Draft, cell, BathyCell{-30.0, 0.5});
  marine_bathymetry_store::save(writer, dir_.string());
  fs::rename(dir_ / "draft", dir_ / "survey");

  BathymetryStore first(5);
  ASSERT_EQ(marine_bathymetry_store::load(first, dir_.string()), 1u);
  ASSERT_TRUE(fs::is_directory(dir_ / "processed"));

  // Second open: no survey/ left, so migration is a no-op and the tile loads
  // normally from processed/.
  BathymetryStore second(5);
  std::ostringstream captured;
  std::streambuf * prev = std::cerr.rdbuf(captured.rdbuf());
  const std::size_t n = marine_bathymetry_store::load(second, dir_.string());
  std::cerr.rdbuf(prev);

  EXPECT_EQ(n, 1u);
  EXPECT_TRUE(captured.str().empty()) << "idempotent re-open must not re-log a migration";
  EXPECT_TRUE(second.get(SourceLayer::Processed, cell).has_value());
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
    writer.set(SourceLayer::Draft, c_in, BathyCell{-10.0, 0.5});
    const auto c_out = lev.cellIndex(gggs::geoPoint(50.0, -40.0));
    writer.set(SourceLayer::Draft, c_out, BathyCell{-20.0, 0.5});
    const auto c_str = lev.cellIndex(
      gggs::geoPoint(
        inside_grid.southLatitude() - 0.001,
        inside_grid.westLongitude() + 0.001));
    writer.set(SourceLayer::Draft, c_str, BathyCell{-30.0, 0.5});
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
    writer.set(SourceLayer::Draft, cell, BathyCell{-15.0, 0.5});
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
    writer.set(SourceLayer::Draft, cell, BathyCell{-10.0, 0.5});
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

TEST_F(TileIoTest, LoadWindowMigratesLegacySurveyDir)
{
  // loadWindow shares the migration helper: a legacy survey/ store opened through
  // the windowed path migrates to processed/ just like load().
  const uint8_t lvl = 5;
  gggs::Level lev(lvl);
  {
    BathymetryStore writer(lvl);
    writer.set(SourceLayer::Draft, lev.cellIndex(gggs::geoPoint(43.0, -70.5)),
      BathyCell{-15.0, 0.5});
    ASSERT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 1u);
  }
  fs::rename(dir_ / "draft", dir_ / "survey");

  const gggs::GridIndex grid = lev.gridIndex(43.0, -70.5);
  const auto min_pt = makeGeoPoint(grid.southLatitude(), grid.westLongitude());
  const auto max_pt = makeGeoPoint(grid.northLatitude(), grid.eastLongitude());

  BathymetryStore result(lvl);
  const std::size_t n = marine_bathymetry_store::loadWindow(
    result, dir_.string(), min_pt, max_pt);
  EXPECT_EQ(n, 1u);
  EXPECT_FALSE(fs::exists(dir_ / "survey"));
  EXPECT_TRUE(result.get(SourceLayer::Processed,
    lev.cellIndex(gggs::geoPoint(43.0, -70.5))).has_value());
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
      BathyCell{-10.0, 0.5});
    ASSERT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 1u);
  }
  // Drop a bogus value-tile name (level 99) into the same layer dir.
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
  t.set(cell_out.row(), cell_out.column(), BathyCell{-20.0, 0.5});
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

// --- Chart layer wholesale regeneration (ADR-0010 D7, #275) ---

TEST_F(TileIoTest, ChartLayerRoundTripViaReplaceChartLayer)
{
  // Stage a chart layer in a staging store, swap it into a fresh store dir,
  // and read it back through a normal (non-staging) runtime store.
  BathymetryStore staging(
    5, /*reference_writable=*/false, /*chart_staging_writable=*/true);
  staging.set(SourceLayer::Chart, staging.cellIndex(43.0, -70.5), BathyCell{-20.5, 1.5});
  const fs::path staged_store = dir_ / "staged";
  ASSERT_EQ(marine_bathymetry_store::save(staging, staged_store.string()), 1u);

  const fs::path store_dir = dir_ / "store";
  fs::create_directories(store_dir);
  marine_bathymetry_store::replaceChartLayer(
    (staged_store / "chart").string(), store_dir.string());

  BathymetryStore runtime(5);
  EXPECT_EQ(marine_bathymetry_store::load(runtime, store_dir.string()), 1u);
  const auto got = runtime.get(SourceLayer::Chart, runtime.cellIndex(43.0, -70.5));
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, -20.5);
}

TEST_F(TileIoTest, ReplaceChartLayerRejectsMissingOrEmptyStagedDir)
{
  // Neither a nonexistent staged dir nor one with no .tif tiles may swap in;
  // the existing chart layer stands untouched either way.
  BathymetryStore staging(5, false, /*chart_staging_writable=*/true);
  staging.set(SourceLayer::Chart, staging.cellIndex(43.0, -70.5), BathyCell{-20.5, 1.5});
  const fs::path staged_store = dir_ / "staged";
  ASSERT_EQ(marine_bathymetry_store::save(staging, staged_store.string()), 1u);
  const fs::path store_dir = dir_ / "store";
  fs::create_directories(store_dir);
  marine_bathymetry_store::replaceChartLayer(
    (staged_store / "chart").string(), store_dir.string());

  EXPECT_THROW(
    marine_bathymetry_store::replaceChartLayer(
      (dir_ / "nonexistent").string(), store_dir.string()),
    std::runtime_error);

  const fs::path empty_staged = dir_ / "empty_staged";
  fs::create_directories(empty_staged);
  EXPECT_THROW(
    marine_bathymetry_store::replaceChartLayer(
      empty_staged.string(), store_dir.string()),
    std::runtime_error);

  // Old layer intact after both refusals.
  BathymetryStore runtime(5);
  EXPECT_EQ(marine_bathymetry_store::load(runtime, store_dir.string()), 1u);
  EXPECT_TRUE(
    runtime.get(SourceLayer::Chart, runtime.cellIndex(43.0, -70.5)).has_value());
}

/// Assert `replaceChartLayer` refuses with a `std::runtime_error` whose message
/// names the expected guard. Matching the message (not merely the type) keeps each
/// refusal case pinned to the check it is meant to exercise — an earlier guard
/// firing first would otherwise make the case silently vacuous.
static void expectChartRefusal(
  const fs::path & staged, const fs::path & store_dir, const std::string & needle)
{
  try {
    marine_bathymetry_store::replaceChartLayer(staged.string(), store_dir.string());
    ADD_FAILURE() << "expected replaceChartLayer to refuse (" << needle << ")";
  } catch (const std::runtime_error & e) {
    EXPECT_NE(std::string(e.what()).find(needle), std::string::npos)
      << "refusal did not come from the expected guard (" << needle << "): " << e.what();
  }
}

TEST_F(TileIoTest, ReplaceChartLayerRejectsSymlinkedAliasedAndNonDirectoryPaths)
{
  // Dedicated refusal coverage for the pre-swap hardening guards: a SYMLINKED
  // staged dir (is_directory() follows the link, so only the explicit
  // is_symlink() check catches it), a staged dir ALIASING the live chart/ or its
  // .chart_backup/ recovery path, and a NON-DIRECTORY store dir. Every refusal
  // must land before anything is moved, leaving the live layer untouched.
  const fs::path store_dir = dir_ / "store";
  fs::create_directories(store_dir);

  // Seed the live chart/ layer whose survival each refusal must preserve.
  BathymetryStore seed(5, false, /*chart_staging_writable=*/true);
  seed.set(SourceLayer::Chart, seed.cellIndex(43.0, -70.5), BathyCell{-20.5, 1.5});
  const fs::path seed_staged = dir_ / "seed";
  ASSERT_EQ(marine_bathymetry_store::save(seed, seed_staged.string()), 1u);
  marine_bathymetry_store::replaceChartLayer(
    (seed_staged / "chart").string(), store_dir.string());

  // A perfectly valid staged layer — only the path it is reached by is wrong.
  BathymetryStore regen(5, false, /*chart_staging_writable=*/true);
  regen.set(SourceLayer::Chart, regen.cellIndex(43.0, -70.5), BathyCell{-99.0, 2.0});
  const fs::path regen_root = dir_ / "regen";
  ASSERT_EQ(marine_bathymetry_store::save(regen, regen_root.string()), 1u);
  const fs::path real_staged = regen_root / "chart";

  // 1. Symlink to a real staged dir: renaming it in would leave chart/ a link to
  //    an out-of-store tree.
  const fs::path staged_link = dir_ / "staged_link";
  fs::create_directory_symlink(real_staged, staged_link);
  expectChartRefusal(staged_link, store_dir, "is a symlink");
  EXPECT_TRUE(fs::exists(real_staged));   // the link target was not moved

  // 2. staged IS the live chart/ layer (equivalent()) — nothing to swap in.
  expectChartRefusal(store_dir / "chart", store_dir, "is the live chart/ layer");

  // 3. staged IS the .chart_backup/ recovery path — the swap would rename the
  //    live chart/ onto the very tree it is being asked to commit.
  const fs::path backup = store_dir / ".chart_backup";
  fs::create_directories(backup);
  {
    std::ofstream tile(backup / "tile.tif");
    tile << "x";
  }
  expectChartRefusal(backup, store_dir, "is the .chart_backup/ recovery path");
  EXPECT_TRUE(fs::exists(backup / "tile.tif"));   // refused before any removal
  fs::remove_all(backup);   // planted by this case only; clear it for the check below

  // 4. A real staged dir whose only `.tif` is a SYMLINK to a tile outside the
  //    store: is_regular_file() follows the link, so the value-tile check alone
  //    would accept it and commit a link into the live navigation layer.
  fs::path real_tile;
  for (const auto & entry : fs::directory_iterator(real_staged)) {
    if (entry.is_regular_file() && entry.path().extension() == ".tif") {
      real_tile = entry.path();
      break;
    }
  }
  ASSERT_FALSE(real_tile.empty()) << "expected a .tif in the staged layer";
  const fs::path linked_staged = dir_ / "linked_staged";
  fs::create_directories(linked_staged);
  // A live (non-dangling) link, so is_regular_file() really does say "yes".
  fs::create_symlink(real_tile, linked_staged / real_tile.filename());
  ASSERT_TRUE(fs::is_regular_file(linked_staged / real_tile.filename()));
  expectChartRefusal(linked_staged, store_dir, "contains a symlinked entry");

  // 5. store dir that exists but is a regular file, not a directory.
  const fs::path file_store = dir_ / "not_a_dir";
  {
    std::ofstream f(file_store);
    f << "x";
  }
  expectChartRefusal(real_staged, file_store, "store dir '");

  // The live layer stands after every refusal, still holding the seeded value.
  EXPECT_FALSE(fs::exists(store_dir / ".chart_backup"));
  BathymetryStore runtime(5);
  EXPECT_EQ(marine_bathymetry_store::load(runtime, store_dir.string()), 1u);
  const auto got = runtime.get(SourceLayer::Chart, runtime.cellIndex(43.0, -70.5));
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, -20.5);
}

TEST_F(TileIoTest, ReplaceChartLayerRejectsMisnamedStagedTile)
{
  // A staged `.tif` whose name has no parseable GGGS level prefix is rejected
  // BEFORE the swap. `load()`/`loadWindow()` call levelFromTileFilename() on every
  // non-companion `.tif` and it throws on such a name; because `Chart` is last in
  // source_layers_by_priority, that throw at load time aborts the whole load after
  // processed/draft/reference were read and blacks out the bathymetry layer for the run.
  // The gate must therefore be at least as strict as the load path so the
  // mis-named tile never commits into the live navigation layer — the old chart/
  // must stand untouched, with no backup left behind.
  const fs::path store_dir = dir_ / "store";
  fs::create_directories(store_dir);

  // Seed the live chart/ layer whose survival the refusal must preserve.
  BathymetryStore seed(5, false, /*chart_staging_writable=*/true);
  seed.set(SourceLayer::Chart, seed.cellIndex(43.0, -70.5), BathyCell{-20.5, 1.5});
  const fs::path seed_staged = dir_ / "seed";
  ASSERT_EQ(marine_bathymetry_store::save(seed, seed_staged.string()), 1u);
  marine_bathymetry_store::replaceChartLayer(
    (seed_staged / "chart").string(), store_dir.string());

  // A staged layer holding a real value tile AND a mis-named one. The scan reaches
  // the bad name and refuses even though a valid tile is present (it scans every
  // entry), so the outcome does not depend on directory iteration order.
  BathymetryStore regen(5, false, /*chart_staging_writable=*/true);
  regen.set(SourceLayer::Chart, regen.cellIndex(43.0, -70.5), BathyCell{-99.0, 2.0});
  const fs::path regen_root = dir_ / "regen";
  ASSERT_EQ(marine_bathymetry_store::save(regen, regen_root.string()), 1u);
  const fs::path staged = regen_root / "chart";
  {
    std::ofstream bad(staged / "chart.tif");   // non-numeric: no level prefix
    bad << "x";
  }

  expectChartRefusal(staged, store_dir, "level prefix");

  // No backup was made and the old layer stands, still holding the seeded value.
  EXPECT_FALSE(fs::exists(store_dir / ".chart_backup"));
  BathymetryStore runtime(5);
  EXPECT_EQ(marine_bathymetry_store::load(runtime, store_dir.string()), 1u);
  const auto got = runtime.get(SourceLayer::Chart, runtime.cellIndex(43.0, -70.5));
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, -20.5);
}

TEST_F(TileIoTest, ReplaceChartLayerRemovesStaleTilesWholesale)
{
  // Regeneration 1 covers two cells in different grids; regeneration 2 covers
  // only one. After the second swap the dropped tile is GONE (supersession,
  // never merge — ADR-0010 D7 / Context: importTiles clobber semantics).
  const fs::path store_dir = dir_ / "store";
  fs::create_directories(store_dir);

  BathymetryStore regen1(5, false, /*chart_staging_writable=*/true);
  regen1.set(SourceLayer::Chart, regen1.cellIndex(43.0, -70.5), BathyCell{-20.0, 1.5});
  regen1.set(SourceLayer::Chart, regen1.cellIndex(44.0, -71.0), BathyCell{-30.0, 1.5});
  const fs::path staged1 = dir_ / "staged1";
  ASSERT_EQ(marine_bathymetry_store::save(regen1, staged1.string()), 2u);
  marine_bathymetry_store::replaceChartLayer(
    (staged1 / "chart").string(), store_dir.string());

  BathymetryStore regen2(5, false, /*chart_staging_writable=*/true);
  regen2.set(SourceLayer::Chart, regen2.cellIndex(43.0, -70.5), BathyCell{-21.0, 1.4});
  const fs::path staged2 = dir_ / "staged2";
  ASSERT_EQ(marine_bathymetry_store::save(regen2, staged2.string()), 1u);
  marine_bathymetry_store::replaceChartLayer(
    (staged2 / "chart").string(), store_dir.string());

  BathymetryStore runtime(5);
  EXPECT_EQ(marine_bathymetry_store::load(runtime, store_dir.string()), 1u);
  const auto kept = runtime.get(SourceLayer::Chart, runtime.cellIndex(43.0, -70.5));
  ASSERT_TRUE(kept.has_value());
  EXPECT_DOUBLE_EQ(kept->depth, -21.0);   // updated value from regen 2
  EXPECT_FALSE(
    runtime.get(SourceLayer::Chart, runtime.cellIndex(44.0, -71.0)).has_value());
  EXPECT_FALSE(fs::exists(store_dir / ".chart_backup"));   // backup cleaned up
}

TEST_F(TileIoTest, ReplaceChartLayerClearsStaleBackupFromCrashedRun)
{
  // A leftover .chart_backup/ from a crashed prior run must not wedge the
  // next regeneration (plan-review finding: ENOTEMPTY on the backup rename).
  const fs::path store_dir = dir_ / "store";
  const fs::path stale_backup = store_dir / ".chart_backup";
  fs::create_directories(stale_backup);
  {
    std::ofstream junk(stale_backup / "junk.tif");
    junk << "stale";
  }

  BathymetryStore staging(5, false, /*chart_staging_writable=*/true);
  staging.set(SourceLayer::Chart, staging.cellIndex(43.0, -70.5), BathyCell{-20.5, 1.5});
  const fs::path staged_store = dir_ / "staged";
  ASSERT_EQ(marine_bathymetry_store::save(staging, staged_store.string()), 1u);

  // First swap: no existing chart/, stale backup present -> must succeed.
  EXPECT_NO_THROW(
    marine_bathymetry_store::replaceChartLayer(
      (staged_store / "chart").string(), store_dir.string()));
  EXPECT_FALSE(fs::exists(stale_backup));

  // Second swap over the existing chart/ with another stale backup planted.
  fs::create_directories(stale_backup);
  {
    std::ofstream junk(stale_backup / "junk.tif");
    junk << "stale";
  }
  BathymetryStore staging2(5, false, /*chart_staging_writable=*/true);
  staging2.set(SourceLayer::Chart, staging2.cellIndex(43.0, -70.5), BathyCell{-22.0, 1.2});
  const fs::path staged2 = dir_ / "staged2";
  ASSERT_EQ(marine_bathymetry_store::save(staging2, staged2.string()), 1u);
  EXPECT_NO_THROW(
    marine_bathymetry_store::replaceChartLayer(
      (staged2 / "chart").string(), store_dir.string()));
  EXPECT_FALSE(fs::exists(stale_backup));

  BathymetryStore runtime(5);
  EXPECT_EQ(marine_bathymetry_store::load(runtime, store_dir.string()), 1u);
  const auto got = runtime.get(SourceLayer::Chart, runtime.cellIndex(43.0, -70.5));
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, -22.0);
}

TEST_F(TileIoTest, ReplaceChartLayerSurvivesFailedPostCommitBackupCleanup)
{
  // A swap that has already COMMITTED must not surface as an exception just
  // because terminal cleanup of .chart_backup/ fails — the caller could not then
  // tell "swap failed, old layer stands" from "swap succeeded, stale backup left"
  // and might wrongly retry or abort the regeneration run. Force the cleanup to
  // fail by making the old chart/ dir non-writable: renaming it to .chart_backup/
  // needs only store_dir write permission, but unlinking its contents does not.
  if (::geteuid() == 0) {
    GTEST_SKIP() << "runs as root: directory-permission denial is bypassed";
  }

  const fs::path store_dir = dir_ / "store";
  const fs::path chart = store_dir / "chart";
  const fs::path backup = store_dir / ".chart_backup";
  fs::create_directories(store_dir);

  BathymetryStore seed(5, false, /*chart_staging_writable=*/true);
  seed.set(SourceLayer::Chart, seed.cellIndex(43.0, -70.5), BathyCell{-20.5, 1.5});
  const fs::path seed_staged = dir_ / "seed";
  ASSERT_EQ(marine_bathymetry_store::save(seed, seed_staged.string()), 1u);
  marine_bathymetry_store::replaceChartLayer((seed_staged / "chart").string(), store_dir.string());

  BathymetryStore regen(5, false, /*chart_staging_writable=*/true);
  regen.set(SourceLayer::Chart, regen.cellIndex(43.0, -70.5), BathyCell{-42.0, 1.0});
  const fs::path regen_root = dir_ / "regen";
  ASSERT_EQ(marine_bathymetry_store::save(regen, regen_root.string()), 1u);

  // Guard both paths the locked directory can occupy (chart/ before the swap,
  // .chart_backup/ after it) so TearDown's remove_all always has permission.
  const ScopedPermissions unlock_chart(chart);
  const ScopedPermissions unlock_backup(backup);
  fs::permissions(
    chart, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);

  EXPECT_NO_THROW(
    marine_bathymetry_store::replaceChartLayer(
      (regen_root / "chart").string(), store_dir.string()));

  // The commit stands: the new layer is live, and the backup that could not be
  // cleaned is merely left behind (the next run drops it as stale).
  EXPECT_TRUE(fs::exists(backup));
  {
    BathymetryStore runtime(5);
    EXPECT_EQ(marine_bathymetry_store::load(runtime, store_dir.string()), 1u);
    const auto got = runtime.get(SourceLayer::Chart, runtime.cellIndex(43.0, -70.5));
    ASSERT_TRUE(got.has_value());
    EXPECT_DOUBLE_EQ(got->depth, -42.0);
  }

  // The recovery half of the premise: the docs promise the leftover backup "is
  // cleared by the next regeneration run", so a SECOND swap over the same store
  // must succeed. The cause of the cleanup failure is persistent (the backup dir
  // is still r-x), so this only holds because the pre-swap stale-backup drop is
  // tolerant — remove_all via error_code, then rename aside. A throwing
  // remove_all there would wedge every regeneration from here on.
  const fs::path aside = store_dir / ".chart_backup.stale.0";
  const ScopedPermissions unlock_aside(aside);

  BathymetryStore regen2(5, false, /*chart_staging_writable=*/true);
  regen2.set(SourceLayer::Chart, regen2.cellIndex(43.0, -70.5), BathyCell{-7.5, 0.9});
  const fs::path regen2_root = dir_ / "regen2";
  ASSERT_EQ(marine_bathymetry_store::save(regen2, regen2_root.string()), 1u);
  EXPECT_NO_THROW(
    marine_bathymetry_store::replaceChartLayer(
      (regen2_root / "chart").string(), store_dir.string()));

  // The stubborn backup was moved out of the way, not deleted (its contents are
  // still unlinkable), and this swap's own backup was cleaned normally.
  EXPECT_TRUE(fs::exists(aside));
  EXPECT_FALSE(fs::exists(backup));

  BathymetryStore runtime2(5);
  EXPECT_EQ(marine_bathymetry_store::load(runtime2, store_dir.string()), 1u);
  const auto got2 = runtime2.get(SourceLayer::Chart, runtime2.cellIndex(43.0, -70.5));
  ASSERT_TRUE(got2.has_value());
  EXPECT_DOUBLE_EQ(got2->depth, -7.5);   // the second regeneration is live
}

TEST_F(TileIoTest, ReplaceChartLayerRestoresChartWhenCommitRenameFails)
{
  // The must-fix: exercise the restore branch (rename backup -> chart after a
  // failed commit). We force the commit rename (staged -> chart) to fail AFTER
  // the live chart/ has already moved to .chart_backup/, and assert chart/ is put
  // back intact with its ORIGINAL value (ADR-0010 D7: a failed swap leaves the
  // old layer standing). The trick: make the STAGED dir's parent read-only so
  // removing the staged entry — part of rename(2) — fails with EACCES, while the
  // chart/ -> .chart_backup/ rename inside the writable store dir still succeeds.
  if (::geteuid() == 0) {
    GTEST_SKIP() << "runs as root: directory-permission denial is bypassed";
  }

  const fs::path store_dir = dir_ / "store";
  fs::create_directories(store_dir);

  // Seed an existing chart/ layer with a known value — the layer whose survival
  // through a failed swap we verify below.
  BathymetryStore seed(5, false, /*chart_staging_writable=*/true);
  seed.set(SourceLayer::Chart, seed.cellIndex(43.0, -70.5), BathyCell{-20.5, 1.5});
  const fs::path seed_staged = dir_ / "seed";
  ASSERT_EQ(marine_bathymetry_store::save(seed, seed_staged.string()), 1u);
  marine_bathymetry_store::replaceChartLayer(
    (seed_staged / "chart").string(), store_dir.string());

  // Build a valid replacement whose staged chart/ lives under a parent we lock.
  const fs::path ro_parent = dir_ / "ro_parent";
  fs::create_directories(ro_parent);
  BathymetryStore regen(5, false, /*chart_staging_writable=*/true);
  regen.set(SourceLayer::Chart, regen.cellIndex(43.0, -70.5), BathyCell{-99.0, 2.0});
  ASSERT_EQ(marine_bathymetry_store::save(regen, ro_parent.string()), 1u);
  const fs::path staged = ro_parent / "chart";   // save() wrote chart/ under ro_parent

  // Lock the staged dir's parent: rename(staged, chart) must remove `chart` from
  // ro_parent, which now lacks write permission -> EACCES mid-swap. (Keep read +
  // exec so the pre-swap validation walk of staged still works.) The guard
  // restores owner_all on scope exit — unconditionally, so no escaping exception
  // type or fatal assertion can leave a locked dir for TearDown to trip over.
  const ScopedPermissions unlock_ro_parent(ro_parent);
  fs::permissions(
    ro_parent, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);

  // Pin the throw to the COMMIT rename (staged -> chart) rather than accept any
  // filesystem_error: assert EACCES (permission_denied) and that path1() is the
  // staged source. Pre-commit validation refusals throw std::runtime_error (not
  // an fs::filesystem_error), so catching only the latter also keeps this
  // non-vacuous — the sole filesystem_error reachable here is the commit rename.
  bool threw = false;
  try {
    marine_bathymetry_store::replaceChartLayer(staged.string(), store_dir.string());
  } catch (const fs::filesystem_error & e) {
    threw = true;
    EXPECT_EQ(e.code(), std::errc::permission_denied);
    EXPECT_EQ(e.path1(), staged);
  }
  EXPECT_TRUE(threw) << "expected replaceChartLayer to throw fs::filesystem_error";

  // (ro_parent is unlocked by `unlock_ro_parent` on scope exit — before TearDown.)

  // The original chart/ is back, holds its ORIGINAL value (the regen never
  // committed), and no backup is left behind.
  EXPECT_FALSE(fs::exists(store_dir / ".chart_backup"));
  BathymetryStore runtime(5);
  EXPECT_EQ(marine_bathymetry_store::load(runtime, store_dir.string()), 1u);
  const auto got = runtime.get(SourceLayer::Chart, runtime.cellIndex(43.0, -70.5));
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, -20.5);
}

TEST_F(TileIoTest, ReplaceChartLayerRestoresOrphanedBackupThenSurvivesFailedSwap)
{
  // Crash recovery: start from a mid-swap crash state — chart/ absent,
  // .chart_backup/ holding the old layer (the only surviving copy) — then drive a
  // NEW swap that itself fails at the commit rename. The orphan must be RESTORED
  // (not discarded), so the old layer stands after the failed swap. With the old
  // discard behavior the store would be left with NO chart layer at all.
  if (::geteuid() == 0) {
    GTEST_SKIP() << "runs as root: directory-permission denial is bypassed";
  }

  const fs::path store_dir = dir_ / "store";
  const fs::path backup = store_dir / ".chart_backup";
  fs::create_directories(store_dir);

  // Plant an orphaned backup (a real saved chart/ renamed into place); chart/ absent.
  BathymetryStore old_layer(5, false, /*chart_staging_writable=*/true);
  old_layer.set(SourceLayer::Chart, old_layer.cellIndex(43.0, -70.5), BathyCell{-10.0, 1.0});
  const fs::path old_src = dir_ / "old";
  ASSERT_EQ(marine_bathymetry_store::save(old_layer, old_src.string()), 1u);
  fs::rename(old_src / "chart", backup);
  ASSERT_FALSE(fs::exists(store_dir / "chart"));

  // A valid new regeneration whose staged chart/ sits under a parent we lock, so
  // the commit rename fails after recovery has restored the orphan.
  const fs::path ro_parent = dir_ / "ro_parent";
  fs::create_directories(ro_parent);
  BathymetryStore regen(5, false, /*chart_staging_writable=*/true);
  regen.set(SourceLayer::Chart, regen.cellIndex(44.0, -71.0), BathyCell{-30.0, 1.5});
  ASSERT_EQ(marine_bathymetry_store::save(regen, ro_parent.string()), 1u);
  const fs::path staged = ro_parent / "chart";
  // Same unconditional-unlock guard as the sibling test: EXPECT_THROW swallowing a
  // non-matching exception type is not a reason to leave the unlock conditional.
  const ScopedPermissions unlock_ro_parent(ro_parent);
  fs::permissions(
    ro_parent, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);

  EXPECT_THROW(
    marine_bathymetry_store::replaceChartLayer(staged.string(), store_dir.string()),
    fs::filesystem_error);

  // The restored old layer stands (the failed regen left no chart/ gap), and no
  // backup lingers.
  EXPECT_FALSE(fs::exists(backup));
  BathymetryStore runtime(5);
  EXPECT_EQ(marine_bathymetry_store::load(runtime, store_dir.string()), 1u);
  const auto got = runtime.get(SourceLayer::Chart, runtime.cellIndex(43.0, -70.5));
  ASSERT_TRUE(got.has_value());   // old orphan data restored and surviving
  EXPECT_DOUBLE_EQ(got->depth, -10.0);
}
