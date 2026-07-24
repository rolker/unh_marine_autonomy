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
#include <ogrsf_frmts.h>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include "s102/catalog.hpp"

namespace fs = std::filesystem;
using marine_bathymetry_store::s102::BBox;
using marine_bathymetry_store::s102::parseResolutionMeters;
using marine_bathymetry_store::s102::queryCatalog;

namespace
{

/// Add one square tile feature spanning [lon, lon+0.1] x [lat, lat+0.1].
void addTile(
  OGRLayer & layer, const std::string & tile_id, double lon, double lat,
  const std::string & url, const std::string & sha,
  const std::string & resolution, const std::string & issuance)
{
  auto feature = std::unique_ptr<OGRFeature>(
    OGRFeature::CreateFeature(layer.GetLayerDefn()));
  feature->SetField("TILE_ID", tile_id.c_str());
  feature->SetField("S102V30", url.c_str());
  feature->SetField("S102V30_SHA256", sha.c_str());
  feature->SetField("Resolution", resolution.c_str());
  feature->SetField("ISSUANCE", issuance.c_str());
  OGRLinearRing ring;
  ring.addPoint(lon, lat);
  ring.addPoint(lon + 0.1, lat);
  ring.addPoint(lon + 0.1, lat + 0.1);
  ring.addPoint(lon, lat + 0.1);
  ring.addPoint(lon, lat);
  OGRPolygon polygon;
  polygon.addRing(&ring);
  feature->SetGeometry(&polygon);
  ASSERT_EQ(layer.CreateFeature(feature.get()), OGRERR_NONE);
}

std::string makeFixtureCatalog(const fs::path & dir)
{
  GDALAllRegister();
  const std::string path = (dir / "catalog.gpkg").string();
  GDALDriver * driver = GetGDALDriverManager()->GetDriverByName("GPKG");
  if (driver == nullptr) {
    ADD_FAILURE() << "GPKG driver unavailable";
    return {};
  }
  auto ds = std::unique_ptr<GDALDataset>(
    driver->Create(path.c_str(), 0, 0, 0, GDT_Unknown, nullptr));
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
  // In-area, valid 4m + 16m; out-of-area; unparseable Resolution; missing SHA.
  addTile(*layer, "IN4M", -70.65, 42.95, "https://x/in4m.h5", "AB", "4m", "t1");
  addTile(*layer, "IN16M", -70.65, 42.95, "https://x/in16m.h5", "CD", "16m", "t2");
  addTile(*layer, "FAR", -75.10, 38.80, "https://x/far.h5", "EF", "4m", "t3");
  addTile(*layer, "BADRES", -70.65, 42.95, "https://x/bad.h5", "01", "banana", "t4");
  addTile(*layer, "NOSHA", -70.65, 42.95, "https://x/nosha.h5", "", "4m", "t5");
  return path;
}

}  // namespace

TEST(S102Catalog, parse_resolution)
{
  EXPECT_EQ(parseResolutionMeters("4m"), 4.0);
  EXPECT_EQ(parseResolutionMeters("16m"), 16.0);
  EXPECT_EQ(parseResolutionMeters("0.5m"), 0.5);
  EXPECT_FALSE(parseResolutionMeters("").has_value());
  EXPECT_FALSE(parseResolutionMeters("banana").has_value());
  EXPECT_FALSE(parseResolutionMeters("4").has_value());
  EXPECT_FALSE(parseResolutionMeters("4meters").has_value());
  EXPECT_FALSE(parseResolutionMeters("-4m").has_value());
  EXPECT_FALSE(parseResolutionMeters("0m").has_value());
}

TEST(S102Catalog, bbox_query_filters_and_validates)
{
  const fs::path dir = fs::temp_directory_path() / "s102_catalog_test";
  fs::remove_all(dir);
  fs::create_directories(dir);
  const std::string catalog = makeFixtureCatalog(dir);
  ASSERT_FALSE(catalog.empty());

  // Area over the Shoals-ish fixtures: hits IN4M/IN16M/BADRES/NOSHA, not FAR.
  const auto records =
    queryCatalog(catalog, BBox{-70.70, 42.90, -70.55, 43.05});
  ASSERT_EQ(records.size(), 2u);
  // BADRES (unparseable Resolution) and NOSHA (no digest) must be skipped —
  // rows we can't place or verify are never imported on faith.
  for (const auto & rec : records) {
    EXPECT_TRUE(rec.tile_id == "IN4M" || rec.tile_id == "IN16M") << rec.tile_id;
    if (rec.tile_id == "IN4M") {
      EXPECT_EQ(rec.resolution_m, 4.0);
      // Digests are normalized to lowercase for comparison.
      EXPECT_EQ(rec.sha256, "ab");
      EXPECT_EQ(rec.issuance, "t1");
      EXPECT_EQ(rec.url, "https://x/in4m.h5");
    } else {
      EXPECT_EQ(rec.resolution_m, 16.0);
    }
  }

  // Disjoint area → nothing.
  EXPECT_TRUE(queryCatalog(catalog, BBox{0., 0., 1., 1.}).empty());

  fs::remove_all(dir);
}

TEST(S102Catalog, missing_catalog_throws)
{
  EXPECT_THROW(
    queryCatalog("/nonexistent/nope.gpkg", BBox{0., 0., 1., 1.}),
    std::runtime_error);
}
