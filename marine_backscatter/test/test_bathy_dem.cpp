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

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_autonomy/gz4d_geo.h"
#include "marine_backscatter/bathy_dem.hpp"
#include "marine_backscatter/seabed_geometry.hpp"
#include "marine_tiled_raster_store/tile_io.hpp"
#include "marine_tiled_raster_store/tiled_raster_tile.hpp"

// [#297] The DEM reader behind sidescan orthorectification, moved here in #304
// where it also became the store-backed seabed::SeabedHeightSampler. Fixtures are written
// with marine_tiled_raster_store::saveTile<double> — the same on-disk contract
// marine_bathymetry_store writes (2-band Float64, band 0 = ellipsoidal height,
// NaN = no data) — so the test takes no marine_bathymetry_store dependency, per
// ADR-0006 D9's "no package dependency, file-level only".

namespace
{

namespace fs = std::filesystem;
namespace mtrs = marine_tiled_raster_store;
namespace mbs = marine_backscatter;
namespace sb = marine_backscatter::seabed;

constexpr int kFineLevel = 13;                 // sidescan native level (~0.11 m)
constexpr int kCoarseLevel = 11;               // typical bathy store level (~0.45 m)
constexpr double kLat = 43.07, kLon = -71.42;  // Lake Massabesic — non-polar

const double kNaN = std::numeric_limits<double>::quiet_NaN();

// A scratch directory unique per test, removed on construction and destruction.
class ScratchDir
{
public:
  explicit ScratchDir(const std::string & name)
  : path_(fs::path(::testing::TempDir()) / ("mbs_dem_" + name))
  {
    fs::remove_all(path_);
    fs::create_directories(path_);
  }
  ~ScratchDir()
  {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  const fs::path & path() const {return path_;}

private:
  fs::path path_;
};

// Write one 2-band Float64 value tile into <root>/<layer>/, band 0 filled from
// @p depth_of(row, col) and band 1 (uncertainty) a constant.
void writeDepthTile(
  const fs::path & root, const std::string & layer, const gggs::GridIndex & grid,
  const std::function<double(uint16_t, uint16_t)> & depth_of)
{
  const fs::path dir = root / layer;
  fs::create_directories(dir);
  mtrs::TiledRasterTile<double> tile(grid, mbs::BathyDem::kBands, kNaN);
  for (uint16_t r = 0; r < tile.edge; ++r) {
    for (uint16_t c = 0; c < tile.edge; ++c) {
      tile.set(r, c, 0, depth_of(r, c));
      tile.set(r, c, 1, 0.25);
    }
  }
  mtrs::saveTile<double>(
    tile, (dir / mtrs::tileFilename(grid)).string(), {kNaN, kNaN});
}

void writeConstantTile(
  const fs::path & root, const std::string & layer, const gggs::GridIndex & grid, double depth)
{
  writeDepthTile(root, layer, grid, [depth](uint16_t, uint16_t) {return depth;});
}

// Geographic centre of the cell containing (@p lat, @p lon) at @p level_n, plus
// the cell's angular spans — CellIndex::position() is the SOUTH-WEST corner.
struct CellGeometry
{
  double centre_lat, centre_lon, lat_span, lon_span;
  gggs::GridIndex grid;
};

// Construct a reader and discard it. A named helper (not an inline temporary)
// so the EXPECT_THROW macro sees ONE argument — its comma is inside parentheses.
void makeDem(const std::string & root, const std::vector<std::string> & layers)
{
  mbs::BathyDem dem(root, layers);
  (void)dem;
}

CellGeometry cellGeometry(int level_n, double lat, double lon)
{
  const gggs::Level level(static_cast<uint8_t>(level_n));
  const gggs::CellIndex cell = level.cellIndex(gggs::geoPoint(lat, lon));
  const auto corner = cell.position();
  CellGeometry g;
  g.grid = cell.grid();
  g.lat_span = cell.grid().latitudinalSpan() / gggs::cell_rows_per_grid;
  g.lon_span = cell.grid().longitudinalSpan() / gggs::cell_columns_per_grid;
  g.centre_lat = corner.latitude + 0.5 * g.lat_span;
  g.centre_lon = corner.longitude + 0.5 * g.lon_span;
  return g;
}

}  // namespace

// A constant-depth store round-trips its value through the reader.
TEST(BathyDem, RoundTripsAKnownDepth)
{
  ScratchDir dir("roundtrip");
  const gggs::Level level(kFineLevel);
  writeConstantTile(dir.path(), "survey", level.gridIndex(kLat, kLon), -12.5);

  mbs::BathyDem dem(dir.path().string(), {"survey", "reference"});
  const auto depth = dem.depthAt(kLat, kLon);
  ASSERT_TRUE(depth.has_value());
  EXPECT_NEAR(*depth, -12.5, 1e-9);
  EXPECT_EQ(dem.tileCount(), 1u);
  EXPECT_EQ(dem.lookupsByLayer().at("survey"), 1u);
}

// ADR-0006 D9 requires the coarser bathy to be INTERPOLATED, not nearest-sampled:
// a query halfway between two cell centres must return their mean.
TEST(BathyDem, BilinearBlendsBetweenCellCentres)
{
  ScratchDir dir("bilinear");
  const gggs::Level level(kFineLevel);
  // Depth ramps by column so the analytic mid-point value is known exactly.
  writeDepthTile(
    dir.path(), "survey", level.gridIndex(kLat, kLon),
    [](uint16_t, uint16_t c) {return -20.0 + static_cast<double>(c) * 0.01;});

  mbs::BathyDem dem(dir.path().string(), {"survey"});
  const auto geom = cellGeometry(kFineLevel, kLat, kLon);
  const gggs::CellIndex cell =
    level.cellIndex(gggs::geoPoint(geom.centre_lat, geom.centre_lon));
  const double here = -20.0 + static_cast<double>(cell.column()) * 0.01;
  const double east = here + 0.01;

  // At the cell centre the stencil collapses to this cell.
  const auto at_centre = dem.depthAt(geom.centre_lat, geom.centre_lon);
  ASSERT_TRUE(at_centre.has_value());
  EXPECT_NEAR(*at_centre, here, 1e-9);

  // Halfway to the eastern neighbour's centre: the exact mean of the two.
  const auto midway = dem.depthAt(geom.centre_lat, geom.centre_lon + 0.5 * geom.lon_span);
  ASSERT_TRUE(midway.has_value());
  EXPECT_NEAR(*midway, 0.5 * (here + east), 1e-9);
}

// A longitude given outside [-180, 180) weighs the same as its wrapped value:
// the blend weights compare the raw longitude with a cell centre, so an
// unwrapped caller must not get a different interpolation.
TEST(BathyDem, UnwrappedLongitudeInterpolatesLikeTheWrappedOne)
{
  ScratchDir dir("unwrapped_lon");
  const gggs::Level level(kFineLevel);
  writeDepthTile(
    dir.path(), "survey", level.gridIndex(kLat, kLon),
    [](uint16_t, uint16_t c) {return -20.0 + static_cast<double>(c) * 0.01;});
  mbs::BathyDem dem(dir.path().string(), {"survey"});
  const auto geom = cellGeometry(kFineLevel, kLat, kLon);
  const double lon = geom.centre_lon + 0.5 * geom.lon_span;

  const auto wrapped = dem.depthAt(geom.centre_lat, lon);
  const auto unwrapped = dem.depthAt(geom.centre_lat, lon + 360.0);
  ASSERT_TRUE(wrapped.has_value());
  ASSERT_TRUE(unwrapped.has_value());
  EXPECT_NEAR(*unwrapped, *wrapped, 1e-9);
  const auto strict = dem.heightAt(geom.centre_lat, lon - 360.0);
  ASSERT_TRUE(strict.valid());
  EXPECT_NEAR(strict.height_m, *wrapped, 1e-9);
}

// One missing neighbour must not throw the interpolation away: the blend is
// renormalised over the cells that ARE present, so a negligible-weight corner
// dropping out barely moves the result (snapping to the nearest cell would
// discard an almost-exact interpolation, and an un-renormalised partial sum would
// bias toward zero).
TEST(BathyDem, PartialStencilRenormalisesRatherThanSnappingToNearest)
{
  ScratchDir dir("partial_stencil");
  const gggs::Level level(kFineLevel);
  const auto geom = cellGeometry(kFineLevel, kLat, kLon);
  const gggs::CellIndex cell =
    level.cellIndex(gggs::geoPoint(geom.centre_lat, geom.centre_lon));
  const uint16_t r0 = cell.row(), c0 = cell.column();
  const auto depth_of = [](uint16_t r, uint16_t c) {
      return -20.0 + static_cast<double>(c) * 0.01 + static_cast<double>(r) * 0.005;
    };
  // Everything ramps except the NE diagonal neighbour, which is no-data.
  writeDepthTile(
    dir.path(), "survey", geom.grid,
    [&](uint16_t r, uint16_t c) {
      return (r == r0 + 1 && c == c0 + 1) ? kNaN : depth_of(r, c);
    });

  mbs::BathyDem dem(dir.path().string(), {"survey"});
  // A tenth of a cell toward the NE corner: the missing diagonal carries weight
  // 0.01 of the stencil, the three present cells 0.99 between them.
  const auto value = dem.depthAt(
    geom.centre_lat + 0.1 * geom.lat_span, geom.centre_lon + 0.1 * geom.lon_span);
  ASSERT_TRUE(value.has_value());
  const double here = depth_of(r0, c0);
  const double expected =
    (0.81 * here + 0.09 * depth_of(r0 + 1, c0) + 0.09 * depth_of(r0, c0 + 1)) / 0.99;
  EXPECT_NEAR(*value, expected, 1e-9);
  // ...and that is NOT the nearest cell's value, which the earlier all-or-nothing
  // fallback returned.
  EXPECT_GT(std::abs(*value - here), 1e-6);
}

// A layer named twice in the search order must be scanned once: otherwise the tile
// count doubles, describe() lists it twice, and two layer entries feed one
// name-keyed lookup counter.
TEST(BathyDem, DuplicateLayerNamesAreScannedOnce)
{
  ScratchDir dir("duplicate_layers");
  const gggs::Level level(kFineLevel);
  writeConstantTile(dir.path(), "survey", level.gridIndex(kLat, kLon), -9.0);

  mbs::BathyDem dem(dir.path().string(), {"survey", "survey"});
  EXPECT_EQ(dem.tileCount(), 1u);
  EXPECT_EQ(dem.layers().size(), 1u);
  ASSERT_EQ(dem.warnings().size(), 1u);
  EXPECT_NE(dem.warnings().front().find("more than once"), std::string::npos)
    << dem.warnings().front();
  // describe() names the layer once, not twice.
  const std::string described = dem.describe();
  EXPECT_EQ(described.find("survey/", described.find("survey/") + 1), std::string::npos)
    << described;

  const auto depth = dem.depthAt(kLat, kLon);
  ASSERT_TRUE(depth.has_value());
  EXPECT_NEAR(*depth, -9.0, 1e-9);
  EXPECT_EQ(dem.lookupsByLayer().at("survey"), 1u);
}

// NaN is the store's no-data sentinel: a cell holding it has no depth to give.
TEST(BathyDem, NoDataCellYieldsNullopt)
{
  ScratchDir dir("nodata");
  const gggs::Level level(kFineLevel);
  writeConstantTile(dir.path(), "survey", level.gridIndex(kLat, kLon), kNaN);

  mbs::BathyDem dem(dir.path().string(), {"survey"});
  EXPECT_FALSE(dem.depthAt(kLat, kLon).has_value());
}

// A position whose grid has no tile on disk is no-coverage, not an error.
TEST(BathyDem, MissingTileYieldsNullopt)
{
  ScratchDir dir("missing_tile");
  const gggs::Level level(kFineLevel);
  writeConstantTile(dir.path(), "survey", level.gridIndex(kLat, kLon), -8.0);

  mbs::BathyDem dem(dir.path().string(), {"survey"});
  EXPECT_TRUE(dem.depthAt(kLat, kLon).has_value());
  EXPECT_FALSE(dem.depthAt(kLat + 1.0, kLon + 1.0).has_value());
}

// A stencil neighbour falling into the adjoining GRID must resolve there rather
// than clamp at the tile edge (the bug that would seam every grid boundary).
TEST(BathyDem, NeighbourAcrossGridBoundaryResolves)
{
  ScratchDir dir("grid_edge");
  const gggs::Level level(kFineLevel);
  const auto west_grid = level.gridIndex(kLat, kLon);
  const auto east_grid = level.gridIndex(kLat, west_grid.eastLongitude() + 1e-7);
  ASSERT_NE(west_grid, east_grid);
  writeConstantTile(dir.path(), "survey", west_grid, -10.0);
  writeConstantTile(dir.path(), "survey", east_grid, -20.0);

  mbs::BathyDem dem(dir.path().string(), {"survey"});
  // Sit on the shared boundary (a hair inside the western grid) at a cell-centre
  // latitude, so the stencil is exactly the two cells straddling the boundary.
  const double boundary_lon = west_grid.eastLongitude();
  const auto geom = cellGeometry(kFineLevel, kLat, boundary_lon - 1e-9);
  const auto blended = dem.depthAt(geom.centre_lat, boundary_lon - 1e-9);
  ASSERT_TRUE(blended.has_value());
  EXPECT_NEAR(*blended, -15.0, 0.05);          // the mean of the two grids
  EXPECT_LT(*blended, -10.5);                  // not clamped to the western value
}

// ADR-0002 permits mixed levels: the finest coverage at the query point wins.
TEST(BathyDem, MultiLevelStorePrefersTheFinerLevel)
{
  ScratchDir dir("multilevel");
  writeConstantTile(
    dir.path(), "survey", gggs::Level(kCoarseLevel).gridIndex(kLat, kLon), -30.0);
  writeConstantTile(
    dir.path(), "survey", gggs::Level(kFineLevel).gridIndex(kLat, kLon), -31.0);

  mbs::BathyDem dem(dir.path().string(), {"survey"});
  ASSERT_EQ(dem.levels().size(), 2u);
  EXPECT_EQ(dem.levels().front(), kFineLevel);   // finest first
  const auto depth = dem.depthAt(kLat, kLon);
  ASSERT_TRUE(depth.has_value());
  EXPECT_NEAR(*depth, -31.0, 1e-9);
}

// The layer search order is the caller's, and a layer with no data at a position
// falls through to the next one rather than reporting no coverage.
TEST(BathyDem, LayerPriorityAndFallthrough)
{
  ScratchDir dir("layers");
  const gggs::Level level(kFineLevel);
  const auto grid = level.gridIndex(kLat, kLon);
  writeConstantTile(dir.path(), "survey", grid, -5.0);
  writeConstantTile(dir.path(), "reference", grid, -6.0);

  {
    mbs::BathyDem dem(dir.path().string(), {"survey", "reference"});
    const auto depth = dem.depthAt(kLat, kLon);
    ASSERT_TRUE(depth.has_value());
    EXPECT_NEAR(*depth, -5.0, 1e-9);
    EXPECT_EQ(dem.lookupsByLayer().at("survey"), 1u);
    EXPECT_EQ(dem.lookupsByLayer().at("reference"), 0u);
  }
  {
    mbs::BathyDem dem(dir.path().string(), {"reference", "survey"});
    const auto depth = dem.depthAt(kLat, kLon);
    ASSERT_TRUE(depth.has_value());
    EXPECT_NEAR(*depth, -6.0, 1e-9);
  }

  // Survey present but empty (all no-data) at the position: fall through.
  ScratchDir hole("layers_hole");
  writeConstantTile(hole.path(), "survey", grid, kNaN);
  writeConstantTile(hole.path(), "reference", grid, -7.0);
  mbs::BathyDem dem(hole.path().string(), {"survey", "reference"});
  const auto depth = dem.depthAt(kLat, kLon);
  ASSERT_TRUE(depth.has_value());
  EXPECT_NEAR(*depth, -7.0, 1e-9);
  EXPECT_EQ(dem.lookupsByLayer().at("reference"), 1u);
}

// An unusable store is a hard failure at construction, never a silent fallback
// to flat-bottom placement for every sample.
TEST(BathyDem, UnusableStoreThrows)
{
  ScratchDir dir("unusable");

  // Root does not exist at all.
  EXPECT_THROW(makeDem((dir.path() / "nope").string(), {"survey"}), std::runtime_error);

  // Root exists, but none of the requested layer directories does.
  EXPECT_THROW(makeDem(dir.path().string(), {"survey"}), std::runtime_error);

  // Layer directory exists but holds no tiles.
  fs::create_directories(dir.path() / "survey");
  EXPECT_THROW(makeDem(dir.path().string(), {"survey"}), std::runtime_error);

  // No layers requested at all.
  EXPECT_THROW(makeDem(dir.path().string(), {}), std::runtime_error);

  // A store whose only tiles are in a layer the caller did not ask for is just as
  // unusable — this is the ADR-0010 D8 survey/ -> processed/ rename case.
  writeConstantTile(dir.path(), "processed", gggs::Level(kFineLevel).gridIndex(kLat, kLon), -3.0);
  EXPECT_THROW(makeDem(dir.path().string(), {"survey"}), std::runtime_error);
  EXPECT_NO_THROW(makeDem(dir.path().string(), {"processed"}));
}

// A requested layer that is absent — or present but tile-less — while another
// requested layer HAS tiles is usable but narrower than the operator asked for.
// It must be reported, never dropped in silence: after ADR-0010 D8's
// survey/ -> processed/ re-classification (+ new draft/), the old `survey,reference`
// default would otherwise quietly orthorectify against the coarse regional layer alone.
TEST(BathyDem, PartiallyMissingLayersWarnRatherThanVanish)
{
  ScratchDir dir("partial");
  const auto grid = gggs::Level(kFineLevel).gridIndex(kLat, kLon);
  writeConstantTile(dir.path(), "reference", grid, -8.0);

  // survey/ absent, reference/ present.
  {
    mbs::BathyDem dem(dir.path().string(), {"survey", "reference"});
    ASSERT_EQ(dem.warnings().size(), 1u);
    EXPECT_NE(dem.warnings()[0].find("survey/"), std::string::npos) << dem.warnings()[0];
    EXPECT_EQ(dem.layers(), std::vector<std::string>{"reference"});
    const auto depth = dem.depthAt(kLat, kLon);
    ASSERT_TRUE(depth.has_value());
    EXPECT_NEAR(*depth, -8.0, 1e-9);
  }

  // survey/ present but holding no value tiles: same treatment.
  fs::create_directories(dir.path() / "survey");
  {
    mbs::BathyDem dem(dir.path().string(), {"survey", "reference"});
    ASSERT_EQ(dem.warnings().size(), 1u);
    EXPECT_NE(dem.warnings()[0].find("no <level>_<row>_<col>.tif"), std::string::npos)
      << dem.warnings()[0];
    EXPECT_EQ(dem.layers(), std::vector<std::string>{"reference"});
  }

  // Everything the caller asked for is there: nothing to say.
  {
    mbs::BathyDem dem(dir.path().string(), {"reference"});
    EXPECT_TRUE(dem.warnings().empty());
  }
}

// ADR-0010 D8: `draft/` is optional-by-design — it starts empty and only live
// CUBE ingest ever creates it, so on an offline or freshly migrated store its
// directory legitimately does not exist. That absence is the expected state, not
// reduced coverage, so the absent-layer warning is suppressed for `draft` (and
// only `draft`) as long as some other requested layer resolved — otherwise the
// new default `processed,draft,reference` would false-alarm the #297 guard on the
// common offline path. A missing NON-draft layer still warns.
TEST(BathyDem, AbsentDraftIsSilentWhenAnotherLayerPresent)
{
  const auto grid = gggs::Level(kFineLevel).gridIndex(kLat, kLon);

  // The common offline case: default `processed,draft,reference` on a store that
  // has processed/ (+ reference/) but no draft/ yet — nothing to warn about.
  {
    ScratchDir dir("draft_absent_ok");
    writeConstantTile(dir.path(), "processed", grid, -9.0);
    writeConstantTile(dir.path(), "reference", grid, -10.0);
    mbs::BathyDem dem(dir.path().string(), {"processed", "draft", "reference"});
    ASSERT_TRUE(dem.warnings().empty())
      << (dem.warnings().empty() ? std::string{} : dem.warnings()[0]);
    EXPECT_EQ(dem.layers(), (std::vector<std::string>{"processed", "reference"}));
    const auto depth = dem.depthAt(kLat, kLon);
    ASSERT_TRUE(depth.has_value());
    EXPECT_NEAR(*depth, -9.0, 1e-9);
  }

  // A missing NON-draft layer is still reported: only draft's absence is benign.
  // (The warning body names both processed/ and draft/, so discriminate on the
  // `'<name>/' does not exist` phrasing rather than a bare substring.)
  {
    ScratchDir dir("nondraft_absent_warns");
    writeConstantTile(dir.path(), "reference", grid, -10.0);
    mbs::BathyDem dem(dir.path().string(), {"processed", "draft", "reference"});
    ASSERT_EQ(dem.warnings().size(), 1u);
    EXPECT_NE(dem.warnings()[0].find("'processed/' does not exist"), std::string::npos)
      << dem.warnings()[0];
    EXPECT_EQ(dem.warnings()[0].find("'draft/' does not exist"), std::string::npos)
      << dem.warnings()[0];
  }
}

// The store's companion rasters (`<level>_<row>_<col>_time.tif` / `_source.tif`,
// pre-#248) carry no depth band. Counting them would inflate the tile count and
// could satisfy the zero-tile hard-fail for a store whose value tiles are all
// gone, while every lookup missed.
TEST(BathyDem, CompanionRastersAreNotValueTiles)
{
  ScratchDir dir("companions");
  const auto grid = gggs::Level(kFineLevel).gridIndex(kLat, kLon);
  const fs::path layer = dir.path() / "survey";
  fs::create_directories(layer);
  const std::string value_name = mtrs::tileFilename(grid);
  const std::string stem = value_name.substr(0, value_name.size() - 4);
  for (const char * suffix : {"_time.tif", "_source.tif"}) {
    std::ofstream companion(layer / (stem + suffix), std::ios::binary);
    companion << "companion raster";
  }

  // Only companions: the store has no value tiles, so construction hard-fails.
  EXPECT_THROW(makeDem(dir.path().string(), {"survey"}), std::runtime_error);

  // With a real value tile alongside them, exactly one tile is counted.
  writeConstantTile(dir.path(), "survey", grid, -4.0);
  mbs::BathyDem dem(dir.path().string(), {"survey"});
  EXPECT_EQ(dem.tileCount(), 1u);
  const auto depth = dem.depthAt(kLat, kLon);
  ASSERT_TRUE(depth.has_value());
  EXPECT_NEAR(*depth, -4.0, 1e-9);
}

TEST(BathyDem, SplitCsvTrimsAndDropsEmpties)
{
  const auto parts = mbs::splitCsv(" survey , reference ,, chart");
  ASSERT_EQ(parts.size(), 3u);
  EXPECT_EQ(parts[0], "survey");
  EXPECT_EQ(parts[1], "reference");
  EXPECT_EQ(parts[2], "chart");
  EXPECT_TRUE(mbs::splitCsv("  ").empty());
}

// The LRU must not change what a lookup returns, only how often a tile is read:
// repeated queries across more grids than the cache holds stay correct.
TEST(BathyDem, TileCacheEvictionPreservesResults)
{
  ScratchDir dir("cache");
  const gggs::Level level(kFineLevel);
  std::vector<gggs::GridIndex> grids;
  std::vector<double> depths;
  double lon = kLon;
  for (int i = 0; i < 3; ++i) {
    const auto grid = level.gridIndex(kLat, lon);
    grids.push_back(grid);
    depths.push_back(-10.0 - i);
    writeConstantTile(dir.path(), "survey", grid, depths.back());
    lon = grid.eastLongitude() + 1e-7;
  }

  mbs::BathyDem dem(dir.path().string(), {"survey"}, 1 /* one resident tile */);
  for (int pass = 0; pass < 2; ++pass) {
    for (std::size_t i = 0; i < grids.size(); ++i) {
      const auto centre = grids[i].southWestPosition();
      const auto depth = dem.depthAt(
        0.5 * (grids[i].northLatitude() + centre.latitude),
        0.5 * (grids[i].eastLongitude() + centre.longitude));
      ASSERT_TRUE(depth.has_value()) << "grid " << i << " pass " << pass;
      EXPECT_NEAR(*depth, depths[i], 1e-9);
    }
  }
}

// ---------------------------------------------------------------------------
// #304: BathyDem as the strict-stencil seabed height sampler
// ---------------------------------------------------------------------------

namespace
{

// The cell at (@p row_off, @p col_off) cells from the south-west corner of the
// level-@p level_n grid containing (kLat, kLon), so a fixture can sit well inside
// one tile (or on its edge) without depending on where kLat/kLon happens to fall.
CellGeometry gridCell(int level_n, double row_off, double col_off)
{
  const gggs::Level level(static_cast<uint8_t>(level_n));
  const gggs::GridIndex grid = level.gridIndex(kLat, kLon);
  const auto sw = grid.southWestPosition();
  const double lat_span = grid.latitudinalSpan() / gggs::cell_rows_per_grid;
  const double lon_span = grid.longitudinalSpan() / gggs::cell_columns_per_grid;
  return cellGeometry(
    level_n, sw.latitude + (row_off + 0.5) * lat_span, sw.longitude + (col_off + 0.5) * lon_span);
}

gggs::CellIndex cellAt(int level_n, double lat, double lon)
{
  return gggs::Level(static_cast<uint8_t>(level_n)).cellIndex(gggs::geoPoint(lat, lon));
}

// Local ENU of (lat, lon) about (lat0, lon0), from gz4d's geodetic -> ECEF -> ENU:
// independent of the metresPerDegree helper the reader and the module share.
gz4d::Point<double> enuOf(double lat0, double lon0, double lat, double lon)
{
  const gz4d::LocalENU frame(gz4d::GeoPointLatLongDegrees(lat0, lon0, 0.0));
  return frame.toLocal(gz4d::GeoPointLatLongDegrees(lat, lon, 0.0));
}

}  // namespace

// The strict lookup never renormalises: a partial stencil at a survey edge is
// kStencilIncomplete (where depthAt still renormalises for placement), and a
// position beyond the edge is kNoData.
TEST(BathyDemSampler, StrictLookupAtASurveyEdge)
{
  ScratchDir dir("strict_edge");
  const auto geom = gridCell(kFineLevel, 480, 480);
  const uint16_t c0 = cellAt(kFineLevel, geom.centre_lat, geom.centre_lon).column();
  const auto ramp = [](uint16_t c) {return -20.0 + 0.01 * static_cast<double>(c);};
  // Survey edge: data up to column c0, nothing east of it.
  writeDepthTile(
    dir.path(), "processed", geom.grid,
    [&](uint16_t, uint16_t c) {return c <= c0 ? ramp(c) : kNaN;});
  mbs::BathyDem dem(dir.path().string(), {"processed"});

  // At the edge cell's centre the east neighbour carries zero weight: complete.
  const auto at_centre = dem.heightAt(geom.centre_lat, geom.centre_lon);
  ASSERT_EQ(at_centre.status, sb::SampleStatus::kValid);
  EXPECT_NEAR(at_centre.height_m, ramp(c0), 1e-9);

  // A quarter cell west: the full bilinear value.
  const auto west = dem.heightAt(geom.centre_lat, geom.centre_lon - 0.25 * geom.lon_span);
  ASSERT_EQ(west.status, sb::SampleStatus::kValid);
  EXPECT_NEAR(west.height_m, ramp(c0) - 0.25 * 0.01, 1e-9);

  // A quarter cell east: the missing neighbour has weight 0.25.
  const double east_lon = geom.centre_lon + 0.25 * geom.lon_span;
  const auto east = dem.heightAt(geom.centre_lat, east_lon);
  EXPECT_EQ(east.status, sb::SampleStatus::kStencilIncomplete);
  EXPECT_TRUE(std::isnan(east.height_m));
  // depthAt is unchanged: it renormalises over the cells present.
  const auto placed = dem.depthAt(geom.centre_lat, east_lon);
  ASSERT_TRUE(placed.has_value());
  EXPECT_NEAR(*placed, ramp(c0), 1e-9);

  // Beyond the edge.
  EXPECT_EQ(
    dem.heightAt(geom.centre_lat, geom.centre_lon + geom.lon_span).status,
    sb::SampleStatus::kNoData);
}

// seabedNormal at a survey edge falls back with kStencilIncomplete instead of
// differencing a renormalised (snapped-toward-nearest) probe; the same point on
// a store without the edge gives a surface normal.
TEST(BathyDemSampler, NormalAtASurveyEdgeFallsBackFlat)
{
  const auto geom = gridCell(kFineLevel, 480, 480);
  const uint16_t c0 = cellAt(kFineLevel, geom.centre_lat, geom.centre_lon).column();
  const auto ramp = [](uint16_t r, uint16_t c) {
      return -20.0 + 0.01 * static_cast<double>(c) + 0.005 * static_cast<double>(r);
    };
  // Centre 1.1 cells west of the edge cell's centre; an explicit step of 1.4
  // east spans puts the east probe 0.3 cells east of c0's centre — its own cell
  // has data, its east neighbour does not.
  const double lat = geom.centre_lat;
  const double lon = geom.centre_lon - 1.1 * geom.lon_span;

  ScratchDir edge_dir("normal_edge");
  writeDepthTile(
    edge_dir.path(), "processed", geom.grid,
    [&](uint16_t r, uint16_t c) {return c <= c0 ? ramp(r, c) : kNaN;});
  mbs::BathyDem edge(edge_dir.path().string(), {"processed"});
  const auto centre = edge.heightAt(lat, lon);
  ASSERT_TRUE(centre.valid());
  ASSERT_LT(centre.span_north_m, 1.4 * centre.span_east_m);   // so 1.4 east spans is unclamped
  sb::NormalOptions options;
  options.step_m = 1.4 * centre.span_east_m;

  const sb::Normal at_edge = sb::seabedNormal(edge, lat, lon, options);
  EXPECT_FALSE(at_edge.from_surface);
  EXPECT_EQ(at_edge.reason, sb::FallbackReason::kStencilIncomplete);
  EXPECT_FALSE(at_edge.step_clamped);

  ScratchDir full_dir("normal_full");
  writeDepthTile(full_dir.path(), "processed", geom.grid, ramp);
  mbs::BathyDem full(full_dir.path().string(), {"processed"});
  const sb::Normal inside = sb::seabedNormal(full, lat, lon, options);
  EXPECT_TRUE(inside.from_surface) << sb::toString(inside.reason);
}

// Two layers at the same level with a 1 m vertical offset: a stencil straddling
// the processed -> reference boundary is a seam, not an ~18 deg slope.
TEST(BathyDemSampler, LayerChangeAcrossTheStencilIsASeam)
{
  ScratchDir dir("layer_seam");
  const auto geom = gridCell(kFineLevel, 480, 480);
  const uint16_t c0 = cellAt(kFineLevel, geom.centre_lat, geom.centre_lon).column();
  writeDepthTile(
    dir.path(), "processed", geom.grid,
    [&](uint16_t, uint16_t c) {return c <= c0 ? -10.0 : kNaN;});
  writeConstantTile(dir.path(), "reference", geom.grid, -11.0);
  mbs::BathyDem dem(dir.path().string(), {"processed", "reference"});

  const double lat = geom.centre_lat;
  const double lon = geom.centre_lon - 0.1 * geom.lon_span;
  const auto centre = dem.heightAt(lat, lon);
  ASSERT_TRUE(centre.valid());
  EXPECT_EQ(centre.source.layer, 0);
  sb::NormalOptions options;
  options.step_m = 1.4 * centre.span_east_m;   // east probe lands in column c0 + 1

  const sb::Normal seam = sb::seabedNormal(dem, lat, lon, options);
  EXPECT_FALSE(seam.from_surface);
  EXPECT_EQ(seam.reason, sb::FallbackReason::kLayerSeam);

  // Five cells further west the whole stencil is in processed/: a surface normal.
  const sb::Normal inside = sb::seabedNormal(dem, lat, lon - 5.0 * geom.lon_span, options);
  EXPECT_TRUE(inside.from_surface) << sb::toString(inside.reason);
  EXPECT_NEAR(inside.normal.u, 1.0, 1e-12);
}

// One layer, a fine patch over a coarse surface at the SAME height: the level
// change alone is a seam (the identity is compared, not the heights).
TEST(BathyDemSampler, LevelChangeAcrossTheStencilIsASeam)
{
  ScratchDir dir("level_seam");
  const auto geom = gridCell(kFineLevel, 480, 480);
  const uint16_t c0 = cellAt(kFineLevel, geom.centre_lat, geom.centre_lon).column();
  writeDepthTile(
    dir.path(), "processed", geom.grid,
    [&](uint16_t, uint16_t c) {return c <= c0 ? -10.0 : kNaN;});
  const gggs::Level coarse(kCoarseLevel);
  writeConstantTile(
    dir.path(), "processed", coarse.gridIndex(geom.centre_lat, geom.centre_lon), -10.0);
  writeConstantTile(
    dir.path(), "processed",
    coarse.gridIndex(geom.centre_lat, geom.centre_lon + 2.0 * geom.lon_span), -10.0);
  mbs::BathyDem dem(dir.path().string(), {"processed"});

  const double lat = geom.centre_lat;
  const double lon = geom.centre_lon - 0.1 * geom.lon_span;
  const auto centre = dem.heightAt(lat, lon);
  ASSERT_TRUE(centre.valid());
  EXPECT_EQ(centre.source.level, kFineLevel);
  const auto beyond = dem.heightAt(lat, geom.centre_lon + 1.5 * geom.lon_span);
  ASSERT_TRUE(beyond.valid());
  EXPECT_EQ(beyond.source.level, kCoarseLevel);

  sb::NormalOptions options;
  options.step_m = 1.4 * centre.span_east_m;
  const sb::Normal seam = sb::seabedNormal(dem, lat, lon, options);
  EXPECT_FALSE(seam.from_surface);
  EXPECT_EQ(seam.reason, sb::FallbackReason::kLayerSeam);
}

namespace
{

// Two adjacent level-kFineLevel tiles, west then east of a meridian, holding ONE
// plane that rises 0.01 m per cell eastward and is constant northward, so the
// store is a single continuous surface split by a tile boundary. Returns the
// slope-aware closed-form normal for that plane through the cell spans.
struct SeamStore
{
  gggs::GridIndex west;
  gggs::GridIndex east;
  double lat;               // a row near the tile's middle.
  double last_lon;          // centre of the western tile's last column.
  double first_lon;         // centre of the eastern tile's first column.
  double boundary_lon;      // the meridian between the tiles.
  double lon_span;
  sb::Enu expected;
};

SeamStore writeSeamStore(
  const fs::path & root, double seed_lat, double seed_lon, bool write_east = true)
{
  const gggs::Level level(kFineLevel);
  SeamStore out;
  out.west = level.gridIndex(seed_lat, seed_lon);
  double east_lon = out.west.eastLongitude() + 1e-7;
  if (east_lon >= 180.0) {
    east_lon -= 360.0;
  }
  out.east = level.gridIndex(seed_lat, east_lon);
  const double kPerCell = 0.01;
  writeDepthTile(
    root, "processed", out.west,
    [kPerCell](uint16_t, uint16_t c) {return -50.0 + kPerCell * c;});
  if (write_east) {
    writeDepthTile(
      root, "processed", out.east,
      [kPerCell](uint16_t, uint16_t c) {
        return -50.0 + kPerCell * (c + gggs::cell_columns_per_grid);
      });
  }
  const auto sw = out.west.southWestPosition();
  out.lon_span = out.west.longitudinalSpan() / gggs::cell_columns_per_grid;
  const double lat_span = out.west.latitudinalSpan() / gggs::cell_rows_per_grid;
  out.lat = sw.latitude + 480.5 * lat_span;
  out.boundary_lon = sw.longitude + gggs::cell_columns_per_grid * out.lon_span;
  out.last_lon = out.boundary_lon - 0.5 * out.lon_span;
  out.first_lon = out.boundary_lon + 0.5 * out.lon_span;
  const sb::MetresPerDegree mpd = sb::metresPerDegree(out.lat);
  const double dh_de = kPerCell / (out.lon_span * mpd.east);
  const double norm = std::sqrt(dh_de * dh_de + 1.0);
  out.expected = sb::Enu{-dh_de / norm, 0.0, 1.0 / norm};
  return out;
}

double angleTo(const sb::Enu & a, const sb::Enu & b)
{
  const double dot = a.e * b.e + a.n * b.n + a.u * b.u;
  return std::acos(std::min(1.0, std::max(-1.0, dot)));
}

}  // namespace

// Within one layer and level the tiles are one continuous surface (reversed
// 2026-11-10): a tilted plane spanning a tile boundary gives the closed-form
// normal on both sides of the seam and exactly on it, and the source identity is
// the same on both sides.
TEST(BathyDemSampler, TiltedPlaneAcrossATileBoundaryGivesTheSurfaceNormal)
{
  ScratchDir dir("tile_seam_plane");
  const SeamStore store = writeSeamStore(dir.path(), kLat, kLon);
  ASSERT_NE(store.west, store.east);
  mbs::BathyDem dem(dir.path().string(), {"processed"});

  const auto west_sample = dem.heightAt(store.lat, store.last_lon);
  const auto east_sample = dem.heightAt(store.lat, store.first_lon);
  ASSERT_TRUE(west_sample.valid());
  ASSERT_TRUE(east_sample.valid());
  EXPECT_EQ(west_sample.source, east_sample.source);

  sb::NormalOptions options;
  options.step_m = 1.4 * west_sample.span_east_m;   // each stencil reaches the other tile.
  for (const double lon : {
    store.last_lon - 0.3 * store.lon_span, store.last_lon, store.boundary_lon,
    store.first_lon, store.first_lon + 0.3 * store.lon_span})
  {
    const sb::Normal result = sb::seabedNormal(dem, store.lat, lon, options);
    ASSERT_TRUE(result.from_surface) << "lon " << lon << " " << sb::toString(result.reason);
    EXPECT_EQ(result.reason, sb::FallbackReason::kNone);
    EXPECT_LT(angleTo(result.normal, store.expected), 1e-4) << "lon " << lon;
  }
  // The default 3-cell step too.
  EXPECT_TRUE(sb::seabedNormal(dem, store.lat, store.boundary_lon).from_surface);
}

// A real source change at the same position is still a layer_seam, and a missing
// neighbour tile is still a fallback (no data), never a surface normal.
TEST(BathyDemSampler, SourceChangeStillSeamsAndMissingTileStillFallsBack)
{
  {
    ScratchDir dir("tile_missing");
    const SeamStore store = writeSeamStore(dir.path(), kLat, kLon, false);
    mbs::BathyDem dem(dir.path().string(), {"processed"});
    sb::NormalOptions options;
    options.step_m = 1.4 * store.lon_span * sb::metresPerDegree(store.lat).east;
    const sb::Normal result = sb::seabedNormal(dem, store.lat, store.last_lon, options);
    EXPECT_FALSE(result.from_surface);
    EXPECT_EQ(result.reason, sb::FallbackReason::kMissingSample);
  }
  {
    // Western tile in "processed", eastern tile only in "draft": the stencil
    // crosses both a tile boundary and a layer change.
    ScratchDir dir("tile_layer_change");
    SeamStore store = writeSeamStore(dir.path(), kLat, kLon, false);
    writeDepthTile(
      dir.path(), "draft", store.east,
      [](uint16_t, uint16_t c) {return -50.0 + 0.01 * (c + gggs::cell_columns_per_grid);});
    mbs::BathyDem dem(dir.path().string(), {"processed", "draft"});
    sb::NormalOptions options;
    options.step_m = 1.4 * store.lon_span * sb::metresPerDegree(store.lat).east;
    const sb::Normal result = sb::seabedNormal(dem, store.lat, store.last_lon, options);
    EXPECT_FALSE(result.from_surface);
    EXPECT_EQ(result.reason, sb::FallbackReason::kLayerSeam);
  }
}

// The same continuity holds across the antimeridian, where the eastern tile's
// longitudes wrap to -180.
TEST(BathyDemSampler, TiltedPlaneAcrossTheAntimeridianGivesTheSurfaceNormal)
{
  ScratchDir dir("antimeridian_plane");
  const gggs::Level level(kFineLevel);
  const gggs::GridIndex seed = level.gridIndex(-16.5, 179.9999);
  const SeamStore store =
    writeSeamStore(dir.path(), -16.5, seed.southWestPosition().longitude + 1e-7);
  ASSERT_GE(store.boundary_lon, 180.0 - 1e-6);
  mbs::BathyDem dem(dir.path().string(), {"processed"});

  sb::NormalOptions options;
  options.step_m = 1.4 * store.lon_span * sb::metresPerDegree(store.lat).east;
  const double lon_east = store.first_lon - 360.0;
  for (const double lon : {store.last_lon, lon_east}) {
    const sb::Normal result = sb::seabedNormal(dem, store.lat, lon, options);
    ASSERT_TRUE(result.from_surface) << "lon " << lon << " " << sb::toString(result.reason);
    EXPECT_LT(angleTo(result.normal, store.expected), 1e-4) << "lon " << lon;
  }
}

// The reported spans are per axis, in metres, and agree with an independent
// geodetic computation; the identity names layer, level and tile.
TEST(BathyDemSampler, ReportsPerAxisSpansAndSourceIdentity)
{
  ScratchDir dir("spans");
  const auto geom = gridCell(kFineLevel, 300, 600);
  writeConstantTile(dir.path(), "reference", geom.grid, -7.0);
  writeConstantTile(dir.path(), "processed", geom.grid, -8.0);
  mbs::BathyDem dem(dir.path().string(), {"processed", "reference"});

  const auto sample = dem.heightAt(geom.centre_lat, geom.centre_lon);
  ASSERT_TRUE(sample.valid());
  EXPECT_NEAR(sample.height_m, -8.0, 1e-9);

  const double north_m =
    enuOf(
    geom.centre_lat, geom.centre_lon, geom.centre_lat + 0.5 * geom.lat_span,
    geom.centre_lon)[1] -
    enuOf(
    geom.centre_lat, geom.centre_lon, geom.centre_lat - 0.5 * geom.lat_span,
    geom.centre_lon)[1];
  const double east_m =
    enuOf(
    geom.centre_lat, geom.centre_lon, geom.centre_lat,
    geom.centre_lon + 0.5 * geom.lon_span)[0] -
    enuOf(
    geom.centre_lat, geom.centre_lon, geom.centre_lat,
    geom.centre_lon - 0.5 * geom.lon_span)[0];
  EXPECT_NEAR(sample.span_north_m, north_m, 1e-6 * north_m);
  EXPECT_NEAR(sample.span_east_m, east_m, 1e-6 * east_m);
  // Square in degrees, not in metres: at 43N east-west is ~0.73 of north-south.
  EXPECT_LT(sample.span_east_m, 0.8 * sample.span_north_m);

  EXPECT_EQ(sample.source.layer, 0);
  EXPECT_EQ(sample.source.level, kFineLevel);
  EXPECT_EQ(sample.source.tile, 0u);   // tiles of one layer and level are one source.
}

// ADR-0011 s.2 / ADR-0013 D8: depth overviews fold to the shallowest value and
// are never a source of heights. A hole in the fine tile stays a hole even with
// a valued overview tile under it, and naming the sidecar as a layer is refused.
TEST(BathyDemSampler, OverviewsAreNeverRead)
{
  ScratchDir dir("overviews");
  const auto geom = gridCell(kFineLevel, 480, 480);
  writeConstantTile(dir.path(), "processed", geom.grid, kNaN);
  for (const int level_n : {kFineLevel - 1, kCoarseLevel}) {
    const gggs::Level level(static_cast<uint8_t>(level_n));
    writeConstantTile(
      dir.path() / "processed", "overviews", level.gridIndex(geom.centre_lat, geom.centre_lon),
      -5.0);
  }
  mbs::BathyDem dem(dir.path().string(), {"processed"});
  EXPECT_EQ(dem.tileCount(), 1u);
  EXPECT_EQ(dem.heightAt(geom.centre_lat, geom.centre_lon).status, sb::SampleStatus::kNoData);
  EXPECT_FALSE(dem.depthAt(geom.centre_lat, geom.centre_lon).has_value());
  EXPECT_EQ(
    sb::seabedNormal(dem, geom.centre_lat, geom.centre_lon).reason,
    sb::FallbackReason::kMissingSample);

  for (const char * bad : {"overviews", "processed/overviews", "overviews.old", "", ".", ".."}) {
    EXPECT_THROW(makeDem(dir.path().string(), {"processed", bad}), std::invalid_argument)
      << "layer name '" << bad << "'";
  }
}

// End to end through the SeabedHeightSampler base: a tilted store gives the
// analytic normal. The expected gradient comes from gz4d's geodetic ENU, not
// from the module's radius helper.
TEST(BathyDemSampler, SeabedNormalOnATiltedStore)
{
  ScratchDir dir("tilted");
  const auto geom = gridCell(kFineLevel, 480, 480);
  constexpr double kPerColumn = 0.01;
  constexpr double kPerRow = 0.005;
  writeDepthTile(
    dir.path(), "processed", geom.grid, [](uint16_t r, uint16_t c) {
      return -20.0 + kPerColumn * static_cast<double>(c) + kPerRow * static_cast<double>(r);
    });
  mbs::BathyDem dem(dir.path().string(), {"processed"});

  const double lat = geom.centre_lat + 0.3 * geom.lat_span;
  const double lon = geom.centre_lon + 0.2 * geom.lon_span;
  sb::SeabedHeightSampler & sampler = dem;
  const sb::Normal result = sb::seabedNormal(sampler, lat, lon);
  ASSERT_TRUE(result.from_surface) << sb::toString(result.reason);
  const auto centre = dem.heightAt(lat, lon);
  EXPECT_NEAR(result.step_used_m, 3.0 * centre.span_north_m, 1e-12);

  const double metres_per_column = enuOf(lat, lon, lat, lon + geom.lon_span)[0];
  const double metres_per_row = enuOf(lat, lon, lat + geom.lat_span, lon)[1];
  const int row_step = static_cast<int>(cellAt(kFineLevel, lat + geom.lat_span, lon).row()) -
    static_cast<int>(cellAt(kFineLevel, lat, lon).row());
  ASSERT_EQ(std::abs(row_step), 1);
  const double dh_de = kPerColumn / metres_per_column;
  const double dh_dn = kPerRow * row_step / metres_per_row;
  const double norm = std::sqrt(dh_de * dh_de + dh_dn * dh_dn + 1.0);
  EXPECT_NEAR(result.normal.e, -dh_de / norm, 1e-6);
  EXPECT_NEAR(result.normal.n, -dh_dn / norm, 1e-6);
  EXPECT_NEAR(result.normal.u, 1.0 / norm, 1e-6);
}

// A corrupt tile is a fault, and the fault reaches the caller of seabedNormal
// rather than becoming a flat fallback.
TEST(BathyDemSampler, CorruptTileThrowsThroughSeabedNormal)
{
  ScratchDir dir("corrupt");
  const auto geom = gridCell(kFineLevel, 480, 480);
  const fs::path layer = dir.path() / "processed";
  fs::create_directories(layer);
  {
    std::ofstream junk(layer / mtrs::tileFilename(geom.grid), std::ios::binary);
    junk << "not a GeoTIFF";
  }
  mbs::BathyDem dem(dir.path().string(), {"processed"});
  EXPECT_THROW(
    sb::seabedNormal(dem, geom.centre_lat, geom.centre_lon), std::runtime_error);
}
