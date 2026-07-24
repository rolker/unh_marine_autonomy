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
/// @brief Offline end-to-end test of the S-102 import pipeline (#278):
/// fixture catalog + synthetic projected tile → temp store, including the
/// whole-pipeline idempotency contract (re-run is a no-op; --force is not).

#include <gtest/gtest.h>

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/tile_io.hpp"
#include "s102/fetch.hpp"
#include "s102/run.hpp"

namespace fs = std::filesystem;
namespace s102 = marine_bathymetry_store::s102;
using marine_bathymetry_store::BathymetryStore;
using marine_bathymetry_store::SourceLayer;

namespace
{

constexpr double kNoData = 1.0e6;
// Fixture tile footprint: 4x4 pixels of 100 m in UTM 19N near the Shoals.
constexpr double kOriginX = 365000.;
constexpr double kOriginY = 4759000.;

std::string makeFixtureTile(const fs::path & path, double depth_value)
{
  GDALAllRegister();
  GDALDriver * gtiff = GetGDALDriverManager()->GetDriverByName("GTiff");
  auto ds = std::unique_ptr<GDALDataset>(gtiff->Create(
      path.string().c_str(), 4, 4, 2, GDT_Float32, nullptr));
  OGRSpatialReference srs;
  srs.importFromEPSG(32619);
  char * wkt = nullptr;
  srs.exportToWkt(&wkt);
  ds->SetProjection(wkt);
  CPLFree(wkt);
  double gt[6] = {kOriginX, 100., 0., kOriginY, 0., -100.};
  ds->SetGeoTransform(gt);
  ds->SetMetadataItem("VERTICAL_DATUM_ABBREV", "MLLW");
  std::vector<float> depth(16, static_cast<float>(depth_value));
  std::vector<float> uncertainty(16, 0.5f);
  GDALRasterBand * b1 = ds->GetRasterBand(1);
  b1->SetDescription("depth");
  b1->SetNoDataValue(kNoData);
  b1->RasterIO(GF_Write, 0, 0, 4, 4, depth.data(), 4, 4, GDT_Float32, 0, 0);
  GDALRasterBand * b2 = ds->GetRasterBand(2);
  b2->SetDescription("uncertainty");
  b2->SetNoDataValue(kNoData);
  b2->RasterIO(
    GF_Write, 0, 0, 4, 4, uncertainty.data(), 4, 4, GDT_Float32, 0, 0);
  return path.string();
}

void makeFixtureCatalog(
  const fs::path & gpkg, const std::string & tile_url,
  const std::string & sha, const std::string & issuance)
{
  GDALAllRegister();
  fs::remove(gpkg);
  GDALDriver * driver = GetGDALDriverManager()->GetDriverByName("GPKG");
  auto ds = std::unique_ptr<GDALDataset>(
    driver->Create(gpkg.string().c_str(), 0, 0, 0, GDT_Unknown, nullptr));
  OGRSpatialReference srs;
  srs.importFromEPSG(4326);
  srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
  OGRLayer * layer = ds->CreateLayer("tiles", &srs, wkbPolygon, nullptr);
  for (const char * name :
    {"TILE_ID", "S102V30", "S102V30_SHA256", "Resolution", "ISSUANCE"})
  {
    OGRFieldDefn field(name, OFTString);
    layer->CreateField(&field);
  }
  auto feature = std::unique_ptr<OGRFeature>(
    OGRFeature::CreateFeature(layer->GetLayerDefn()));
  feature->SetField("TILE_ID", "US5TEST1");
  feature->SetField("S102V30", tile_url.c_str());
  feature->SetField("S102V30_SHA256", sha.c_str());
  feature->SetField("Resolution", "16m");
  feature->SetField("ISSUANCE", issuance.c_str());
  // Roughly the fixture tile's geographic footprint (Shoals area).
  OGRLinearRing ring;
  ring.addPoint(-70.66, 42.95);
  ring.addPoint(-70.64, 42.95);
  ring.addPoint(-70.64, 42.97);
  ring.addPoint(-70.66, 42.97);
  ring.addPoint(-70.66, 42.95);
  OGRPolygon polygon;
  polygon.addRing(&ring);
  feature->SetGeometry(&polygon);
  ASSERT_EQ(layer->CreateFeature(feature.get()), OGRERR_NONE);
}

class S102RunTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    root_ = fs::temp_directory_path() / "s102_run_test";
    fs::remove_all(root_);
    fs::create_directories(root_ / "remote");

