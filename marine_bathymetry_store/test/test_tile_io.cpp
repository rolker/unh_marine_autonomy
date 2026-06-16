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
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/tile_io.hpp"

using marine_bathymetry_store::BathyCell;
using marine_bathymetry_store::BathymetryStore;
using marine_bathymetry_store::BathymetryTile;
using marine_bathymetry_store::Provenance;
using marine_bathymetry_store::SourceLayer;

namespace fs = std::filesystem;

namespace
{

constexpr const char * kDay1 = "2026-06-09";
constexpr const char * kDay2 = "2026-06-10";

/// Build a single-tile map holding @p value at @p cell, for importEpoch.
std::map<gggs::GridIndex, BathymetryTile> oneTile(
  const gggs::CellIndex & cell, const BathyCell & value)
{
  std::map<gggs::GridIndex, BathymetryTile> tiles;
  BathymetryTile tile(cell.grid());
  tile.set(cell.row(), cell.column(), value);
  tiles.emplace(cell.grid(), std::move(tile));
  return tiles;
}

}  // namespace

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
  store.set(SourceLayer::Draft, kDay1, store.cellIndex(43.0, -70.5),
    BathyCell{-30.123, 0.456, 1.78e9});
  store.set(SourceLayer::Processed, kDay1, store.cellIndex(44.0, -71.0),
    BathyCell{-12.5, 0.2, 1.79e9});

  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 2u);

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 2u);

  const auto draft = reloaded.get(SourceLayer::Draft, kDay1, reloaded.cellIndex(43.0, -70.5));
  ASSERT_TRUE(draft.has_value());
  EXPECT_DOUBLE_EQ(draft->depth, -30.123);
  EXPECT_DOUBLE_EQ(draft->uncertainty, 0.456);
  // Float64 timestamp band: absolute Unix seconds survive exactly.
  // (A Float32 band would lose ~128 s here — this is the precision guard.)
  EXPECT_DOUBLE_EQ(draft->timestamp, 1.78e9);

  const auto processed =
    reloaded.get(SourceLayer::Processed, kDay1, reloaded.cellIndex(44.0, -71.0));
  ASSERT_TRUE(processed.has_value());
  EXPECT_DOUBLE_EQ(processed->depth, -12.5);
  EXPECT_DOUBLE_EQ(processed->timestamp, 1.79e9);
}

TEST_F(TileIoTest, RoundTripPreservesEpochsIndependently)
{
  // Two epochs of the same layer survive a save/load as distinct surfaces.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, kDay1, cell, BathyCell{-30.0, 0.5, 1.0});
  store.set(SourceLayer::Draft, kDay2, cell, BathyCell{-28.0, 0.4, 2.0});

  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 2u);
  EXPECT_TRUE(fs::is_directory(dir_ / "draft" / kDay1));
  EXPECT_TRUE(fs::is_directory(dir_ / "draft" / kDay2));

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 2u);
  EXPECT_DOUBLE_EQ(reloaded.get(SourceLayer::Draft, kDay1, cell)->depth, -30.0);
  EXPECT_DOUBLE_EQ(reloaded.get(SourceLayer::Draft, kDay2, cell)->depth, -28.0);
}

TEST_F(TileIoTest, ProvenanceSurvivesRoundTrip)
{
  // A compacted epoch must reload as Replayed — otherwise a later live
  // snapshot could regress it after a restart (ADR-0002 §A1.2).
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Draft, kDay1, cell, BathyCell{-30.0, 0.5, 1.0});
  ASSERT_TRUE(
    store.importEpoch(
      SourceLayer::Draft, kDay2, oneTile(cell, BathyCell{-28.0, 0.2, 2.0}),
      Provenance::Replayed));
  marine_bathymetry_store::save(store, dir_.string());

  BathymetryStore reloaded(5);
  marine_bathymetry_store::load(reloaded, dir_.string());
  EXPECT_EQ(
    reloaded.epochs(SourceLayer::Draft).at(kDay1).provenance, Provenance::LiveFused);
  EXPECT_EQ(
    reloaded.epochs(SourceLayer::Draft).at(kDay2).provenance, Provenance::Replayed);
  // The reloaded compacted epoch still refuses live writes.
  EXPECT_FALSE(reloaded.set(SourceLayer::Draft, kDay2, cell, BathyCell{-50.0, 5.0, 3.0}));
}

