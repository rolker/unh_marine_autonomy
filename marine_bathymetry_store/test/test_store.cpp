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
#include <map>
#include <stdexcept>
#include <utility>

#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"

using marine_bathymetry_store::BathyCell;
using marine_bathymetry_store::BathymetryStore;
using marine_bathymetry_store::SourceLayer;

// Tile count of one layer (one fused surface per layer since #221).
static std::size_t tilesIn(const BathymetryStore & store, SourceLayer layer)
{
  return store.tiles(layer).size();
}

TEST(Store, SetGetRoundTrip)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Survey, cell, BathyCell{-30.0, 0.5});

  const auto got = store.get(SourceLayer::Survey, cell);
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, -30.0);
  EXPECT_DOUBLE_EQ(got->uncertainty, 0.5);
}

TEST(Store, GetEmptyLayerIsNullopt)
{
  BathymetryStore store(5);
  EXPECT_FALSE(store.get(SourceLayer::Survey, store.cellIndex(43.0, -70.5)).has_value());
}

TEST(Store, LayersAreIndependent)
{
  // A survey write must not touch the reference layer at the same cell.
  BathymetryStore store(5, /*reference_writable=*/true);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Reference, cell, BathyCell{-10.0, 0.1});
  store.set(SourceLayer::Survey, cell, BathyCell{-12.0, 2.0});
  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Reference, cell)->depth, -10.0);
  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Survey, cell)->depth, -12.0);
}

TEST(Store, DoubleWriteSameCellLastWriteWins)
{
  // Single fused surface (#221): a second write to the same cell overwrites the
  // first — there is no per-day epoch ordering or provenance guard.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  EXPECT_TRUE(store.set(SourceLayer::Survey, cell, BathyCell{-10.0, 0.1}));
  EXPECT_TRUE(store.set(SourceLayer::Survey, cell, BathyCell{-12.0, 0.2}));
  const auto got = store.get(SourceLayer::Survey, cell);
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, -12.0);
  EXPECT_DOUBLE_EQ(got->uncertainty, 0.2);
  // Still one tile (same grid).
  EXPECT_EQ(tilesIn(store, SourceLayer::Survey), 1u);
}

TEST(Store, TilesAllocatedLazilyPerLayer)
{
  BathymetryStore store(5);
  EXPECT_TRUE(store.tiles(SourceLayer::Survey).empty());
  EXPECT_TRUE(store.tiles(SourceLayer::Reference).empty());

  store.set(SourceLayer::Survey, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5});
  EXPECT_EQ(tilesIn(store, SourceLayer::Survey), 1u);
  EXPECT_TRUE(store.tiles(SourceLayer::Reference).empty());
}

TEST(Store, NoDataCellReadsBackAsNoData)
{
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Survey, cell, BathyCell{});  // depth NaN
  const auto got = store.get(SourceLayer::Survey, cell);
  ASSERT_TRUE(got.has_value());      // tile exists
  EXPECT_FALSE(got->hasData());      // but the cell carries no usable depth
}

TEST(Store, MultiLevelTilesCoexist)
{
  // The store is multi-level (ADR-0002 §D2): a cell at a level other than the
  // store's default level is accepted, stored, and read back independently.
  BathymetryStore store(5);
  gggs::Level fine(6);
  const auto fine_cell = fine.cellIndex(gggs::geoPoint(43.0, -70.5));
  const auto default_cell = store.cellIndex(43.0, -70.5);
  ASSERT_NE(fine_cell.level(), default_cell.level());

  EXPECT_NO_THROW(store.set(SourceLayer::Survey, fine_cell, BathyCell{-22.0, 0.3}));
  store.set(SourceLayer::Survey, default_cell, BathyCell{-20.0, 0.5});

  // Both tiles exist in the same layer at different levels.
  EXPECT_EQ(tilesIn(store, SourceLayer::Survey), 2u);
  const auto fine_got = store.get(SourceLayer::Survey, fine_cell);
  ASSERT_TRUE(fine_got.has_value());
  EXPECT_DOUBLE_EQ(fine_got->depth, -22.0);
  const auto default_got = store.get(SourceLayer::Survey, default_cell);
  ASSERT_TRUE(default_got.has_value());
  EXPECT_DOUBLE_EQ(default_got->depth, -20.0);
}

