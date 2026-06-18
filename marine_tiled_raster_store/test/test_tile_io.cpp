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
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_tiled_raster_store/tile_io.hpp"
#include "marine_tiled_raster_store/tiled_raster_tile.hpp"

namespace mtrs = marine_tiled_raster_store;

namespace
{

// A scratch directory unique per test, removed on construction and destruction.
class ScratchDir
{
public:
  explicit ScratchDir(const std::string & name)
  : path_(std::filesystem::path(::testing::TempDir()) / ("mtrs_" + name))
  {
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  ~ScratchDir() {std::filesystem::remove_all(path_);}
  const std::filesystem::path & path() const {return path_;}

private:
  std::filesystem::path path_;
};

// A grid near Lake Massabesic, away from the polar scaling boundaries.
gggs::GridIndex sampleGrid(const gggs::Level & level)
{
  return level.gridIndex(43.07, -71.42);
}

}  // namespace

// A single-band uint16 tile (the sidescan-mosaic shape, #173) round-trips
// through GeoTIFF: values and recovered GridIndex survive.
TEST(TiledRasterTileIo, RoundTripUint16SingleBand)
{
  ScratchDir dir("u16_single");
  const gggs::Level level(13);                       // ~0.11 m cells
  const gggs::GridIndex grid = sampleGrid(level);
  const std::string path = (dir.path() / mtrs::tileFilename(grid)).string();

  mtrs::TiledRasterTile<std::uint16_t> tile(grid, 1, 0);
  tile.set(0, 0, 0, 7);
  tile.set(100, 200, 0, 4242);
  tile.set(tile.edge - 1, tile.edge - 1, 0, 65535);

  mtrs::saveTile<std::uint16_t>(tile, path, {std::nullopt});
  const auto loaded = mtrs::loadTile<std::uint16_t>(path, level, 1);

  EXPECT_EQ(loaded.index(), grid);
  EXPECT_EQ(loaded.bandCount(), 1u);
  EXPECT_EQ(loaded.get(0, 0, 0), 7);
  EXPECT_EQ(loaded.get(100, 200, 0), 4242);
  EXPECT_EQ(loaded.get(tile.edge - 1, tile.edge - 1, 0), 65535);
  EXPECT_FALSE(loaded.dirty());
}

// The bathymetry shape (3-band Float64, NaN no-data on two bands) round-trips,
// confirming the generalization preserves the existing persistence behavior.
TEST(TiledRasterTileIo, RoundTripDoubleThreeBand)
{
  ScratchDir dir("f64_three");
  const gggs::Level level(11);                       // ~0.45 m cells
  const gggs::GridIndex grid = sampleGrid(level);
  const std::string path = (dir.path() / mtrs::tileFilename(grid)).string();

  const double nan = std::numeric_limits<double>::quiet_NaN();
  mtrs::TiledRasterTile<double> tile(grid, std::vector<double>{nan, nan, 0.0});
  tile.set(10, 20, 0, -12.5);     // depth (ellipsoidal height)
  tile.set(10, 20, 1, 0.3);       // uncertainty
  tile.set(10, 20, 2, 1.78e9);    // timestamp

  mtrs::saveTile<double>(tile, path, {nan, nan, std::nullopt});
  const auto loaded = mtrs::loadTile<double>(path, level, 3);

  EXPECT_EQ(loaded.index(), grid);
  EXPECT_DOUBLE_EQ(loaded.get(10, 20, 0), -12.5);
  EXPECT_DOUBLE_EQ(loaded.get(10, 20, 1), 0.3);
  EXPECT_DOUBLE_EQ(loaded.get(10, 20, 2), 1.78e9);
  EXPECT_TRUE(std::isnan(loaded.get(0, 0, 0)));     // untouched cell stays no-data
  EXPECT_DOUBLE_EQ(loaded.get(0, 0, 2), 0.0);       // timestamp band fill
}

// A tile saved at one level is rejected when loaded at a different level (its
// geotransform won't match any grid there).
TEST(TiledRasterTileIo, LevelMismatchRejected)
{
  ScratchDir dir("level_mismatch");
  const gggs::Level saved_level(13);
  const gggs::GridIndex grid = sampleGrid(saved_level);
  const std::string path = (dir.path() / mtrs::tileFilename(grid)).string();

  mtrs::TiledRasterTile<std::uint16_t> tile(grid, 1, 0);
  tile.set(0, 0, 0, 1);
  mtrs::saveTile<std::uint16_t>(tile, path, {std::nullopt});

  const gggs::Level wrong_level(12);
  EXPECT_THROW(mtrs::loadTile<std::uint16_t>(path, wrong_level, 1), std::runtime_error);
}

// band_nodata must describe every band, or saveTile rejects the call.
TEST(TiledRasterTileIo, BandNodataSizeMismatchThrows)
{
  ScratchDir dir("nodata_size");
  const gggs::Level level(13);
  const gggs::GridIndex grid = sampleGrid(level);
  const std::string path = (dir.path() / mtrs::tileFilename(grid)).string();

  mtrs::TiledRasterTile<double> tile(grid, 3, 0.0);
  // Two entries for a three-band tile.
  EXPECT_THROW(
    mtrs::saveTile<double>(tile, path, {std::nullopt, std::nullopt}),
    std::invalid_argument);
}

// saveTiles writes only dirty tiles and clears their flags; loadTiles recovers
// exactly the tiles that were written.
TEST(TiledRasterTileIo, SaveTilesWritesOnlyDirty)
{
  ScratchDir dir("dirty_only");
  const gggs::Level level(13);
  const gggs::GridIndex grid_a = level.gridIndex(43.07, -71.42);
  const gggs::GridIndex grid_b = level.gridIndex(43.50, -71.10);
  ASSERT_FALSE(grid_a == grid_b);

  std::map<gggs::GridIndex, mtrs::TiledRasterTile<std::uint16_t>> tiles;
  tiles.emplace(grid_a, mtrs::TiledRasterTile<std::uint16_t>(grid_a, 1, 0));
  tiles.emplace(grid_b, mtrs::TiledRasterTile<std::uint16_t>(grid_b, 1, 0));
  tiles.at(grid_a).set(1, 1, 0, 11);   // dirty
  tiles.at(grid_b).set(2, 2, 0, 22);
  tiles.at(grid_b).clearDirty();       // pretend already persisted

  EXPECT_EQ(mtrs::saveTiles<std::uint16_t>(tiles, dir.path().string(), {std::nullopt}), 1u);
  EXPECT_FALSE(tiles.at(grid_a).dirty());   // cleared by the write

  std::map<gggs::GridIndex, mtrs::TiledRasterTile<std::uint16_t>> reloaded;
  EXPECT_EQ(mtrs::loadTiles<std::uint16_t>(reloaded, dir.path().string(), level, 1), 1u);
  ASSERT_EQ(reloaded.count(grid_a), 1u);
  EXPECT_EQ(reloaded.at(grid_a).get(1, 1, 0), 11);
  EXPECT_EQ(reloaded.count(grid_b), 0u);    // clean tile was never written
}
