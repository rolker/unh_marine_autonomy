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

/// @file
/// @brief Subprocess test of the built `import_geotiff` binary.
///
/// The library round-trip in `test_geotiff_import.cpp` never exercises `main()`.
/// This suite runs the actual binary (path injected via the
/// `IMPORT_GEOTIFF_BINARY` compile definition) to cover the arg-parsing paths
/// that are otherwise untested: `--stage` / `--commit` mode selection,
/// mode-dependent positional counts, the "layer must be chart with --stage"
/// usage error, and missing-operand handling (plan.md, review suggestion 3).

#include <gtest/gtest.h>

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <sys/wait.h>

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "marine_autonomy/gggs.h"

namespace fs = std::filesystem;

namespace
{

/// Write a small north-up WGS84 GeoTIFF at the NW corner of the level-@p level
/// grid containing (43.0, -70.5): band 1 = depth, band 2 = uncertainty.
void writeFixtureTiff(const fs::path & path, uint8_t level, int width, int height)
{
  const gggs::Level lvl(level);
  const gggs::GridIndex grid = lvl.gridIndex(43.0, -70.5);
  const double cell_x = grid.longitudinalSpan() / gggs::cell_rows_per_grid;
  const double cell_y = grid.latitudinalSpan() / gggs::cell_rows_per_grid;

  std::vector<float> depth(static_cast<std::size_t>(width) * height);
  std::vector<float> unc(depth.size());
  for (std::size_t i = 0; i < depth.size(); ++i) {
    depth[i] = -30.0f - static_cast<float>(i);
    unc[i] = 1.5f;
  }

  GDALAllRegister();
  GDALDriver * driver = GetGDALDriverManager()->GetDriverByName("GTiff");
  if (driver == nullptr) {
    throw std::runtime_error("writeFixtureTiff: GTiff driver unavailable");
  }
  GDALDataset * ds =
    driver->Create(path.string().c_str(), width, height, 2, GDT_Float32, nullptr);
  if (ds == nullptr) {
    throw std::runtime_error("writeFixtureTiff: could not create " + path.string());
  }
  double gt[6] = {grid.westLongitude(), cell_x, 0.0, grid.northLatitude(), 0.0, -cell_y};
  ds->SetGeoTransform(gt);
  OGRSpatialReference srs;
  srs.SetWellKnownGeogCS("WGS84");
  char * wkt = nullptr;
  srs.exportToWkt(&wkt);
  ds->SetProjection(wkt);
  CPLFree(wkt);
  if (ds->GetRasterBand(1)->RasterIO(
      GF_Write, 0, 0, width, height, depth.data(), width, height, GDT_Float32, 0, 0) != CE_None ||
    ds->GetRasterBand(2)->RasterIO(
      GF_Write, 0, 0, width, height, unc.data(), width, height, GDT_Float32, 0, 0) != CE_None)
  {
    GDALClose(ds);
    throw std::runtime_error("writeFixtureTiff: band write failed");
  }
  GDALClose(ds);
}

}  // namespace

class ImportGeotiffCliTest : public ::testing::Test
{
protected:
  fs::path dir_;
  fs::path tif_;

  void SetUp() override
  {
    const std::string name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
    dir_ = fs::temp_directory_path() / ("marine_bathy_cli_" + name);
    fs::remove_all(dir_);
    fs::create_directories(dir_);
    tif_ = dir_ / "fixture.tif";
    writeFixtureTiff(tif_, 11, 2, 2);
  }

  void TearDown() override
  {
    fs::remove_all(dir_);
  }

  /// Run the binary with @p args (space-joined), stdout+stderr redirected to a
  /// per-call log under dir_. Returns the process exit status, or -1 if the
  /// process did not exit normally.
  int run(const std::string & args)
  {
    const std::string log = (dir_ / "cli.log").string();
    const std::string cmd =
      std::string(IMPORT_GEOTIFF_BINARY) + " " + args + " > \"" + log + "\" 2>&1";
    const int rc = std::system(cmd.c_str());
    if (rc == -1 || !WIFEXITED(rc)) {
      return -1;
    }
    return WEXITSTATUS(rc);
  }

  std::size_t countTiles(const fs::path & layer_dir)
  {
    std::size_t n = 0;
    if (!fs::is_directory(layer_dir)) {
      return 0;
    }
    for (const auto & entry : fs::directory_iterator(layer_dir)) {
      if (entry.path().extension() == ".tif") {
        ++n;
      }
    }
    return n;
  }
};

TEST_F(ImportGeotiffCliTest, StageThenCommitChartLandsTiles)
{
  const fs::path staged = dir_ / "staged";
  const fs::path store = dir_ / "store";
  fs::create_directories(store);

  // --stage chart: builds the chart layer under <staged>/chart, live store
  // untouched.
  EXPECT_EQ(
    run("--stage \"" + staged.string() + "\" chart \"" + tif_.string() + "\" --level 11"), 0);
  EXPECT_EQ(countTiles(staged / "chart"), 1u);
  EXPECT_FALSE(fs::exists(store / "chart"));

  // --commit: atomic swap of the staged chart layer into the store.
  EXPECT_EQ(
    run("--commit \"" + staged.string() + "\" \"" + store.string() + "\""), 0);
  EXPECT_EQ(countTiles(store / "chart"), 1u);
}

TEST_F(ImportGeotiffCliTest, StageRejectsNonChartLayer)
{
  // survey/reference are direct-to-store imports; --stage is chart-only.
  const fs::path staged = dir_ / "staged";
  EXPECT_NE(
    run("--stage \"" + staged.string() + "\" survey \"" + tif_.string() + "\" --level 11"), 0);
  EXPECT_FALSE(fs::exists(staged / "chart"));
}

TEST_F(ImportGeotiffCliTest, StageWithoutOperandIsUsageError)
{
  // --stage consumes the staged dir as its operand; nothing follows -> usage.
  EXPECT_NE(run("--stage"), 0);
}

TEST_F(ImportGeotiffCliTest, CommitWithoutStoreDirIsUsageError)
{
  // --commit needs exactly one positional (<store_dir>) after its staged-dir
  // operand.
  const fs::path staged = dir_ / "staged";
  EXPECT_NE(run("--commit \"" + staged.string() + "\""), 0);
}