TEST(Store, InvalidCellThrows)
{
  BathymetryStore store(5);
  EXPECT_THROW(store.set(SourceLayer::Survey, gggs::CellIndex{}, BathyCell{}),
    std::invalid_argument);
}

TEST(Store, FromCellSizeChoosesAFiniteLevel)
{
  const auto store = BathymetryStore::fromCellSize(30.0f);
  // The chosen level must produce valid cells for a normal position.
  EXPECT_TRUE(store.cellIndex(43.0, -70.5).valid());
}

TEST(Store, ReferenceIsReadOnlyByDefault)
{
  // The prior must be unclobberable by live ingest: set(Reference) throws
  // unless the store was explicitly opened reference_writable (ADR-0002 §D3).
  BathymetryStore store(5);
  EXPECT_FALSE(store.referenceWritable());
  EXPECT_THROW(
    store.set(SourceLayer::Reference, store.cellIndex(43.0, -70.5), BathyCell{-10.0, 3.0}),
    std::logic_error);
  // Other layers are unaffected by the guard.
  const auto cell = store.cellIndex(43.0, -70.5);
  EXPECT_NO_THROW(store.set(SourceLayer::Survey, cell, BathyCell{-12.0, 2.0}));
}

TEST(Store, ReferenceWritableStoreAllowsSet)
{
  // The importer opts in; the converted prior then writes and reads back.
  BathymetryStore store(5, /*reference_writable=*/true);
  EXPECT_TRUE(store.referenceWritable());
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Reference, cell, BathyCell{38.58, 3.0});
  const auto got = store.get(SourceLayer::Reference, cell);
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, 38.58);
}

TEST(Store, FromCellSizePropagatesReferenceWritable)
{
  auto store = BathymetryStore::fromCellSize(30.0f, /*reference_writable=*/true);
  EXPECT_TRUE(store.referenceWritable());
  EXPECT_NO_THROW(
    store.set(SourceLayer::Reference, store.cellIndex(43.0, -70.5), BathyCell{40.0, 3.0}));
}

namespace
{
// Build a one-cell tile map for importTiles from a (cell, value) pair.
std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> oneTile(
  const gggs::CellIndex & cell, const BathyCell & value)
{
  std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
  marine_bathymetry_store::BathymetryTile tile(cell.grid());
  tile.set(cell.row(), cell.column(), value);
  tiles.emplace(cell.grid(), std::move(tile));
  return tiles;
}
}  // namespace

TEST(Store, ImportTilesBulkInsert)
{
  // The importer's bulk-insert path merges a tile map into the layer, marks
  // tiles dirty, and reads back. A second import replaces the tile at that grid
  // (last-write-wins) while leaving other grids untouched.
  BathymetryStore store(5);
  const auto cell = store.cellIndex(43.0, -70.5);
  EXPECT_EQ(store.importTiles(SourceLayer::Survey, oneTile(cell, BathyCell{-30.0, 0.5})), 1u);
  const auto got = store.get(SourceLayer::Survey, cell);
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, -30.0);

  // Re-importing the same grid replaces it (no provenance ordering since #221).
  EXPECT_EQ(store.importTiles(SourceLayer::Survey, oneTile(cell, BathyCell{-28.0, 0.4})), 1u);
  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Survey, cell)->depth, -28.0);
  EXPECT_EQ(tilesIn(store, SourceLayer::Survey), 1u);

  // A grid not in the import is left untouched: import a second, distinct grid.
  const auto other = store.cellIndex(44.0, -71.0);
  ASSERT_FALSE(cell.grid() == other.grid());
  store.importTiles(SourceLayer::Survey, oneTile(other, BathyCell{-15.0, 0.6}));
  EXPECT_EQ(tilesIn(store, SourceLayer::Survey), 2u);
  EXPECT_DOUBLE_EQ(store.get(SourceLayer::Survey, cell)->depth, -28.0);   // first grid intact
}