TEST_F(TileIoTest, CompactionRemovesStaleTilesOnDisk)
{
  // The live surface covered two distant cells (two grids); the compaction
  // product covers only one. After save, the stale tile file must be gone —
  // a reload must see exactly the compacted surface (ADR-0002 §A1.2
  // "supersedes wholesale").
  BathymetryStore store(5);
  const auto cell_a = store.cellIndex(43.0, -70.5);
  const auto cell_b = store.cellIndex(44.0, -71.0);   // a different grid
  store.set(SourceLayer::Draft, kDay1, cell_a, BathyCell{-30.0, 0.5, 1.0});
  store.set(SourceLayer::Draft, kDay1, cell_b, BathyCell{-20.0, 0.5, 1.0});
  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 2u);

  ASSERT_TRUE(
    store.importEpoch(
      SourceLayer::Draft, kDay1, oneTile(cell_a, BathyCell{-29.5, 0.2, 2.0}),
      Provenance::Replayed));
  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 1u);

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 1u);
  EXPECT_DOUBLE_EQ(reloaded.get(SourceLayer::Draft, kDay1, cell_a)->depth, -29.5);
  EXPECT_FALSE(reloaded.get(SourceLayer::Draft, kDay1, cell_b).has_value());
}

TEST_F(TileIoTest, LoadedTilesAreCleanAndDontResave)
{
  BathymetryStore store(5);
  store.set(SourceLayer::Draft, kDay1, store.cellIndex(43.0, -70.5),
    BathyCell{-30.0, 0.5, 1.0});

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
  store.set(SourceLayer::Draft, kDay1, store.cellIndex(43.0, -70.5),
    BathyCell{-30.0, 0.5, 1.0});
  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 1u);

  store.set(SourceLayer::Draft, kDay1, store.cellIndex(43.0, -70.5),
    BathyCell{-31.0, 0.4, 2.0});
  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 1u);
}

TEST_F(TileIoTest, LayersAndEpochsWriteToSeparateSubdirectories)
{
  BathymetryStore store(5);
  store.set(SourceLayer::Processed, kDay1, store.cellIndex(43.0, -70.5),
    BathyCell{-10.0, 0.1, 1.0});
  store.set(SourceLayer::Draft, kDay1, store.cellIndex(43.0, -70.5),
    BathyCell{-12.0, 0.5, 2.0});
  marine_bathymetry_store::save(store, dir_.string());

  EXPECT_TRUE(fs::is_directory(dir_ / "processed" / kDay1));
  EXPECT_TRUE(fs::is_directory(dir_ / "draft" / kDay1));
  EXPECT_TRUE(fs::is_regular_file(dir_ / "draft" / kDay1 / "provenance"));
}

TEST_F(TileIoTest, LoadRejectsTilesFromAnotherLevel)
{
  BathymetryStore store(5);
  store.set(SourceLayer::Draft, kDay1, store.cellIndex(43.0, -70.5),
    BathyCell{-30.0, 0.5, 1.0});
  marine_bathymetry_store::save(store, dir_.string());

  BathymetryStore wrong_level(6);
  EXPECT_THROW(marine_bathymetry_store::load(wrong_level, dir_.string()), std::runtime_error);
}

