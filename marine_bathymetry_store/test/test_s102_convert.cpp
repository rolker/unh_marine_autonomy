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

#include <gdal_priv.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "s102/convert.hpp"

namespace fs = std::filesystem;
using marine_bathymetry_store::s102::ConstantOffsetProvider;
using marine_bathymetry_store::s102::convertTile;

namespace
{

constexpr double kNoData = 1.0e6;

/// Build a synthetic projected (EPSG:32619, UTM 19N) S-102-like GeoTIFF:
/// band "depth" positive-down from MLLW, band "uncertainty", nodata 1e6.
/// 4x4 pixels of 100 m near Portsmouth. Pixel (1,1) is nodata in both bands;
/// pixel (2,2) has sigma = 0 (non-positive, importer treats as missing).
std::string makeFixtureTile(
  const fs::path & path, const char * vertical_datum, double depth_value)
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
  double gt[6] = {365000., 100., 0., 4759000., 0., -100.};
  ds->SetGeoTransform(gt);
  if (vertical_datum != nullptr) {
    ds->SetMetadataItem("VERTICAL_DATUM_ABBREV", vertical_datum);
  }

  std::vector<float> depth(16, static_cast<float>(depth_value));
  std::vector<float> uncertainty(16, 0.5f);
  depth[1 * 4 + 1] = static_cast<float>(kNoData);
  uncertainty[1 * 4 + 1] = static_cast<float>(kNoData);
  uncertainty[2 * 4 + 2] = 0.0f;

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

struct Converted
{
  std::vector<double> height;
  std::vector<double> uncertainty;
  int width = 0;
  int height_px = 0;
  bool geographic = false;
};

Converted readConverted(const std::string & path)
{
  Converted out;
  auto ds = std::unique_ptr<GDALDataset>(static_cast<GDALDataset *>(
        GDALOpenEx(path.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
      nullptr, nullptr, nullptr)));
  if (ds == nullptr) {
    ADD_FAILURE() << "cannot open " << path;
    return out;
  }
  out.width = ds->GetRasterXSize();
  out.height_px = ds->GetRasterYSize();
  const OGRSpatialReference * srs = ds->GetSpatialRef();
  out.geographic = (srs != nullptr) && srs->IsGeographic();
  out.height.resize(static_cast<size_t>(out.width) * out.height_px);
  out.uncertainty.resize(out.height.size());
  ds->GetRasterBand(1)->RasterIO(
    GF_Read, 0, 0, out.width, out.height_px, out.height.data(),
    out.width, out.height_px, GDT_Float64, 0, 0);
  ds->GetRasterBand(2)->RasterIO(
    GF_Read, 0, 0, out.width, out.height_px, out.uncertainty.data(),
    out.width, out.height_px, GDT_Float64, 0, 0);
  EXPECT_STREQ(ds->GetRasterBand(1)->GetDescription(), "height");
  EXPECT_STREQ(ds->GetRasterBand(2)->GetDescription(), "uncertainty");
  return out;
}

class S102ConvertTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    dir_ = fs::temp_directory_path() / "s102_convert_test";
    fs::remove_all(dir_);
    fs::create_directories(dir_);
  }
  void TearDown() override {fs::remove_all(dir_);}
  fs::path dir_;
};

}  // namespace

TEST_F(S102ConvertTest, warps_shifts_and_maps_nodata)
{
  const auto src = makeFixtureTile(dir_ / "tile.tif", "MLLW", 5.0);
  const std::string out = (dir_ / "out.tif").string();
  // MLLW surface at -28 m ellipsoidal: seafloor at 5 m depth below MLLW is
  // at -33 m ellipsoidal (up-positive) — the sign contract of the store.
  convertTile(src, out, ConstantOffsetProvider(-28.0));

  const auto converted = readConverted(out);
  ASSERT_GT(converted.width, 0);
  EXPECT_TRUE(converted.geographic) <<
    "importGeoTiff rejects projected rasters; convert must warp";

  size_t finite = 0;
  size_t nan_cells = 0;
  for (size_t i = 0; i < converted.height.size(); ++i) {
    if (std::isfinite(converted.height[i])) {
      ++finite;
      EXPECT_NEAR(converted.height[i], -33.0, 1e-9);
      // Valid-depth cells keep their sigma (0.5, or the deliberate 0.0 —
      // non-positive sigma is the importer's decision, not the converter's).
      EXPECT_TRUE(
        converted.uncertainty[i] == 0.5 || converted.uncertainty[i] == 0.0);
    } else {
      ++nan_cells;
      // Height nodata must pair with uncertainty nodata.
      EXPECT_TRUE(std::isnan(converted.uncertainty[i]));
    }
  }
  EXPECT_GT(finite, 0u);
  EXPECT_GT(nan_cells, 0u) << "the 1e6 source pixel must become NaN";
}

TEST_F(S102ConvertTest, datum_provider_nullopt_becomes_nodata)
{
  class NoAnswerProvider : public marine_bathymetry_store::s102::VerticalDatumProvider
  {
public:
    std::optional<double> mllwHeight(double, double) const override
    {
      return std::nullopt;
    }
  };
  const auto src = makeFixtureTile(dir_ / "tile.tif", "MLLW", 5.0);
  const std::string out = (dir_ / "out.tif").string();
  convertTile(src, out, NoAnswerProvider());
  // No datum answer anywhere ⇒ every cell nodata — never silently unshifted.
  const auto converted = readConverted(out);
  for (const double h : converted.height) {
    EXPECT_TRUE(std::isnan(h));
  }
}

TEST_F(S102ConvertTest, non_mllw_datum_hard_fails)
{
  // A Great Lakes LWD tile shifted as MLLW would be wrong by the full datum
  // separation — the converter must refuse.
  const auto lwd = makeFixtureTile(dir_ / "lwd.tif", "LWD", 5.0);
  EXPECT_THROW(
    convertTile(lwd, (dir_ / "out1.tif").string(), ConstantOffsetProvider(0.)),
    std::runtime_error);

  const auto absent = makeFixtureTile(dir_ / "absent.tif", nullptr, 5.0);
  EXPECT_THROW(
    convertTile(
      absent, (dir_ / "out2.tif").string(), ConstantOffsetProvider(0.)),
    std::runtime_error);
}

TEST_F(S102ConvertTest, missing_source_throws)
{
  EXPECT_THROW(
    convertTile(
      (dir_ / "nope.tif").string(), (dir_ / "out.tif").string(),
      ConstantOffsetProvider(0.)),
    std::runtime_error);
}
