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
#include <functional>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_sidescan_mosaic/bathy_dem.hpp"
#include "marine_tiled_raster_store/tile_io.hpp"
#include "marine_tiled_raster_store/tiled_raster_tile.hpp"

// [#297] The DEM reader behind sidescan orthorectification. Fixtures are written
// with marine_tiled_raster_store::saveTile<double> — the same on-disk contract
// marine_bathymetry_store writes (2-band Float64, band 0 = ellipsoidal height,
// NaN = no data) — so the test takes no marine_bathymetry_store dependency, per
// ADR-0006 D9's "no package dependency, file-level only".

namespace
{

namespace fs = std::filesystem;
namespace mtrs = marine_tiled_raster_store;
namespace msm = marine_sidescan_mosaic;

constexpr int kFineLevel = 13;                 // sidescan native level (~0.11 m)
constexpr int kCoarseLevel = 11;               // typical bathy store level (~0.45 m)
constexpr double kLat = 43.07, kLon = -71.42;  // Lake Massabesic — non-polar

const double kNaN = std::numeric_limits<double>::quiet_NaN();

// A scratch directory unique per test, removed on construction and destruction.
class ScratchDir
{
public:
  explicit ScratchDir(const std::string & name)
  : path_(fs::path(::testing::TempDir()) / ("msm_dem_" + name))
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
  mtrs::TiledRasterTile<double> tile(grid, msm::BathyDem::kBands, kNaN);
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
  msm::BathyDem dem(root, layers);
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

  msm::BathyDem dem(dir.path().string(), {"survey", "reference"});
  const auto depth = dem.depthAt(kLat, kLon);
  ASSERT_TRUE(depth.has_value());
  EXPECT_NEAR(*depth, -12.5, 1e-9);
  EXPECT_EQ(dem.tileCount(), 1u);
  EXPECT_EQ(dem.hitsByLayer().at("survey"), 1u);
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

  msm::BathyDem dem(dir.path().string(), {"survey"});
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

// NaN is the store's no-data sentinel: a cell holding it has no depth to give.
TEST(BathyDem, NoDataCellYieldsNullopt)
{
  ScratchDir dir("nodata");
  const gggs::Level level(kFineLevel);
  writeConstantTile(dir.path(), "survey", level.gridIndex(kLat, kLon), kNaN);

  msm::BathyDem dem(dir.path().string(), {"survey"});
  EXPECT_FALSE(dem.depthAt(kLat, kLon).has_value());
}

// A position whose grid has no tile on disk is no-coverage, not an error.
TEST(BathyDem, MissingTileYieldsNullopt)
{
  ScratchDir dir("missing_tile");
  const gggs::Level level(kFineLevel);
  writeConstantTile(dir.path(), "survey", level.gridIndex(kLat, kLon), -8.0);

  msm::BathyDem dem(dir.path().string(), {"survey"});
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

  msm::BathyDem dem(dir.path().string(), {"survey"});
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

  msm::BathyDem dem(dir.path().string(), {"survey"});
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
    msm::BathyDem dem(dir.path().string(), {"survey", "reference"});
    const auto depth = dem.depthAt(kLat, kLon);
    ASSERT_TRUE(depth.has_value());
    EXPECT_NEAR(*depth, -5.0, 1e-9);
    EXPECT_EQ(dem.hitsByLayer().at("survey"), 1u);
    EXPECT_EQ(dem.hitsByLayer().at("reference"), 0u);
  }
  {
    msm::BathyDem dem(dir.path().string(), {"reference", "survey"});
    const auto depth = dem.depthAt(kLat, kLon);
    ASSERT_TRUE(depth.has_value());
    EXPECT_NEAR(*depth, -6.0, 1e-9);
  }

  // Survey present but empty (all no-data) at the position: fall through.
  ScratchDir hole("layers_hole");
  writeConstantTile(hole.path(), "survey", grid, kNaN);
  writeConstantTile(hole.path(), "reference", grid, -7.0);
  msm::BathyDem dem(hole.path().string(), {"survey", "reference"});
  const auto depth = dem.depthAt(kLat, kLon);
  ASSERT_TRUE(depth.has_value());
  EXPECT_NEAR(*depth, -7.0, 1e-9);
  EXPECT_EQ(dem.hitsByLayer().at("reference"), 1u);
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
  // unusable — this is the ADR-0010 D3 survey/ -> processed/ rename case.
  writeConstantTile(dir.path(), "processed", gggs::Level(kFineLevel).gridIndex(kLat, kLon), -3.0);
  EXPECT_THROW(makeDem(dir.path().string(), {"survey"}), std::runtime_error);
  EXPECT_NO_THROW(makeDem(dir.path().string(), {"processed"}));
}

// A requested layer that is absent — or present but tile-less — while another
// requested layer HAS tiles is usable but narrower than the operator asked for.
// It must be reported, never dropped in silence: after ADR-0010 D3's
// survey/ -> processed/ re-classification, `survey,reference` would otherwise
// quietly orthorectify against the coarse regional layer alone.
TEST(BathyDem, PartiallyMissingLayersWarnRatherThanVanish)
{
  ScratchDir dir("partial");
  const auto grid = gggs::Level(kFineLevel).gridIndex(kLat, kLon);
  writeConstantTile(dir.path(), "reference", grid, -8.0);

  // survey/ absent, reference/ present.
  {
    msm::BathyDem dem(dir.path().string(), {"survey", "reference"});
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
    msm::BathyDem dem(dir.path().string(), {"survey", "reference"});
    ASSERT_EQ(dem.warnings().size(), 1u);
    EXPECT_NE(dem.warnings()[0].find("no <level>_<row>_<col>.tif"), std::string::npos)
      << dem.warnings()[0];
    EXPECT_EQ(dem.layers(), std::vector<std::string>{"reference"});
  }

  // Everything the caller asked for is there: nothing to say.
  {
    msm::BathyDem dem(dir.path().string(), {"reference"});
    EXPECT_TRUE(dem.warnings().empty());
  }
}

TEST(BathyDem, SplitCsvTrimsAndDropsEmpties)
{
  const auto parts = msm::splitCsv(" survey , reference ,, chart");
  ASSERT_EQ(parts.size(), 3u);
  EXPECT_EQ(parts[0], "survey");
  EXPECT_EQ(parts[1], "reference");
  EXPECT_EQ(parts[2], "chart");
  EXPECT_TRUE(msm::splitCsv("  ").empty());
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

  msm::BathyDem dem(dir.path().string(), {"survey"}, 1 /* one resident tile */);
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