TEST(Store, ImportTilesRejectsTileKeyMismatch)
{
  // A tile must be built for the grid it is keyed under (harvested #148 fix,
  // preserved across the epoch removal).
  BathymetryStore store(5);
  const auto cell_a = store.cellIndex(43.0, -70.5);
  const auto cell_b = store.cellIndex(44.0, -71.0);
  ASSERT_FALSE(cell_a.grid() == cell_b.grid());

  std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
  // Tile built for grid_b but inserted under grid_a's key.
  marine_bathymetry_store::BathymetryTile mismatched(cell_b.grid());
  tiles.emplace(cell_a.grid(), std::move(mismatched));
  EXPECT_THROW(
    store.importTiles(SourceLayer::Survey, std::move(tiles)),
    std::invalid_argument);
}

TEST(Store, ImportTilesHonorsReferenceReadOnlyGate)
{
  // importTiles is a public mutator like set(), so it must honor the Reference
  // read-only gate (ADR-0002 §D3) — otherwise a bulk import would overwrite the
  // prior on a default store. Only a reference_writable (importer) store may.
  const auto cell = BathymetryStore(5).cellIndex(43.0, -70.5);

  BathymetryStore read_only(5);
  EXPECT_THROW(
    read_only.importTiles(SourceLayer::Reference, oneTile(cell, BathyCell{-10.0, 3.0})),
    std::logic_error);

  BathymetryStore importer(5, /*reference_writable=*/true);
  EXPECT_EQ(
    importer.importTiles(SourceLayer::Reference, oneTile(cell, BathyCell{-10.0, 3.0})), 1u);
  EXPECT_DOUBLE_EQ(importer.get(SourceLayer::Reference, cell)->depth, -10.0);
}

TEST(Store, ImportTilesEmptyIsNoOp)
{
  // An empty import (e.g. an entirely no-data GeoTIFF) inserts nothing and adds
  // no tiles to the layer.
  BathymetryStore store(5);
  EXPECT_EQ(store.importTiles(SourceLayer::Survey, {}), 0u);
  EXPECT_TRUE(store.tiles(SourceLayer::Survey).empty());
}

TEST(Store, ChartIsReadOnlyByDefault)
{
  // Chart is writable only via the regeneration workflow (ADR-0010 D7): both
  // mutators throw on a normal runtime store.
  BathymetryStore store(5);
  EXPECT_FALSE(store.chartStagingWritable());
  EXPECT_THROW(
    store.set(SourceLayer::Chart, store.cellIndex(43.0, -70.5), BathyCell{-20.0, 1.5}),
    std::logic_error);
}

TEST(Store, ChartStagingWritableStoreAllowsSet)
{
  // The staging workflow opts in; a staged chart cell writes and reads back.
  BathymetryStore store(5, /*reference_writable=*/false, /*chart_staging_writable=*/true);
  EXPECT_TRUE(store.chartStagingWritable());
  const auto cell = store.cellIndex(43.0, -70.5);
  store.set(SourceLayer::Chart, cell, BathyCell{-20.0, 1.5});
  const auto got = store.get(SourceLayer::Chart, cell);
  ASSERT_TRUE(got.has_value());
  EXPECT_DOUBLE_EQ(got->depth, -20.0);
}

TEST(Store, FromCellSizePropagatesChartStagingWritable)
{
  auto store = BathymetryStore::fromCellSize(
    30.0f, /*reference_writable=*/false, /*chart_staging_writable=*/true);
  EXPECT_TRUE(store.chartStagingWritable());
  EXPECT_NO_THROW(
    store.set(SourceLayer::Chart, store.cellIndex(43.0, -70.5), BathyCell{-20.0, 1.5}));
}

TEST(Store, ImportTilesHonorsChartGate)
{
  // Bulk import must honor the same Chart gate as set(): a runtime store
  // refuses, a staging store accepts.
  BathymetryStore runtime_store(5);
  const auto cell = runtime_store.cellIndex(43.0, -70.5);
  EXPECT_THROW(
    runtime_store.importTiles(SourceLayer::Chart, oneTile(cell, BathyCell{-20.0, 1.5})),
    std::logic_error);

  BathymetryStore staging(5, /*reference_writable=*/false, /*chart_staging_writable=*/true);
  EXPECT_EQ(staging.importTiles(SourceLayer::Chart, oneTile(cell, BathyCell{-20.0, 1.5})), 1u);
}