TEST_F(TileIoTest, MissingProvenanceSidecarLoadsAsLiveFused)
{
  // Conservative default: an epoch without a sidecar loads at the lower rank,
  // so a later compaction can still supersede it.
  BathymetryStore store(5);
  store.set(SourceLayer::Draft, kDay1, store.cellIndex(43.0, -70.5),
    BathyCell{-30.0, 0.5, 1.0});
  marine_bathymetry_store::save(store, dir_.string());
  fs::remove(dir_ / "draft" / kDay1 / "provenance");

  BathymetryStore reloaded(5);
  marine_bathymetry_store::load(reloaded, dir_.string());
  EXPECT_EQ(
    reloaded.epochs(SourceLayer::Draft).at(kDay1).provenance, Provenance::LiveFused);
}

TEST_F(TileIoTest, CrlfProvenanceSidecarStillLoadsAsReplayed)
{
  // A sidecar written with CRLF line endings (e.g. on a Windows host) leaves a
  // trailing '\r' on the token. readProvenance() must trim it — otherwise the
  // exact-match parse fails and the epoch silently downgrades to LiveFused,
  // allowing live writes over replayed data after reload.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  ASSERT_TRUE(
    store.importEpoch(
      SourceLayer::Draft, kDay1, oneTile(cell, BathyCell{-28.0, 0.2, 2.0}),
      Provenance::Replayed));
  marine_bathymetry_store::save(store, dir_.string());

  // Rewrite the sidecar with a CRLF line ending.
  {
    std::ofstream out(dir_ / "draft" / kDay1 / "provenance", std::ios::binary);
    out << "replayed\r\n";
  }

  BathymetryStore reloaded(5);
  marine_bathymetry_store::load(reloaded, dir_.string());
  EXPECT_EQ(
    reloaded.epochs(SourceLayer::Draft).at(kDay1).provenance, Provenance::Replayed);
  // Still refuses live writes after the CRLF round-trip.
  EXPECT_FALSE(reloaded.set(SourceLayer::Draft, kDay1, cell, BathyCell{-50.0, 5.0, 3.0}));
}

TEST_F(TileIoTest, ChartRoundTripsAndLoadsIntoReadOnlyStore)
{
  // The importer writes Chart (chart_writable); the runtime loads it into a
  // default (read-only-Chart) store. load() populates via getOrCreateTile, not
  // set(), so the prior loads even though live set(Chart) stays forbidden.
  BathymetryStore writer(5, /*chart_writable=*/true);
  writer.set(
    SourceLayer::Chart, kDay1, writer.cellIndex(43.0, -70.5), BathyCell{38.58, 3.0, 1.78e9});
  EXPECT_EQ(marine_bathymetry_store::save(writer, dir_.string()), 1u);
  EXPECT_TRUE(fs::is_directory(dir_ / "chart"));

  BathymetryStore runtime(5);   // Chart NOT writable
  EXPECT_FALSE(runtime.chartWritable());
  EXPECT_EQ(marine_bathymetry_store::load(runtime, dir_.string()), 1u);

  const auto got = runtime.get(SourceLayer::Chart, kDay1, runtime.cellIndex(43.0, -70.5));
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, 38.58);
  EXPECT_DOUBLE_EQ(got->timestamp, 1.78e9);

  // The read-only guard still holds after the prior is loaded.
  EXPECT_THROW(
    runtime.set(
      SourceLayer::Chart, kDay1, runtime.cellIndex(43.0, -70.5), BathyCell{1.0, 1.0, 1.0}),
    std::logic_error);
}

TEST_F(TileIoTest, ProcessedDraftOnlyStoreLoadsWithoutChartDir)
{
  // Back-compat: a Draft-only store with no chart/ subdir still saves and loads;
  // the absent chart layer is simply skipped, not an error.
  BathymetryStore store(5);
  store.set(SourceLayer::Draft, kDay1, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 1.0});
  marine_bathymetry_store::save(store, dir_.string());
  EXPECT_FALSE(fs::exists(dir_ / "chart"));   // no spurious empty chart/ dir

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 1u);
  EXPECT_TRUE(reloaded.epochs(SourceLayer::Chart).empty());
}