    tile_path_ = makeFixtureTile(root_ / "remote" / "US5TEST1.h5", 5.0);
    catalog_ = (root_ / "catalog.gpkg");
    makeFixtureCatalog(
      catalog_, tile_path_, s102::sha256File(tile_path_), "2026-01-01");

    options_.area = {-70.70, 42.90, -70.55, 43.05};
    options_.store_dir = (root_ / "store").string();
    options_.cache_dir = (root_ / "cache").string();
    options_.catalog = catalog_.string();
    options_.layer = SourceLayer::Reference;
    options_.datum = &datum_;
  }
  void TearDown() override {fs::remove_all(root_);}

  fs::path root_;
  fs::path catalog_;
  std::string tile_path_;
  s102::ConstantOffsetProvider datum_{-28.0};
  s102::S102ImportOptions options_;
};

}  // namespace

TEST_F(S102RunTest, end_to_end_then_noop_then_force)
{
  const auto first = s102::runS102Import(options_);
  EXPECT_EQ(first.discovered, 1u);
  EXPECT_EQ(first.fetched, 1u);
  EXPECT_EQ(first.converted, 1u);
  EXPECT_EQ(first.skipped, 0u);
  EXPECT_GT(first.imported_cells, 0u);

  // The store on disk holds Reference tiles with the shifted height.
  auto store = BathymetryStore::fromCellSize(0.5f, true);
  ASSERT_GT(marine_bathymetry_store::load(store, options_.store_dir), 0u);
  EXPECT_FALSE(store.tiles(SourceLayer::Reference).empty());
  EXPECT_TRUE(fs::exists(fs::path(options_.store_dir) / "s102_imported.json"));

  // Issue acceptance (#278): re-run at unchanged issuance is a no-op.
  const auto second = s102::runS102Import(options_);
  EXPECT_EQ(second.discovered, 1u);
  EXPECT_EQ(second.skipped, 1u);
  EXPECT_EQ(second.fetched, 0u);
  EXPECT_EQ(second.converted, 0u);
  EXPECT_EQ(second.imported_cells, 0u);

  // --force re-runs convert + import.
  options_.force = true;
  const auto forced = s102::runS102Import(options_);
  EXPECT_EQ(forced.converted, 1u);
  EXPECT_GT(forced.imported_cells, 0u);
}

TEST_F(S102RunTest, reissued_tile_reimports)
{
  ASSERT_EQ(s102::runS102Import(options_).converted, 1u);

  // Upstream reissues: new content + issuance in the catalog.
  makeFixtureTile(root_ / "remote" / "US5TEST1.h5", 7.0);
  makeFixtureCatalog(
    catalog_, tile_path_, s102::sha256File(tile_path_), "2026-02-02");

  const auto rerun = s102::runS102Import(options_);
  EXPECT_EQ(rerun.skipped, 0u);
  EXPECT_EQ(rerun.fetched, 1u);
  EXPECT_EQ(rerun.converted, 1u);
}

TEST_F(S102RunTest, missing_datum_provider_fails)
{
  options_.datum = nullptr;
  EXPECT_THROW(s102::runS102Import(options_), std::runtime_error);
}

TEST_F(S102RunTest, offline_without_cache_fails)
{
  options_.offline = true;
  EXPECT_THROW(s102::runS102Import(options_), std::runtime_error);
}
