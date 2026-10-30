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

#include <fcntl.h>
#include <gtest/gtest.h>
#include <gdal_priv.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_tiled_raster_store/coverage_manifest.hpp"
#include "marine_tiled_raster_store/export_geotiff.hpp"
#include "marine_tiled_raster_store/tile_io.hpp"
#include "marine_tiled_raster_store/tiled_raster_tile.hpp"

// [#370 Slice A] Whole-area GeoTIFF export: per-tile selection precedence
// (native > sidecar > hole, ADR-0011), pixel placement across a clipped 2x2
// tile neighbourhood, refusals that leave no output and no temporary file, the datum
// policy (ADR-0002 D4), the pixel cap, and the CLI's exit codes.

namespace
{

namespace fs = std::filesystem;
namespace mtrs = marine_tiled_raster_store;

constexpr std::uint64_t kEdge = 960;
// Level-10 tile whose south-west corner is exactly 43.0 N, -70.6015625 E
// (near Appledore Island): row = (43 + 96) / (8 / 1024), col = 109.3984375 / (8 / 1024).
constexpr std::uint32_t kRow10 = 17792;
constexpr std::uint32_t kCol10 = 14003;
constexpr std::uint64_t kCap = 250000000ULL;
const double kNaN = std::numeric_limits<double>::quiet_NaN();

gggs::GridIndex gridAt(std::uint8_t level, std::uint32_t row, std::uint32_t col)
{
  const std::string name = std::to_string(level) + "_" + std::to_string(row) + "_" +
    std::to_string(col) + ".tif";
  const gggs::GridIndex g = mtrs::gridFromTileName(level, row, col, name);
  EXPECT_TRUE(g.valid()) << name;
  return g;
}

double cellSpan(std::uint8_t level) {return gggs::levels[level].cell_angular_span;}

/// A box given in fractional GLOBAL cell coordinates at @p level.
mtrs::ExportBox cellBox(
  std::uint8_t level, double row_begin, double col_begin, double row_end, double col_end)
{
  const double c = cellSpan(level);
  return mtrs::ExportBox{-96.0 + row_begin * c, -180.0 + col_begin * c,
    -96.0 + row_end * c, -180.0 + col_end * c};
}

/// A box comfortably inside one tile (cells 100..860 of it).
mtrs::ExportBox insideTile(std::uint8_t level, std::uint32_t row, std::uint32_t col)
{
  const double r = static_cast<double>(row) * kEdge, c = static_cast<double>(col) * kEdge;
  return cellBox(level, r + 100.5, c + 100.5, r + 860.5, c + 860.5);
}

mtrs::ExportOptions opts(const mtrs::ExportBox & box, std::optional<std::uint8_t> level = {})
{
  mtrs::ExportOptions o;
  o.box = box;
  o.level = level;
  o.max_pixels = kCap;
  return o;
}

class Store
{
public:
  explicit Store(const std::string & name)
  : root_(fs::path(::testing::TempDir()) / ("mtrs_export_" + name))
  {
    fs::remove_all(root_);
    fs::create_directories(layer());
    fs::create_directories(outDir());
  }
  ~Store()
  {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }
  fs::path root() const {return root_;}
  fs::path layer() const {return root_ / "theme" / "processed";}
  fs::path sidecar() const {return layer() / "overviews";}
  fs::path registry() const {return root_ / "theme" / "registry.json";}
  fs::path outDir() const {return root_ / "out";}
  fs::path out() const {return outDir() / "export.tif";}
  /// The prefix of every temporary file an export to out() may create
  /// (`export.tif.tmp.XXXXXX`); also the name of a plain foreign lookalike.
  fs::path tmpPrefix() const {return outDir() / "export.tif.tmp";}

  void writeRegistry(const std::string & json) const {std::ofstream(registry()) << json;}

private:
  fs::path root_;
};

/// Write one tile; @p f gives band b's value at GGGS cell (row south-first, col).
template<typename T>
void writeTile(
  const fs::path & dir, const gggs::GridIndex & g, std::size_t bands,
  const std::function<T(std::size_t, std::size_t, std::size_t)> & f, std::optional<T> nodata)
{
  fs::create_directories(dir);
  mtrs::TiledRasterTile<T> tile(g, bands, T{});
  for (std::size_t b = 0; b < bands; ++b) {
    for (std::size_t r = 0; r < kEdge; ++r) {
      for (std::size_t c = 0; c < kEdge; ++c) {
        tile.band(b)[r * kEdge + c] = f(r, c, b);
      }
    }
  }
  mtrs::saveTile<T>(
    tile, (dir / mtrs::tileFilename(g)).string(), std::vector<std::optional<T>>(bands, nodata));
}

void writeConst(
  const fs::path & dir, const gggs::GridIndex & g, double value, std::size_t bands = 2)
{
  writeTile<double>(
    dir, g, bands, [value](std::size_t, std::size_t, std::size_t) {return value;}, kNaN);
}

struct Raster
{
  int width = 0, height = 0, bands = 0;
  std::vector<std::vector<float>> data;
  std::vector<GDALDataType> types;
  std::vector<std::string> descriptions;
  std::vector<std::string> band_nodata_count;
  std::vector<bool> nodata_is_nan;
  double gt[6] = {0, 0, 0, 0, 0, 0};
  std::map<std::string, std::string> md;
  std::string crs_auth;

  float at(int b, int x, int y) const
  {
    return data[b][static_cast<std::size_t>(y) * width + x];
  }
};

Raster readRaster(const fs::path & path)
{
  GDALAllRegister();
  Raster r;
  GDALDataset * ds = GDALDataset::FromHandle(GDALOpen(path.c_str(), GA_ReadOnly));
  if (ds == nullptr) {
    ADD_FAILURE() << "cannot open " << path;
    return r;
  }
  r.width = ds->GetRasterXSize();
  r.height = ds->GetRasterYSize();
  r.bands = ds->GetRasterCount();
  ds->GetGeoTransform(r.gt);
  const OGRSpatialReference * srs = ds->GetSpatialRef();
  if (srs != nullptr && srs->GetAuthorityCode(nullptr) != nullptr) {
    r.crs_auth = std::string(srs->GetAuthorityName(nullptr)) + ":" + srs->GetAuthorityCode(nullptr);
  }
  for (const char * key : {"SOURCE_LEVEL", "SOURCE_KIND", "GEOMETRIC_ERROR_M", "BAND_NAMES",
      "VERTICAL_DATUM", "SOURCE_VERTICAL_DATUM", "DOWNCAST", "NODATA_COUNT"})
  {
    const char * v = ds->GetMetadataItem(key);
    if (v != nullptr) {
      r.md[key] = v;
    }
  }
  for (int b = 1; b <= r.bands; ++b) {
    GDALRasterBand * band = ds->GetRasterBand(b);
    r.types.push_back(band->GetRasterDataType());
    r.descriptions.push_back(band->GetDescription());
    const char * nc = band->GetMetadataItem("NODATA_COUNT");
    r.band_nodata_count.push_back(nc ? nc : "");
    int has = 0;
    const double nd = band->GetNoDataValue(&has);
    r.nodata_is_nan.push_back(has != 0 && std::isnan(nd));
    std::vector<float> buf(static_cast<std::size_t>(r.width) * r.height);
    EXPECT_EQ(
      band->RasterIO(
        GF_Read, 0, 0, r.width, r.height, buf.data(), r.width, r.height, GDT_Float32, 0, 0),
      CE_None);
    r.data.push_back(std::move(buf));
  }
  GDALClose(ds);
  return r;
}

bool hasWarning(const mtrs::ExportResult & r, const std::string & needle)
{
  for (const std::string & w : r.warnings) {
    if (w.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

/// Every file in the output directory whose name starts with `export.tif.tmp`.
std::set<std::string> tmpFiles(const Store & s)
{
  std::set<std::string> names;
  const std::string prefix = s.tmpPrefix().filename().string();
  for (const fs::directory_entry & e : fs::directory_iterator(s.outDir())) {
    const std::string name = e.path().filename().string();
    if (name.compare(0, prefix.size(), prefix) == 0) {
      names.insert(name);
    }
  }
  return names;
}

/// Every refusal/failure must leave neither an output nor a stray temporary file.
void expectNothingWritten(const Store & s)
{
  EXPECT_FALSE(fs::exists(s.out())) << "a refused/failed export left an output";
  EXPECT_TRUE(tmpFiles(s).empty()) << "a refused/failed export left a temporary file";
}

std::string readFile(const fs::path & p)
{
  std::ifstream in(p);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

// ---------------------------------------------------------------- lattice ----

TEST(ExportSnap, SnapsOutwardInIntegerCells)
{
  const std::uint64_t r0 = kRow10 * kEdge, c0 = kCol10 * kEdge;
  const mtrs::CellWindow w = mtrs::snapBoxToLevel(
    cellBox(10, r0 + 100.3, c0 + 200.7, r0 + 1460.6, c0 + 1260.2), 10);
  EXPECT_EQ(w.row_begin, r0 + 100);
  EXPECT_EQ(w.row_end, r0 + 1461);
  EXPECT_EQ(w.col_begin, c0 + 200);
  EXPECT_EQ(w.col_end, c0 + 1261);
}

TEST(ExportSnap, EdgeOnALatticeLineAddsNoSliver)
{
  const std::uint64_t r0 = kRow10 * kEdge, c0 = kCol10 * kEdge;
  // Whole-tile box: edges land on lattice lines up to floating-point noise.
  const mtrs::CellWindow w = mtrs::snapBoxToLevel(
    cellBox(10, static_cast<double>(r0), static_cast<double>(c0),
    static_cast<double>(r0 + kEdge), static_cast<double>(c0 + kEdge)), 10);
  EXPECT_EQ(w.row_end - w.row_begin, kEdge);
  EXPECT_EQ(w.col_end - w.col_begin, kEdge);
  EXPECT_EQ(w.row_begin, r0);
  EXPECT_EQ(w.col_begin, c0);
}

TEST(ExportSnap, NonFiniteOrUnorderedBoxIsRefusedAndHugeValuesClamp)
{
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  for (const mtrs::ExportBox & b : {mtrs::ExportBox{nan, 0.0, 1.0, 1.0},
      mtrs::ExportBox{0.0, 0.0, 1.0, inf}, mtrs::ExportBox{0.0, -inf, 1.0, 1.0},
      mtrs::ExportBox{2.0, 0.0, 1.0, 1.0}, mtrs::ExportBox{0.0, 2.0, 1.0, 1.0}})
  {
    EXPECT_THROW(mtrs::snapBoxToLevel(b, 10), std::invalid_argument);
  }
  // Finite but far outside the lattice: clamped, never an overflowing cast.
  const mtrs::CellWindow w =
    mtrs::snapBoxToLevel(mtrs::ExportBox{-1e300, -1e300, 1e300, 1e300}, 3);
  const std::uint64_t rows = static_cast<std::uint64_t>(gggs::levels[3].row_count) * kEdge;
  const std::uint64_t cols = static_cast<std::uint64_t>(gggs::levels[3].column_count) * kEdge;
  EXPECT_EQ(w.row_begin, 0u);
  EXPECT_EQ(w.row_end, rows);
  EXPECT_EQ(w.col_begin, 0u);
  EXPECT_EQ(w.col_end, cols);
}

TEST(ExportBandNames, ByBandCountOnly)
{
  EXPECT_EQ(mtrs::bandNamesForCount(2), (std::vector<std::string>{"depth", "uncertainty"}));
  EXPECT_EQ(
    mtrs::bandNamesForCount(3),
    (std::vector<std::string>{"mean", "scaled_standard_error", "sample_sd"}));
  EXPECT_EQ(mtrs::bandNamesForCount(1), (std::vector<std::string>{"band_1"}));
  EXPECT_EQ(mtrs::bandNamesForCount(4).back(), "band_4");
}

TEST(ExportRegistry, TrailingSlashResolvesToTheParent)
{
  EXPECT_EQ(mtrs::registryPathForLayer("/a/b/"), "/a/registry.json");
  EXPECT_EQ(mtrs::registryPathForLayer("/a/b"), "/a/registry.json");
  EXPECT_EQ(mtrs::registryPathForLayer("/a/b/./"), "/a/registry.json");
}

// -------------------------------------------------------------- placement ----

// Asymmetric values over a 2x2 tile neighbourhood (E-W and N-S neighbours) and
// a box that snaps outward and clips all four tiles. Every output pixel is
// compared against the GGGS cell it must hold — this catches a missing or a
// doubled row flip and any off-by-one in the integer window offsets.
TEST(ExportPlacement, ClippedTwoByTwoEveryPixel)
{
  Store s("placement");
  // Value encodes (tile k, south-first local row, local col, band).
  const auto value = [](int k, std::size_t lr, std::size_t lc, std::size_t b) {
      return 100000.0 * k + 1000.0 * static_cast<double>(lr) + static_cast<double>(lc) +
             0.5 * static_cast<double>(b);
    };
  for (int dr = 0; dr < 2; ++dr) {
    for (int dc = 0; dc < 2; ++dc) {
      const int k = dr * 2 + dc;
      writeTile<double>(
        s.layer(), gridAt(10, kRow10 + dr, kCol10 + dc), 2,
        [&value, k](std::size_t r, std::size_t c, std::size_t b) {return value(k, r, c, b);},
        kNaN);
    }
  }
  s.writeRegistry(R"({"datum": "mllw"})");
  const std::uint64_t r0 = kRow10 * kEdge, c0 = kCol10 * kEdge;
  const mtrs::ExportBox box = cellBox(10, r0 + 100.3, c0 + 200.7, r0 + 1460.6, c0 + 1260.2);

  const mtrs::ExportResult res = mtrs::exportGeoTiff(s.layer().string(), s.out().string(),
    opts(box));
  EXPECT_EQ(res.width, 1061u);
  EXPECT_EQ(res.height, 1361u);
  EXPECT_EQ(res.tiles_native, 4u);
  EXPECT_EQ(res.tile_positions, 4u);
  EXPECT_EQ(res.source_kind, "native");
  EXPECT_EQ(res.nodata_count, (std::vector<std::uint64_t>{0, 0}));

  const Raster out = readRaster(s.out());
  ASSERT_EQ(out.width, 1061);
  ASSERT_EQ(out.height, 1361);
  ASSERT_EQ(out.bands, 2);
  const double c = cellSpan(10);
  EXPECT_DOUBLE_EQ(out.gt[0], -180.0 + static_cast<double>(c0 + 200) * c);
  EXPECT_DOUBLE_EQ(out.gt[3], -96.0 + static_cast<double>(r0 + 1461) * c);
  EXPECT_DOUBLE_EQ(out.gt[1], c);
  EXPECT_DOUBLE_EQ(out.gt[5], -c);
  EXPECT_EQ(out.gt[2], 0.0);
  EXPECT_EQ(out.gt[4], 0.0);
  EXPECT_EQ(out.crs_auth, "EPSG:4326");

  const std::uint64_t row_end = r0 + 1461, col_begin = c0 + 200;
  std::size_t mismatches = 0;
  for (int b = 0; b < 2; ++b) {
    for (int y = 0; y < out.height; ++y) {
      const std::uint64_t gr = row_end - 1 - static_cast<std::uint64_t>(y);
      const int dr = static_cast<int>(gr / kEdge - kRow10);
      for (int x = 0; x < out.width; ++x) {
        const std::uint64_t gc = col_begin + static_cast<std::uint64_t>(x);
        const int dc = static_cast<int>(gc / kEdge - kCol10);
        const float expected = static_cast<float>(
          value(dr * 2 + dc, gr % kEdge, gc % kEdge, static_cast<std::size_t>(b)));
        if (out.at(b, x, y) != expected && ++mismatches <= 5) {
          ADD_FAILURE() << "band " << b << " pixel (" << x << "," << y << ") = " <<
            out.at(b, x, y) << ", expected " << expected;
        }
      }
    }
  }
  EXPECT_EQ(mismatches, 0u);

  // Independent geographic cross-check: read a pixel straight from a north-up
  // source file, locate its centre in the output by geotransform alone.
  const auto spot = [&](const gggs::GridIndex & g, int px, int py) {
      GDALDataset * ds = GDALDataset::FromHandle(
        GDALOpen((s.layer() / mtrs::tileFilename(g)).c_str(), GA_ReadOnly));
      ASSERT_NE(ds, nullptr);
      double tgt[6];
      ds->GetGeoTransform(tgt);
      double v = 0;
      ASSERT_EQ(
        ds->GetRasterBand(1)->RasterIO(GF_Read, px, py, 1, 1, &v, 1, 1, GDT_Float64, 0, 0),
        CE_None);
      GDALClose(ds);
      const double lon = tgt[0] + (px + 0.5) * tgt[1];
      const double lat = tgt[3] + (py + 0.5) * tgt[5];
      const int ox = static_cast<int>(std::floor((lon - out.gt[0]) / out.gt[1]));
      const int oy = static_cast<int>(std::floor((lat - out.gt[3]) / out.gt[5]));
      ASSERT_GE(ox, 0);
      ASSERT_LT(ox, out.width);
      ASSERT_GE(oy, 0);
      ASSERT_LT(oy, out.height);
      EXPECT_EQ(out.at(0, ox, oy), static_cast<float>(v)) << mtrs::tileFilename(g);
    };
  spot(gridAt(10, kRow10 + 1, kCol10), 300, 600);   // north-west tile
  spot(gridAt(10, kRow10, kCol10 + 1), 10, 20);     // south-east tile
  spot(gridAt(10, kRow10 + 1, kCol10 + 1), 100, 900);   // north-east tile
  spot(gridAt(10, kRow10, kCol10), 959, 0);         // south-west tile, seam corner
}

// A missing tile between two present ones is a NaN hole, NaN cells inside a
// tile stay NaN, and NODATA_COUNT = total - non-NaN cells written, per band.
TEST(ExportPlacement, HolesAndNoDataCount)
{
  Store s("holes");
  // Band 0: NaN in local rows < 10; band 1: NaN in local cols < 20.
  const auto f = [](std::size_t r, std::size_t c, std::size_t b) {
      if ((b == 0 && r < 10) || (b == 1 && c < 20)) {
        return kNaN;
      }
      return 5.0;
    };
  writeTile<double>(s.layer(), gridAt(10, kRow10, kCol10), 2, f, kNaN);
  writeTile<double>(s.layer(), gridAt(10, kRow10, kCol10 + 2), 2, f, kNaN);
  s.writeRegistry(R"({"datum": "mllw"})");
  const double r0 = static_cast<double>(kRow10) * kEdge, c0 = static_cast<double>(kCol10) * kEdge;
  const mtrs::ExportBox box = cellBox(10, r0, c0, r0 + kEdge, c0 + 3 * kEdge);

  const mtrs::ExportResult res = mtrs::exportGeoTiff(s.layer().string(), s.out().string(),
    opts(box));
  EXPECT_EQ(res.width, 3 * kEdge);
  EXPECT_EQ(res.height, kEdge);
  EXPECT_EQ(res.tiles_native, 2u);
  EXPECT_EQ(res.tile_positions, 3u);
  const std::uint64_t hole = kEdge * kEdge;
  ASSERT_EQ(res.nodata_count.size(), 2u);
  EXPECT_EQ(res.nodata_count[0], hole + 2 * 10 * kEdge);
  EXPECT_EQ(res.nodata_count[1], hole + 2 * 20 * kEdge);

  const Raster out = readRaster(s.out());
  EXPECT_TRUE(std::isnan(out.at(0, 960 + 480, 480)));   // the hole
  EXPECT_EQ(out.at(0, 480, 480), 5.0f);
  EXPECT_TRUE(std::isnan(out.at(0, 480, 959)));          // south rows of band 0 (bottom of output)
  EXPECT_TRUE(std::isnan(out.at(1, 1920 + 5, 100)));     // west cols of band 1, east tile
  std::uint64_t nan0 = 0;
  for (float v : out.data[0]) {
    nan0 += std::isnan(v) ? 1 : 0;
  }
  EXPECT_EQ(nan0, res.nodata_count[0]);
  EXPECT_EQ(out.md.at("NODATA_COUNT"), std::to_string(res.nodata_count[0]));
  EXPECT_EQ(out.band_nodata_count[1], std::to_string(res.nodata_count[1]));
  EXPECT_TRUE(out.nodata_is_nan[0]);
  EXPECT_TRUE(out.nodata_is_nan[1]);
}

// -------------------------------------------------------------- selection ----

TEST(ExportSelection, NativeWinsOverSidecarAtTheSameLevel)
{
  Store s("native_wins");
  s.writeRegistry(R"({"datum": "mllw"})");
  const gggs::GridIndex g = gridAt(9, kRow10 / 2, kCol10 / 2);
  writeConst(s.layer(), g, 1.0);
  writeConst(s.sidecar(), g, 2.0);
  const mtrs::ExportResult res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(), opts(insideTile(9, g.row(), g.column()), 9));
  EXPECT_EQ(res.source_kind, "native");
  EXPECT_EQ(res.tiles_native, 1u);
  EXPECT_EQ(res.tiles_overview, 0u);
  const Raster out = readRaster(s.out());
  EXPECT_EQ(out.at(0, 10, 10), 1.0f);
  EXPECT_EQ(out.md.at("SOURCE_KIND"), "native");
}

TEST(ExportSelection, MixedNativeAndSidecarAtOneLevel)
{
  Store s("mixed");
  s.writeRegistry(R"({"datum": "mllw"})");
  const gggs::GridIndex west = gridAt(9, kRow10 / 2, kCol10 / 2);
  const gggs::GridIndex east = gridAt(9, kRow10 / 2, kCol10 / 2 + 1);
  writeConst(s.layer(), west, 1.0);
  writeConst(s.sidecar(), east, 2.0);
  mtrs::CoverageManifest m;
  m.add(east, 4.25);
  mtrs::saveCoverageManifest(m, (s.sidecar() / "coverage.json").string(), "derived");
  const double r0 = static_cast<double>(west.row()) * kEdge;
  const double c0 = static_cast<double>(west.column()) * kEdge;
  const mtrs::ExportResult res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(),
    opts(cellBox(9, r0 + 10, c0 + 10, r0 + 20, c0 + 2 * kEdge - 10)));
  EXPECT_EQ(res.source_level, 9);   // the finest native level in the box
  EXPECT_EQ(res.source_kind, "mixed");
  EXPECT_EQ(res.tiles_native, 1u);
  EXPECT_EQ(res.tiles_overview, 1u);
  EXPECT_DOUBLE_EQ(res.geometric_error_m, 4.25);
  EXPECT_EQ(res.nodata_count[0], 0u) << "the sidecar tile must fill, not leave a silent NaN";
  const Raster out = readRaster(s.out());
  EXPECT_EQ(out.at(0, 0, 0), 1.0f);
  EXPECT_EQ(out.at(0, out.width - 1, 0), 2.0f);
  EXPECT_EQ(out.md.at("SOURCE_KIND"), "mixed");
}

TEST(ExportSelection, OtherNativeLevelsInTheBoxAreExcludedWithAWarning)
{
  Store s("excluded_levels");
  s.writeRegistry(R"({"datum": "mllw"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  // A native level-9 tile elsewhere in the same box (one level-9 tile east).
  const gggs::GridIndex coarse = gridAt(9, kRow10 / 2, kCol10 / 2 + 1);
  writeConst(s.layer(), coarse, 3.0);
  const double r0 = static_cast<double>(kRow10) * kEdge;
  const double c0 = static_cast<double>(kCol10) * kEdge;
  // The level-9 tile's west edge, in level-10 cells.
  const double c9 = static_cast<double>(coarse.column()) * 2 * kEdge;
  const mtrs::ExportResult res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(), opts(cellBox(10, r0 + 10, c0 + 10, r0 + 20, c9 + 10)));
  EXPECT_EQ(res.source_level, 10);
  EXPECT_TRUE(hasWarning(res, "native tiles at level(s) 9 inside the box are NOT exported"));
  EXPECT_TRUE(hasWarning(res, "NaN holes"));
}

// The reviewer's reproduction: an explicit coarse --level whose sidecar tile
// carries the finer native data (no native tile in between) loses nothing, so
// it must not warn. Likewise a coarser native tile under the box whose every
// export-level position holds a tile that is fully valid (band 1 non-NaN)
// inside the window: there is no hole the coarser data could have filled.
TEST(ExportSelection, CoveredOtherLevelsDoNotWarn)
{
  Store s("covered_levels");
  s.writeRegistry(R"({"datum": "mllw"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  writeConst(s.sidecar(), gridAt(9, kRow10 / 2, kCol10 / 2), 2.0);
  mtrs::ExportResult res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(), opts(insideTile(10, kRow10, kCol10), 9));
  EXPECT_EQ(res.source_kind, "overview");
  EXPECT_FALSE(hasWarning(res, "NOT exported")) << "sidecar-covered native data is not lost";
  // A native level-8 tile over the same ground, default level 10: the box's
  // only level-10 position holds a native tile with no NaN cell, so nothing of
  // level 8 can be missing.
  writeConst(s.layer(), gridAt(8, kRow10 / 4, kCol10 / 4), 3.0);
  res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(), opts(insideTile(10, kRow10, kCol10)));
  EXPECT_EQ(res.source_level, 10);
  EXPECT_EQ(res.nodata_count[0], 0u);
  EXPECT_TRUE(res.warnings.empty()) << res.warnings.front();
}

// The reviewer's other reproduction: a coarser native tile under export-level
// tiles that have NaN cells. Neither a native export-level tile nor a sidecar
// tile (folded from FINER levels only) carries the coarser data, so it is lost
// wherever the export-level tile is NaN, and that is warned about, counting
// only the NaN cells inside the window. Typical of a mixed-resolution
// reference layer.
TEST(ExportSelection, CoarserNativeUnderNanCellsOfExportLevelTilesWarns)
{
  Store s("coarser_under_nan");
  s.writeRegistry(R"({"datum": "mllw"})");
  // Level-10 tile: the southern half (local rows < 480) NaN in band 1 only;
  // band 2 is valid everywhere, so only band 1 decides.
  writeTile<double>(
    s.layer(), gridAt(10, kRow10, kCol10), 2,
    [](std::size_t r, std::size_t, std::size_t b) {return b == 0 && r < 480 ? kNaN : 1.0;},
    kNaN);
  writeConst(s.layer(), gridAt(8, kRow10 / 4, kCol10 / 4), 3.0);
  const double r0 = static_cast<double>(kRow10) * kEdge;
  const double c0 = static_cast<double>(kCol10) * kEdge;
  // Rows 400..600 x columns 100..200: 80 NaN rows (400..479) x 100 columns.
  mtrs::ExportResult res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(),
    opts(cellBox(10, r0 + 400, c0 + 100, r0 + 600, c0 + 200)));
  EXPECT_EQ(res.source_level, 10);
  EXPECT_EQ(res.nodata_count[0], 8000u);
  EXPECT_TRUE(
    hasWarning(res, "native tiles at level(s) 8 inside the box are NOT used to fill holes"));
  EXPECT_TRUE(hasWarning(res, "8000 band-1 cell(s) of level-10 tiles"));
  EXPECT_FALSE(hasWarning(res, "NaN holes")) << "the position holds a tile: not a hole";
  // The same tile, but a window over its valid northern half only: no warning.
  res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(),
    opts(cellBox(10, r0 + 500, c0 + 100, r0 + 600, c0 + 200)));
  EXPECT_EQ(res.nodata_count[0], 0u);
  EXPECT_FALSE(hasWarning(res, "NOT used to fill holes"));
  // A sidecar tile at the export level carries no coarser data either.
  writeTile<double>(
    s.sidecar(), gridAt(9, kRow10 / 2, kCol10 / 2), 2,
    [](std::size_t r, std::size_t, std::size_t) {return r < 300 ? kNaN : 2.0;}, kNaN);
  res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(),
    opts(insideTile(9, kRow10 / 2, kCol10 / 2), 9));
  EXPECT_EQ(res.source_kind, "overview");
  EXPECT_TRUE(
    hasWarning(res, "native tiles at level(s) 8 inside the box are NOT used to fill holes"));
}

// Finer native data beneath a NATIVE ancestor never reaches the export level:
// the fold stops at a native tile (ADR-0011 fold scope).
TEST(ExportSelection, FinerNativeBeneathANativeAncestorWarns)
{
  Store s("shadowed_levels");
  s.writeRegistry(R"({"datum": "mllw"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  writeConst(s.layer(), gridAt(9, kRow10 / 2, kCol10 / 2), 2.0);
  // Export at the native ancestor's own level: the native level-9 tile is read.
  mtrs::ExportResult res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(), opts(insideTile(10, kRow10, kCol10), 9));
  EXPECT_EQ(res.source_kind, "native");
  EXPECT_TRUE(hasWarning(res, "native tiles at level(s) 10 inside the box are NOT exported"));
  EXPECT_TRUE(hasWarning(res, "beneath native tile(s) at level(s) 9"));
  EXPECT_FALSE(hasWarning(res, "NaN holes"));
  // Export coarser still, from a sidecar level-8 tile: the level-9 native tile
  // is in between, so the level-10 data is still missing.
  writeConst(s.sidecar(), gridAt(8, kRow10 / 4, kCol10 / 4), 3.0);
  res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(), opts(insideTile(10, kRow10, kCol10), 8));
  EXPECT_EQ(res.source_kind, "overview");
  EXPECT_TRUE(hasWarning(res, "native tiles at level(s) 10 inside the box are NOT exported"));
  EXPECT_TRUE(hasWarning(res, "beneath native tile(s) at level(s) 9"));
}

// Finer native data whose export-level position has no tile at all (a stale or
// partial sidecar) is a hole, and says so.
TEST(ExportSelection, FinerNativeOverAnExportLevelHoleWarns)
{
  Store s("finer_over_hole");
  s.writeRegistry(R"({"datum": "mllw"})");
  const gggs::GridIndex west = gridAt(10, kRow10, kCol10);
  // Four level-9 tiles further east (8 level-10 tiles): a different level-9 parent.
  const gggs::GridIndex east = gridAt(10, kRow10, kCol10 + 8);
  writeConst(s.layer(), west, 1.0);
  writeConst(s.layer(), east, 1.0);
  writeConst(s.sidecar(), gridAt(9, kRow10 / 2, kCol10 / 2), 2.0);   // west's parent only
  const double r0 = static_cast<double>(kRow10) * kEdge;
  const double c0 = static_cast<double>(kCol10) * kEdge;
  const mtrs::ExportResult res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(),
    opts(cellBox(10, r0 + 10, c0 + 10, r0 + 20, c0 + 9 * kEdge - 10), 9));
  EXPECT_TRUE(hasWarning(res, "native tiles at level(s) 10 inside the box are NOT exported"));
  EXPECT_TRUE(hasWarning(res, "1 level-9 tile position(s)"));
  EXPECT_FALSE(hasWarning(res, "beneath native"));
}

// The snap tolerance is per level (1e-4 of a cell AT that level), so a box edge
// just past a level-9 tile boundary snaps onto it at level 9 but still takes in
// the next level-10 tile at level 10. That finer tile's level-9 position is not
// in the export, so nothing of it is missing from the file: no hole, nothing
// shadowed, and no warning (Lens B's reproduction: "1 native of 1 tile
// position" plus a hole warning).
TEST(ExportSelection, FinerNativeWhoseExportPositionIsOutsideTheExportIsNotCounted)
{
  Store s("finer_outside_export");
  s.writeRegistry(R"({"datum": "mllw"})");
  const std::uint32_t row9 = kRow10 / 2, col9 = kCol10 / 2;   // kCol10 is the east child
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  const gggs::GridIndex east = gridAt(10, kRow10, kCol10 + 1);   // under level-9 col9 + 1
  writeConst(s.layer(), east, 1.0);
  writeConst(s.sidecar(), gridAt(9, row9, col9), 2.0);
  const double r9 = static_cast<double>(row9) * kEdge;
  const double boundary9 = static_cast<double>(col9 + 1) * kEdge;
  // East edge 0.75e-4 level-9 cells (1.5e-4 level-10 cells) past the boundary.
  const mtrs::ExportBox box = cellBox(9, r9 + 10, boundary9 - 400, r9 + 20, boundary9 + 0.75e-4);
  ASSERT_EQ(mtrs::snapBoxToLevel(box, 9).col_end, static_cast<std::uint64_t>(boundary9));
  ASSERT_GT(mtrs::snapBoxToLevel(box, 10).col_end, static_cast<std::uint64_t>(2 * boundary9))
    << "the east level-10 tile must count as inside the box at level 10";
  mtrs::ExportResult res =
    mtrs::exportGeoTiff(s.layer().string(), s.out().string(), opts(box, 9));
  EXPECT_EQ(res.tile_positions, 1u);
  EXPECT_FALSE(hasWarning(res, "NOT exported")) << res.warnings.front();
  // The same with a native level-9 tile over the east one, just outside the
  // export: it would have "shadowed" a tile that is not in the file either.
  writeConst(s.layer(), gridAt(9, row9, col9 + 1), 3.0);
  res = mtrs::exportGeoTiff(s.layer().string(), s.out().string(), opts(box, 9));
  EXPECT_EQ(res.tile_positions, 1u);
  EXPECT_FALSE(hasWarning(res, "NOT exported")) << res.warnings.front();
}

TEST(ExportSelection, FinestNativeLevelIsJudgedInsideTheBox)
{
  Store s("finest_in_box");
  s.writeRegistry(R"({"datum": "mllw"})");
  // Level 10 exists, but far from the box; level 9 is the finest native in it.
  writeConst(s.layer(), gridAt(10, kRow10 + 40, kCol10 + 40), 1.0);
  const gggs::GridIndex g9 = gridAt(9, kRow10 / 2, kCol10 / 2);
  writeConst(s.layer(), g9, 2.0);
  const mtrs::ExportResult res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(), opts(insideTile(9, g9.row(), g9.column())));
  EXPECT_EQ(res.source_level, 9);
  EXPECT_FALSE(hasWarning(res, "NOT exported"));
}

TEST(ExportSelection, RefusesToUpsample)
{
  Store s("no_upsample");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  try {
    mtrs::exportGeoTiff(
      s.layer().string(), s.out().string(), opts(insideTile(10, kRow10, kCol10), 11));
    FAIL() << "expected a refusal";
  } catch (const mtrs::ExportRefused & e) {
    EXPECT_NE(std::string(e.what()).find("refusing to upsample"), std::string::npos) << e.what();
  }
  expectNothingWritten(s);
}

TEST(ExportSelection, RefusesACoarseLevelAbsentFromTheSidecar)
{
  Store s("coarse_absent");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  const mtrs::ExportBox box = insideTile(10, kRow10, kCol10);
  // No sidecar at all.
  try {
    mtrs::exportGeoTiff(s.layer().string(), s.out().string(), opts(box, 8));
    FAIL() << "expected a refusal";
  } catch (const mtrs::ExportRefused & e) {
    const std::string msg = e.what();
    EXPECT_NE(msg.find("no overviews/ sidecar"), std::string::npos) << msg;
    EXPECT_NE(msg.find("native levels in the box: 10"), std::string::npos) << msg;
  }
  expectNothingWritten(s);
  // A sidecar that holds level 9 but not 8.
  writeConst(s.sidecar(), gridAt(9, kRow10 / 2, kCol10 / 2), 2.0);
  try {
    mtrs::exportGeoTiff(s.layer().string(), s.out().string(), opts(box, 8));
    FAIL() << "expected a refusal";
  } catch (const mtrs::ExportRefused & e) {
    const std::string msg = e.what();
    EXPECT_NE(msg.find("sidecar has none at that level"), std::string::npos) << msg;
    EXPECT_NE(msg.find("sidecar levels in the box: 9"), std::string::npos) << msg;
  }
  expectNothingWritten(s);
}

TEST(ExportSelection, CoarseLevelFromTheSidecarCarriesItsGeometricError)
{
  Store s("coarse_sidecar");
  s.writeRegistry(R"({"datum": "mllw"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  const gggs::GridIndex g8 = gridAt(8, kRow10 / 4, kCol10 / 4);
  writeConst(s.sidecar(), g8, 7.0);
  mtrs::CoverageManifest m;
  m.add(g8, 3.625);
  mtrs::saveCoverageManifest(m, (s.sidecar() / "coverage.json").string(), "derived");
  const mtrs::ExportResult res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(), opts(insideTile(10, kRow10, kCol10), 8));
  EXPECT_EQ(res.source_level, 8);
  EXPECT_EQ(res.source_kind, "overview");
  EXPECT_DOUBLE_EQ(res.geometric_error_m, 3.625);
  // The sidecar tile carries the native level-10 data (no native tile in
  // between): nothing is lost, so no excluded-level warning.
  EXPECT_FALSE(hasWarning(res, "NOT exported"));
  const Raster out = readRaster(s.out());
  EXPECT_EQ(out.at(0, 0, 0), 7.0f);
  EXPECT_EQ(out.md.at("SOURCE_LEVEL"), "8");
  EXPECT_EQ(out.md.at("GEOMETRIC_ERROR_M"), "3.625");
}

TEST(ExportSelection, SidecarTileWithoutRecordedErrorFallsBackLoudly)
{
  Store s("no_recorded_error");
  s.writeRegistry(R"({"datum": "mllw"})");
  const gggs::GridIndex g8 = gridAt(8, kRow10 / 4, kCol10 / 4);
  writeConst(s.sidecar(), g8, 7.0);   // no coverage.json
  const mtrs::ExportResult res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(), opts(insideTile(8, g8.row(), g8.column()), 8));
  EXPECT_DOUBLE_EQ(res.geometric_error_m, gggs::Level(8).cellSize());
  EXPECT_TRUE(hasWarning(res, "UNDERSTATES"));
}

TEST(ExportSelection, BoxOutsideNativeCoverageButInsideASidecarTile)
{
  Store s("outside_native");
  s.writeRegistry(R"({"datum": "mllw"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  const gggs::GridIndex g8 = gridAt(8, kRow10 / 4, kCol10 / 4);
  writeConst(s.sidecar(), g8, 7.0);
  // Level-10 tile (kRow10+2, kCol10-2) lies inside g8 (L10 rows 17792-17795,
  // cols 14000-14003) but holds no native tile.
  const mtrs::ExportBox box = insideTile(10, kRow10 + 2, kCol10 - 2);
  EXPECT_THROW(
    mtrs::exportGeoTiff(s.layer().string(), s.out().string(), opts(box)), mtrs::ExportRefused);
  expectNothingWritten(s);
  const mtrs::ExportResult res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(), opts(box, 8));
  EXPECT_EQ(res.source_kind, "overview");
  EXPECT_EQ(readRaster(s.out()).at(0, 0, 0), 7.0f);
}

TEST(ExportSelection, BoxIntersectingNothingIsRefused)
{
  Store s("empty_box");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  try {
    mtrs::exportGeoTiff(
      s.layer().string(), s.out().string(), opts(insideTile(10, kRow10 + 50, kCol10)));
    FAIL() << "expected a refusal";
  } catch (const mtrs::ExportRefused & e) {
    EXPECT_NE(std::string(e.what()).find("intersects no native tile"), std::string::npos);
  }
  expectNothingWritten(s);
  // Also with an explicit level.
  EXPECT_THROW(
    mtrs::exportGeoTiff(
      s.layer().string(), s.out().string(), opts(insideTile(10, kRow10 + 50, kCol10), 10)),
    mtrs::ExportRefused);
  expectNothingWritten(s);
}

TEST(ExportSelection, MissingLayerDirectoryIsRefused)
{
  Store s("missing_layer");
  // Also with the output beside the would-be layer (the parent of a missing
  // layer is not a store): the error names the real problem.
  for (const fs::path & output : {s.out(), s.root() / "x.tif"}) {
    SCOPED_TRACE(output.string());
    try {
      mtrs::exportGeoTiff(
        (s.root() / "nope").string(), output.string(), opts(insideTile(10, kRow10, kCol10)));
      ADD_FAILURE() << "expected a refusal";
    } catch (const mtrs::ExportRefused & e) {
      EXPECT_NE(std::string(e.what()).find("layer directory does not exist"), std::string::npos)
        << e.what();
    }
    EXPECT_FALSE(fs::exists(output));
  }
  expectNothingWritten(s);
}

// --------------------------------------------------------------- refusals ----

TEST(ExportRefusals, PolarAndScaleBoundaryBoxes)
{
  Store s("polar");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  for (const mtrs::ExportBox & box : {mtrs::ExportBox{73.0, 10.0, 74.0, 11.0},
      mtrs::ExportBox{-75.0, 10.0, -73.0, 11.0}, mtrs::ExportBox{71.0, 10.0, 72.0, 11.0}})
  {
    try {
      mtrs::exportGeoTiff(s.layer().string(), s.out().string(), opts(box));
      FAIL() << "expected a refusal";
    } catch (const mtrs::ExportRefused & e) {
      EXPECT_NE(std::string(e.what()).find("|lat| >= 72"), std::string::npos) << e.what();
    }
    expectNothingWritten(s);
  }
  try {
    mtrs::exportGeoTiff(
      s.layer().string(), s.out().string(), opts(mtrs::ExportBox{70.0, 10.0, 75.0, 11.0}));
    FAIL() << "expected a refusal";
  } catch (const mtrs::ExportRefused & e) {
    EXPECT_NE(std::string(e.what()).find("longitude-scale boundary"), std::string::npos);
  }
  expectNothingWritten(s);
}

TEST(ExportRefusals, CorruptTileFailsTheWholeExport)
{
  Store s("corrupt");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  std::ofstream(s.layer() / mtrs::tileFilename(gridAt(10, kRow10, kCol10 + 1))) << "not a tiff";
  const double r0 = static_cast<double>(kRow10) * kEdge, c0 = static_cast<double>(kCol10) * kEdge;
  try {
    mtrs::exportGeoTiff(
      s.layer().string(), s.out().string(),
      opts(cellBox(10, r0 + 10, c0 + 10, r0 + 20, c0 + 2 * kEdge - 10)));
    FAIL() << "expected a failure";
  } catch (const std::runtime_error & e) {
    // GDAL's own reason travels with the message.
    EXPECT_NE(std::string(e.what()).find("(GDAL: "), std::string::npos) << e.what();
    EXPECT_NE(std::string(e.what()).find("not recognized"), std::string::npos) << e.what();
  }
  expectNothingWritten(s);
}

TEST(ExportRefusals, TileGeoreferencedUnlikeItsNameFails)
{
  Store s("misnamed");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  // A real level-10 tile whose content is (kRow10, kCol10+5), saved under the
  // name of (kRow10, kCol10+1): placing it by name would put it 4 tiles off.
  // The header probe's origin check catches it before anything is created
  // (the writer's own grid check after loadTile stays as a second line).
  {
    mtrs::TiledRasterTile<double> t(gridAt(10, kRow10, kCol10 + 5), 2, 1.0);
    mtrs::saveTile<double>(
      t, (s.layer() / mtrs::tileFilename(gridAt(10, kRow10, kCol10 + 1))).string(),
      {kNaN, kNaN});
  }
  const double r0 = static_cast<double>(kRow10) * kEdge, c0 = static_cast<double>(kCol10) * kEdge;
  try {
    mtrs::exportGeoTiff(
      s.layer().string(), s.out().string(),
      opts(cellBox(10, r0 + 10, c0 + 10, r0 + 20, c0 + 2 * kEdge - 10)));
    FAIL() << "expected a failure";
  } catch (const std::runtime_error & e) {
    const std::string msg = e.what();
    EXPECT_NE(msg.find("would shift its data"), std::string::npos) << msg;
    EXPECT_NE(msg.find("off by 3840 x 0 cells"), std::string::npos) << msg;
  }
  expectNothingWritten(s);
}

// loadTile recovers a tile's grid from its ORIGIN only. A tile with the right
// origin but another pixel size (or a rotation) would be stretched into place;
// the exporter's probe checks the pixel grid against the level and fails first.
TEST(ExportRefusals, TileWithTheWrongPixelSizeFails)
{
  const double cell = cellSpan(10);
  const std::vector<std::pair<std::string, std::array<double, 3>>> bad = {
    {"doubled pixel width", {2.0 * cell, 0.0, -cell}},
    {"halved pixel height", {cell, 0.0, -0.5 * cell}},
    {"rotated", {cell, 1e-6, -cell}},
  };
  for (const auto & item : bad) {
    SCOPED_TRACE(item.first);
    Store s("pixel_size");
    writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
    const gggs::GridIndex g = gridAt(10, kRow10, kCol10 + 1);
    writeConst(s.layer(), g, 2.0);
    {
      GDALAllRegister();
      const fs::path path = s.layer() / mtrs::tileFilename(g);
      GDALDataset * ds = GDALDataset::FromHandle(GDALOpen(path.c_str(), GA_Update));
      ASSERT_NE(ds, nullptr);
      double gt[6];
      ASSERT_EQ(ds->GetGeoTransform(gt), CE_None);
      gt[1] = item.second[0];
      gt[2] = item.second[1];
      gt[5] = item.second[2];
      ASSERT_EQ(ds->SetGeoTransform(gt), CE_None);
      ASSERT_EQ(GDALClose(ds), CE_None);
    }
    const double r0 = static_cast<double>(kRow10) * kEdge;
    const double c0 = static_cast<double>(kCol10) * kEdge;
    try {
      mtrs::exportGeoTiff(
        s.layer().string(), s.out().string(),
        opts(cellBox(10, r0 + 10, c0 + 10, r0 + 20, c0 + 2 * kEdge - 10)));
      ADD_FAILURE() << "expected a failure";
    } catch (const std::runtime_error & e) {
      EXPECT_NE(std::string(e.what()).find("cannot be placed by its origin"), std::string::npos)
        << e.what();
    }
    expectNothingWritten(s);
  }
}

// loadTile accepts an origin up to half a cell off and the writer places the
// tile on the grid, which would shift its data by that fraction silently. The
// probe checks the origin against the named grid's north-west corner.
TEST(ExportRefusals, TileWithAnOffGridOriginFails)
{
  const double cell = cellSpan(10);
  const std::vector<std::pair<std::string, std::array<double, 2>>> bad = {
    {"0.3 cell east", {0.3 * cell, 0.0}},
    {"0.3 cell south", {0.0, -0.3 * cell}},
    {"1e-4 cell north", {0.0, 1e-4 * cell}},
  };
  for (const auto & item : bad) {
    SCOPED_TRACE(item.first);
    Store s("off_grid_origin");
    writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
    const gggs::GridIndex g = gridAt(10, kRow10, kCol10 + 1);
    writeConst(s.layer(), g, 2.0);
    {
      GDALAllRegister();
      const fs::path path = s.layer() / mtrs::tileFilename(g);
      GDALDataset * ds = GDALDataset::FromHandle(GDALOpen(path.c_str(), GA_Update));
      ASSERT_NE(ds, nullptr);
      double gt[6];
      ASSERT_EQ(ds->GetGeoTransform(gt), CE_None);
      gt[0] += item.second[0];
      gt[3] += item.second[1];
      ASSERT_EQ(ds->SetGeoTransform(gt), CE_None);
      ASSERT_EQ(GDALClose(ds), CE_None);
    }
    const double r0 = static_cast<double>(kRow10) * kEdge;
    const double c0 = static_cast<double>(kCol10) * kEdge;
    try {
      mtrs::exportGeoTiff(
        s.layer().string(), s.out().string(),
        opts(cellBox(10, r0 + 10, c0 + 10, r0 + 20, c0 + 2 * kEdge - 10)));
      ADD_FAILURE() << "expected a failure";
    } catch (const std::runtime_error & e) {
      EXPECT_NE(std::string(e.what()).find("would shift its data"), std::string::npos)
        << e.what();
    }
    expectNothingWritten(s);
  }
}

TEST(ExportRefusals, MixedBandCounts)
{
  Store s("mixed_bands");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0, 2);
  writeConst(s.layer(), gridAt(10, kRow10, kCol10 + 1), 1.0, 3);
  const double r0 = static_cast<double>(kRow10) * kEdge, c0 = static_cast<double>(kCol10) * kEdge;
  try {
    mtrs::exportGeoTiff(
      s.layer().string(), s.out().string(),
      opts(cellBox(10, r0 + 10, c0 + 10, r0 + 20, c0 + 2 * kEdge - 10)));
    FAIL() << "expected a refusal";
  } catch (const mtrs::ExportRefused & e) {
    EXPECT_NE(std::string(e.what()).find("mixed band counts"), std::string::npos) << e.what();
  }
  expectNothingWritten(s);
}

TEST(ExportRefusals, NonFloatDtype)
{
  Store s("uint16");
  writeTile<std::uint16_t>(
    s.layer(), gridAt(10, kRow10, kCol10), 3,
    [](std::size_t, std::size_t, std::size_t) {return std::uint16_t{7};}, std::nullopt);
  try {
    mtrs::exportGeoTiff(
      s.layer().string(), s.out().string(), opts(insideTile(10, kRow10, kCol10)));
    FAIL() << "expected a refusal";
  } catch (const mtrs::ExportRefused & e) {
    const std::string msg = e.what();
    EXPECT_NE(msg.find("UInt16"), std::string::npos) << msg;
    EXPECT_NE(msg.find("only Float32/Float64"), std::string::npos) << msg;
  }
  expectNothingWritten(s);
}

// Pixels are read raw, so a finite (or infinite) no-data sentinel would be
// exported as data under the output's NaN no-data tag. Every float writer in
// the stores uses NaN; anything else is refused.
TEST(ExportRefusals, NonNanNoDataIsRefused)
{
  for (const double nodata : {-9999.0, std::numeric_limits<double>::infinity()}) {
    SCOPED_TRACE(nodata);
    Store s("finite_nodata");
    writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
    writeTile<double>(
      s.layer(), gridAt(10, kRow10, kCol10 + 1), 2,
      [](std::size_t, std::size_t c, std::size_t) {return c < 100 ? -9999.0 : 2.0;}, nodata);
    const double r0 = static_cast<double>(kRow10) * kEdge;
    const double c0 = static_cast<double>(kCol10) * kEdge;
    try {
      mtrs::exportGeoTiff(
        s.layer().string(), s.out().string(),
        opts(cellBox(10, r0 + 10, c0 + 10, r0 + 20, c0 + 2 * kEdge - 10)));
      ADD_FAILURE() << "expected a refusal";
    } catch (const mtrs::ExportRefused & e) {
      EXPECT_NE(std::string(e.what()).find("only NaN no-data can be exported"),
        std::string::npos) << e.what();
    }
    expectNothingWritten(s);
  }
}

TEST(ExportRefusals, OutputInsideTheLayerDirectory)
{
  Store s("inside_layer");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  EXPECT_THROW(
    mtrs::exportGeoTiff(
      s.layer().string(), (s.layer() / "x.tif").string(), opts(insideTile(10, kRow10, kCol10))),
    mtrs::ExportRefused);
  EXPECT_FALSE(fs::exists(s.layer() / "x.tif"));
  // A file named like the would-be .tmp inside the store is never touched.
  std::ofstream(s.layer() / "y.tif.tmp") << "not ours";
  EXPECT_THROW(
    mtrs::exportGeoTiff(
      s.layer().string(), (s.layer() / "y.tif").string(), opts(insideTile(10, kRow10, kCol10))),
    mtrs::ExportRefused);
  EXPECT_EQ(readFile(s.layer() / "y.tif.tmp"), "not ours");
}

// The guard compares symlink-resolved paths and covers the whole store
// directory (the one holding registry.json), not only the layer itself.
TEST(ExportRefusals, OutputAnywhereInTheStoreIsRefused)
{
  Store s("inside_store");
  s.writeRegistry(R"({"datum": "mllw"})");
  const gggs::GridIndex g = gridAt(10, kRow10, kCol10);
  writeConst(s.layer(), g, 1.0);
  writeConst(s.sidecar(), gridAt(9, kRow10 / 2, kCol10 / 2), 2.0);
  const fs::path tile = s.layer() / mtrs::tileFilename(g);
  const std::string tile_bytes = readFile(tile);
  const std::string registry_bytes = readFile(s.registry());
  const fs::path sibling = s.layer().parent_path() / "chart";
  fs::create_directories(sibling);
  const fs::path link_dir = s.root() / "links";
  fs::create_directories(link_dir);
  const fs::path layer_link = link_dir / "processed_link";
  fs::create_directory_symlink(s.layer(), layer_link);
  const fs::path out_link = s.outDir() / "alias.tif";
  fs::create_symlink(tile, out_link);
  const mtrs::ExportOptions o = opts(insideTile(10, kRow10, kCol10));

  struct Case
  {
    std::string what;
    fs::path layer;
    fs::path output;
    fs::path must_not_exist;
  };
  const std::vector<Case> cases = {
    {"layer itself", s.layer(), s.layer() / "x.tif", s.layer() / "x.tif"},
    {"sidecar", s.layer(), s.sidecar() / "x.tif", s.sidecar() / "x.tif"},
    {"subdirectory of the layer", s.layer(), s.layer() / "sub" / "x.tif", s.layer() / "sub"},
    {"sibling layer", s.layer(), sibling / "x.tif", sibling / "x.tif"},
    {"store directory", s.layer(), s.layer().parent_path() / "x.tif",
      s.layer().parent_path() / "x.tif"},
    {"registry.json", s.layer(), s.registry(), {}},
    {"output through a symlink to the layer", s.layer(), layer_link / "x.tif",
      s.layer() / "x.tif"},
    {"layer given through a symlink", layer_link, s.layer() / "x.tif", s.layer() / "x.tif"},
    {"output path is a symlink to a native tile", s.layer(), out_link, {}},
    {"output path replaces a native tile through a symlinked layer", s.layer(),
      layer_link / mtrs::tileFilename(g), {}},
  };
  for (const Case & c : cases) {
    SCOPED_TRACE(c.what);
    try {
      mtrs::exportGeoTiff(c.layer.string(), c.output.string(), o);
      ADD_FAILURE() << "expected a refusal";
    } catch (const mtrs::ExportRefused & e) {
      EXPECT_NE(std::string(e.what()).find("inside the store"), std::string::npos) << e.what();
    }
    if (!c.must_not_exist.empty()) {
      EXPECT_FALSE(fs::exists(c.must_not_exist));
    }
  }
  EXPECT_EQ(readFile(tile), tile_bytes) << "a native tile was modified";
  EXPECT_EQ(readFile(s.registry()), registry_bytes) << "registry.json was modified";
  EXPECT_TRUE(fs::is_symlink(out_link));
  // An output beside the store (a sibling of its directory) is fine.
  const fs::path beside = s.root() / "beside.tif";
  mtrs::exportGeoTiff(layer_link.string(), beside.string(), o);
  EXPECT_TRUE(fs::exists(beside));
}

// The store is found by its registry.json, not by position: with the layer
// nested one directory deeper than usual, the registry beside its group
// directory is still inside the guarded store.
TEST(ExportRefusals, StoreRootIsTheNearestDirectoryHoldingTheRegistry)
{
  Store s("nested_store");
  s.writeRegistry(R"({"datum": "mllw"})");
  const fs::path nested = s.layer().parent_path() / "group" / "processed";
  writeConst(nested, gridAt(10, kRow10, kCol10), 1.0);
  const std::string registry_bytes = readFile(s.registry());
  const mtrs::ExportOptions o = opts(insideTile(10, kRow10, kCol10));
  for (const fs::path & output : {s.registry(), s.layer().parent_path() / "x.tif",
      s.layer() / "x.tif"})
  {
    SCOPED_TRACE(output.string());
    try {
      mtrs::exportGeoTiff(nested.string(), output.string(), o);
      ADD_FAILURE() << "expected a refusal";
    } catch (const mtrs::ExportRefused & e) {
      EXPECT_NE(std::string(e.what()).find("inside the store"), std::string::npos) << e.what();
    }
  }
  EXPECT_EQ(readFile(s.registry()), registry_bytes) << "registry.json was modified";
  EXPECT_FALSE(fs::exists(s.layer() / "x.tif"));
  mtrs::exportGeoTiff(nested.string(), s.out().string(), o);
  EXPECT_TRUE(fs::exists(s.out()));
}

// A stray registry.json INSIDE a layer is not its store: the search starts at
// the layer's parent, so the real store above still guards a sibling layer.
// (Searching from the layer itself made store/sub/layerC the "store" and let
// an output into store/layerB through.)
TEST(ExportRefusals, StrayRegistryInsideTheLayerDoesNotShadowTheStore)
{
  Store s("stray_registry");
  s.writeRegistry(R"({"datum": "mllw"})");
  const fs::path store = s.registry().parent_path();
  const fs::path layer_c = store / "sub" / "layerC";
  writeConst(layer_c, gridAt(10, kRow10, kCol10), 1.0);
  std::ofstream(layer_c / "registry.json") << R"({"datum": "lat"})";
  const fs::path layer_b = store / "layerB";
  fs::create_directories(layer_b);
  const mtrs::ExportOptions o = opts(insideTile(10, kRow10, kCol10));
  for (const fs::path & output : {layer_b / "x.tif", s.registry(), store / "sub" / "x.tif",
      layer_c / "x.tif"})
  {
    SCOPED_TRACE(output.string());
    try {
      mtrs::exportGeoTiff(layer_c.string(), output.string(), o);
      ADD_FAILURE() << "expected a refusal";
    } catch (const mtrs::ExportRefused & e) {
      EXPECT_NE(std::string(e.what()).find("inside the store"), std::string::npos) << e.what();
    }
  }
  EXPECT_FALSE(fs::exists(layer_b / "x.tif"));
  EXPECT_EQ(readFile(s.registry()), R"({"datum": "mllw"})");
  mtrs::exportGeoTiff(layer_c.string(), s.out().string(), o);
  EXPECT_TRUE(fs::exists(s.out()));
}

// An overviews/ sidecar given as the layer is refused outright, whatever the
// output: read as a layer, its derived tiles would be labelled native and the
// store guard would be anchored one level too low (the reviewer's reproduction
// turned registry.json into a TIFF this way).
TEST(ExportRefusals, OverviewsSidecarGivenAsTheLayerIsRefused)
{
  Store s("overviews_as_layer");
  s.writeRegistry(R"({"datum": "mllw"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  writeConst(s.sidecar(), gridAt(9, kRow10 / 2, kCol10 / 2), 2.0);
  const std::string registry_bytes = readFile(s.registry());
  const fs::path sidecar_link = s.root() / "sidecar_link";
  fs::create_directory_symlink(s.sidecar(), sidecar_link);
  const mtrs::ExportOptions o = opts(insideTile(9, kRow10 / 2, kCol10 / 2), 9);
  const std::string slashed = s.sidecar().string() + "/";
  // The suggested layer is the directory really holding the sidecar, also for
  // the symlink (whose own directory is the store root, not a layer).
  const std::string suggested = "(" + fs::canonical(s.layer()).string() + ")";
  for (const std::string & layer : {s.sidecar().string(), slashed, sidecar_link.string()}) {
    for (const fs::path & output : {s.registry(), s.out()}) {
      SCOPED_TRACE(layer + " -> " + output.string());
      try {
        mtrs::exportGeoTiff(layer, output.string(), o);
        ADD_FAILURE() << "expected a refusal";
      } catch (const mtrs::ExportRefused & e) {
        EXPECT_NE(
          std::string(e.what()).find("its overviews/ sidecar is read automatically"),
          std::string::npos) << e.what();
        EXPECT_NE(std::string(e.what()).find(suggested), std::string::npos) << e.what();
      }
    }
  }
  EXPECT_EQ(readFile(s.registry()), registry_bytes) << "registry.json was modified";
  expectNothingWritten(s);
}

// A symlinked layer whose LINK lives in another store's directory: both the
// store holding the link and the store holding the target are guarded.
TEST(ExportRefusals, SymlinkedLayerGuardsBothTheLinksStoreAndTheTargetsStore)
{
  Store s("link_in_other_store");
  s.writeRegistry(R"({"datum": "mllw"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  const fs::path other = s.root() / "other";
  fs::create_directories(other);
  std::ofstream(other / "registry.json") << R"({"datum": "lat"})";
  const fs::path link = other / "linked";
  fs::create_directory_symlink(s.layer(), link);
  const mtrs::ExportOptions o = opts(insideTile(10, kRow10, kCol10));
  for (const fs::path & output : {other / "x.tif", other / "registry.json",
      s.layer().parent_path() / "x.tif", s.registry()})
  {
    SCOPED_TRACE(output.string());
    try {
      mtrs::exportGeoTiff(link.string(), output.string(), o);
      ADD_FAILURE() << "expected a refusal";
    } catch (const mtrs::ExportRefused & e) {
      EXPECT_NE(std::string(e.what()).find("inside the store"), std::string::npos) << e.what();
    }
  }
  EXPECT_FALSE(fs::exists(other / "x.tif"));
  EXPECT_FALSE(fs::exists(s.layer().parent_path() / "x.tif"));
  EXPECT_EQ(readFile(other / "registry.json"), R"({"datum": "lat"})");
  mtrs::exportGeoTiff(link.string(), s.out().string(), o);
  EXPECT_TRUE(fs::exists(s.out()));
}

// A plain layer with no registry.json anywhere above it still exports: its
// parent is guarded, and an output beside that parent is fine.
TEST(ExportRefusals, LayerWithNoRegistryAnywhereStillExports)
{
  Store s("no_registry_anywhere");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  const mtrs::ExportOptions o = opts(insideTile(10, kRow10, kCol10));
  EXPECT_THROW(
    mtrs::exportGeoTiff(
      s.layer().string(), (s.layer().parent_path() / "x.tif").string(), o),
    mtrs::ExportRefused);
  const fs::path beside = s.root() / "beside.tif";
  const mtrs::ExportResult res = mtrs::exportGeoTiff(s.layer().string(), beside.string(), o);
  EXPECT_EQ(res.source_level, 10);
  EXPECT_EQ(readRaster(beside).at(0, 0, 0), 1.0f);
}

TEST(ExportRefusals, InvalidOptions)
{
  Store s("invalid_options");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  mtrs::ExportOptions o = opts(insideTile(10, kRow10, kCol10));
  o.max_pixels.reset();
  EXPECT_THROW(
    mtrs::exportGeoTiff(s.layer().string(), s.out().string(), o), std::invalid_argument);
  o = opts(mtrs::ExportBox{43.1, -70.6, 43.0, -70.5});   // lat_min > lat_max
  EXPECT_THROW(
    mtrs::exportGeoTiff(s.layer().string(), s.out().string(), o), std::invalid_argument);
  o = opts(insideTile(10, kRow10, kCol10), 21);
  EXPECT_THROW(
    mtrs::exportGeoTiff(s.layer().string(), s.out().string(), o), std::invalid_argument);
  expectNothingWritten(s);
}

// --------------------------------------------------------- output handling ----

TEST(ExportOutput, ExistingOutputReplacedOnSuccessUntouchedOnRefusal)
{
  Store s("existing_output");
  s.writeRegistry(R"({"datum": "mllw"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  std::ofstream(s.out()) << "previous";
  // Refusal: the existing output is untouched.
  EXPECT_THROW(
    mtrs::exportGeoTiff(
      s.layer().string(), s.out().string(), opts(insideTile(10, kRow10 + 50, kCol10))),
    mtrs::ExportRefused);
  EXPECT_EQ(readFile(s.out()), "previous");
  EXPECT_TRUE(tmpFiles(s).empty());
  // Success: replaced by the rename.
  mtrs::exportGeoTiff(s.layer().string(), s.out().string(), opts(insideTile(10, kRow10, kCol10)));
  EXPECT_EQ(readRaster(s.out()).at(0, 0, 0), 1.0f);
  EXPECT_TRUE(tmpFiles(s).empty());
}

// Replacing an output must not leave the previous file's GDAL sidecars attached
// to the new one: old statistics in `.aux.xml`, `gdaladdo` overviews of a
// larger raster in `.ovr`, an external `.msk` mask. Only the output's own
// sidecar names are removed; look-alikes of other names survive.
TEST(ExportOutput, ReplacedOutputsStaleSidecarsAreRemoved)
{
  Store s("stale_sidecars");
  s.writeRegistry(R"({"datum": "mllw"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  writeConst(s.layer(), gridAt(10, kRow10, kCol10 + 1), 5.0);
  const double r0 = static_cast<double>(kRow10) * kEdge, c0 = static_cast<double>(kCol10) * kEdge;
  // First export: both tiles (values 1 and 5), then statistics, overviews and a mask.
  mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(),
    opts(cellBox(10, r0 + 10, c0 + 10, r0 + 600, c0 + 2 * kEdge - 10)));
  GDALAllRegister();
  {
    GDALDataset * ds = GDALDataset::FromHandle(GDALOpen(s.out().c_str(), GA_ReadOnly));
    ASSERT_NE(ds, nullptr);
    double mn = 0, mx = 0, mean = 0, sd = 0;
    ASSERT_EQ(ds->GetRasterBand(1)->ComputeStatistics(false, &mn, &mx, &mean, &sd, nullptr,
      nullptr), CE_None);
    const int factors[] = {2, 4};
    ASSERT_EQ(ds->BuildOverviews("NEAREST", 2, factors, 0, nullptr, nullptr, nullptr), CE_None);
    ASSERT_EQ(ds->CreateMaskBand(GMF_PER_DATASET), CE_None);
    GDALClose(ds);
  }
  fs::path aux = s.out(), ovr = s.out(), msk = s.out();
  aux += ".aux.xml";
  ovr += ".ovr";
  msk += ".msk";
  ASSERT_TRUE(fs::exists(aux));
  ASSERT_TRUE(fs::exists(ovr));
  ASSERT_TRUE(fs::exists(msk));
  // Look-alikes that are not the output's own sidecars.
  const fs::path stem_ovr = s.outDir() / "export.ovr";
  const fs::path other_ovr = s.outDir() / "other.tif.ovr";
  fs::path backup = s.out();
  backup += ".ovr.bak";
  for (const fs::path & p : {stem_ovr, other_ovr, backup}) {
    std::ofstream(p) << "keep";
  }
  // Second export: the west tile only (value 1), a smaller raster.
  mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(),
    opts(cellBox(10, r0 + 10, c0 + 10, r0 + 300, c0 + 500)));
  EXPECT_FALSE(fs::exists(aux)) << "stale statistics survived";
  EXPECT_FALSE(fs::exists(ovr)) << "stale overviews survived";
  EXPECT_FALSE(fs::exists(msk)) << "stale mask survived";
  for (const fs::path & p : {stem_ovr, other_ovr, backup}) {
    EXPECT_EQ(readFile(p), "keep") << p;
  }
  GDALDataset * ds = GDALDataset::FromHandle(GDALOpen(s.out().c_str(), GA_ReadOnly));
  ASSERT_NE(ds, nullptr);
  GDALRasterBand * band = ds->GetRasterBand(1);
  EXPECT_EQ(band->GetOverviewCount(), 0);
  EXPECT_EQ(band->GetMetadataItem("STATISTICS_MAXIMUM"), nullptr);
  EXPECT_NE(band->GetMaskFlags() & GMF_NODATA, 0) << "the mask must come from the NaN no-data";
  GDALClose(ds);
  EXPECT_EQ(readRaster(s.out()).width, 490);
}

// A failure AFTER the temporary file and dataset exist (every other
// existing-output test fails before Create): a truncated tile has a valid
// header, so it passes the probe, and fails only when its pixels are read
// during the strip loop. The existing output must be byte-identical and no
// temporary file may remain.
TEST(ExportOutput, FailureAfterCreateLeavesTheExistingOutputUntouched)
{
  Store s("fail_after_create");
  s.writeRegistry(R"({"datum": "mllw"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  const gggs::GridIndex bad = gridAt(10, kRow10, kCol10 + 1);
  // Varying values so the compressed pixel data is large and lies after the
  // header (GDAL writes the IFD first): cutting the file in half keeps the
  // header readable and loses pixel strips.
  writeTile<double>(
    s.layer(), bad, 2,
    [](std::size_t r, std::size_t c, std::size_t b) {
      return std::sin(0.37 * static_cast<double>(r * kEdge + c)) + static_cast<double>(b);
    },
    kNaN);
  const fs::path bad_path = s.layer() / mtrs::tileFilename(bad);
  fs::resize_file(bad_path, fs::file_size(bad_path) / 2);
  const std::string previous = "previous output, not a GeoTIFF";
  std::ofstream(s.out()) << previous;
  const double r0 = static_cast<double>(kRow10) * kEdge, c0 = static_cast<double>(kCol10) * kEdge;
  try {
    mtrs::exportGeoTiff(
      s.layer().string(), s.out().string(),
      opts(cellBox(10, r0 + 10, c0 + 10, r0 + 20, c0 + 2 * kEdge - 10)));
    FAIL() << "expected a failure";
  } catch (const mtrs::ExportRefused & e) {
    FAIL() << "the truncated tile was refused by the probe, before Create: " << e.what();
  } catch (const std::runtime_error & e) {
    // Failed in loadTile's pixel read, i.e. after the dataset was created, and
    // GDAL's own reason travels with tile_io's step name.
    EXPECT_NE(std::string(e.what()).find("read band"), std::string::npos) << e.what();
    EXPECT_NE(std::string(e.what()).find("(GDAL: "), std::string::npos) << e.what();
    EXPECT_NE(std::string(e.what()).find(bad_path.string()), std::string::npos) << e.what();
  }
  EXPECT_EQ(readFile(s.out()), previous);
  EXPECT_TRUE(tmpFiles(s).empty()) << "a failed export left its temporary file";
}

// The temporary file is unique and exclusively created, and only this run's
// own file is ever removed: files that merely look like one (a user's file, a
// crashed run's debris, another run's in-flight file) survive a refusal and a
// success alike. There is deliberately no stale-file cleanup.
TEST(ExportOutput, ForeignTmpLookalikesAreNeverTouched)
{
  Store s("foreign_tmp");
  s.writeRegistry(R"({"datum": "mllw"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  const fs::path plain = s.tmpPrefix();
  fs::path suffixed = s.tmpPrefix();
  suffixed += ".abc123";
  std::ofstream(plain) << "a user's file";
  std::ofstream(suffixed) << "another run's file";
  const std::set<std::string> foreign = {
    plain.filename().string(), suffixed.filename().string()};
  EXPECT_THROW(
    mtrs::exportGeoTiff(
      s.layer().string(), s.out().string(), opts(insideTile(10, kRow10 + 50, kCol10))),
    mtrs::ExportRefused);
  EXPECT_FALSE(fs::exists(s.out()));
  EXPECT_EQ(tmpFiles(s), foreign);
  mtrs::exportGeoTiff(s.layer().string(), s.out().string(), opts(insideTile(10, kRow10, kCol10)));
  EXPECT_EQ(readRaster(s.out()).at(0, 0, 0), 1.0f);
  EXPECT_EQ(tmpFiles(s), foreign) << "only this run's own temporary file may be removed";
  EXPECT_EQ(readFile(plain), "a user's file");
  EXPECT_EQ(readFile(suffixed), "another run's file");
}

// Exclusive creation must not cost the output its normal mode: it is created
// under the umask like any new file, not 0600 as mkstemp would leave it.
TEST(ExportOutput, OutputModeFollowsTheUmask)
{
  Store s("output_mode");
  s.writeRegistry(R"({"datum": "mllw"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  const fs::path reference = s.outDir() / "reference";
  std::ofstream(reference) << "x";
  mtrs::exportGeoTiff(s.layer().string(), s.out().string(), opts(insideTile(10, kRow10, kCol10)));
  EXPECT_EQ(fs::status(s.out()).permissions(), fs::status(reference).permissions());
}

// Several exports aimed at ONE output at the same time: each writes its own
// temporary file, so none can rename another's half-written file or unlink it
// mid-write. Every run succeeds, the output is a complete raster, and no
// temporary file is left.
TEST(ExportOutput, ConcurrentExportsToOneOutputDoNotCollide)
{
  Store s("concurrent");
  s.writeRegistry(R"({"datum": "mllw"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  constexpr int kRuns = 4;
  std::vector<std::string> errors(kRuns);
  std::vector<std::thread> runs;
  for (int i = 0; i < kRuns; ++i) {
    runs.emplace_back(
      [&s, &errors, i]() {
        try {
          mtrs::exportGeoTiff(
            s.layer().string(), s.out().string(), opts(insideTile(10, kRow10, kCol10)));
        } catch (const std::exception & e) {
          errors[i] = e.what();
        }
      });
  }
  for (std::thread & t : runs) {
    t.join();
  }
  for (int i = 0; i < kRuns; ++i) {
    EXPECT_EQ(errors[i], "") << "run " << i;
  }
  const mtrs::CellWindow w = mtrs::snapBoxToLevel(insideTile(10, kRow10, kCol10), 10);
  const Raster out = readRaster(s.out());
  EXPECT_EQ(static_cast<std::uint64_t>(out.width), w.col_end - w.col_begin);
  EXPECT_EQ(static_cast<std::uint64_t>(out.height), w.row_end - w.row_begin);
  EXPECT_EQ(out.at(0, out.width - 1, out.height - 1), 1.0f);
  EXPECT_TRUE(tmpFiles(s).empty());
}

// An overview rebuild publishes a new overviews/ by rename while an export is
// reading the old one: the export would mix two sidecar generations, so it
// fails, leaving the existing output untouched and no temporary file. The swap
// is made from another thread as soon as this export's temporary file exists
// (the export is large enough to still be writing then).
TEST(ExportOutput, SidecarSwappedDuringTheExportFailsIt)
{
  Store s("sidecar_swap");
  s.writeRegistry(R"({"datum": "mllw"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  const gggs::GridIndex g9 = gridAt(9, kRow10 / 2, kCol10 / 2);
  writeConst(s.sidecar(), g9, 2.0);
  const fs::path staged = s.layer() / "overviews.tmp";
  writeConst(staged, g9, 3.0);
  std::ofstream(s.out()) << "previous";
  const double r9 = static_cast<double>(g9.row()) * kEdge;
  const double c9 = static_cast<double>(g9.column()) * kEdge;
  const mtrs::ExportOptions o = opts(cellBox(9, r9 + 10, c9 + 10, r9 + 16010, c9 + 3010), 9);
  std::atomic<bool> swapped{false};
  std::atomic<bool> done{false};
  std::thread rebuild([&]() {
      while (!done && tmpFiles(s).empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      if (done) {
        return;
      }
      // rename-aside, as the builders' fallback does
      fs::rename(s.sidecar(), s.layer() / "overviews.old");
      fs::rename(staged, s.sidecar());
      swapped = true;
    });
  std::string error;
  try {
    mtrs::exportGeoTiff(s.layer().string(), s.out().string(), o);
  } catch (const std::runtime_error & e) {
    error = e.what();
  }
  done = true;
  rebuild.join();
  ASSERT_TRUE(swapped) << "the export finished before the swap could be made";
  EXPECT_NE(error.find("was replaced during the export"), std::string::npos) << error;
  EXPECT_EQ(readFile(s.out()), "previous");
  EXPECT_TRUE(tmpFiles(s).empty());
}

// A SIGHUP observer for the commit-window test: records, at delivery, how
// many exports had replaced their output and whether the stale sidecar was
// still there. Only async-signal-safe calls (atomic stores, access(2)).
namespace
{
std::atomic<int> g_hup_seen{0};
std::atomic<std::uint64_t> g_hup_replaced{0};
std::atomic<int> g_hup_sidecar_present{-1};
char g_hup_sidecar[4096];
extern "C" void onTestHup(int)
{
  g_hup_replaced.store(mtrs::exportOutputsReplaced());
  g_hup_sidecar_present.store(::access(g_hup_sidecar, F_OK) == 0 ? 1 : 0);
  g_hup_seen.store(1);
}
}  // namespace

// A signal sent the moment the rename is visible is held back until the
// export has counted the replacement and removed the old sidecars: a handler
// then never sees "no output replaced" (the CLI would print "nothing written"
// over a replaced output) or the old .aux.xml still attached to the new raster.
// The signal goes to the exporting thread as soon as another thread sees the
// output's inode change, so it usually lands inside the blocked window; when
// it lands after the export returned, the same invariants hold.
TEST(ExportOutput, SignalDuringTheRenameReportsTheOutputReplaced)
{
  Store s("signal_commit_window");
  s.writeRegistry(R"({"datum": "mllw"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  std::ofstream(s.out()) << "previous";
  const fs::path aux = s.out().string() + ".aux.xml";
  std::ofstream(aux) << "<PAMDataset/>";
  ASSERT_LT(aux.native().size(), sizeof(g_hup_sidecar));
  std::memcpy(g_hup_sidecar, aux.c_str(), aux.native().size() + 1);
  struct stat before_st;
  ASSERT_EQ(::stat(s.out().c_str(), &before_st), 0);

  // A refusal does not count as a replaced output.
  const std::uint64_t before = mtrs::exportOutputsReplaced();
  EXPECT_THROW(
    mtrs::exportGeoTiff(
      s.layer().string(), s.out().string(), opts(insideTile(10, kRow10 + 50, kCol10))),
    mtrs::ExportRefused);
  EXPECT_EQ(mtrs::exportOutputsReplaced(), before);

  struct sigaction sa, old_sa;
  std::memset(&sa, 0, sizeof(sa));
  sa.sa_handler = onTestHup;
  sigemptyset(&sa.sa_mask);
  ASSERT_EQ(::sigaction(SIGHUP, &sa, &old_sa), 0);
  g_hup_seen = 0;
  g_hup_sidecar_present = -1;
  const pthread_t exporter = pthread_self();
  std::atomic<bool> done{false}, sent{false};
  std::thread watcher([&]() {
      // Spin without sleeping, to catch the rename as early as possible.
      while (!done) {
        struct stat st;
        if (::stat(s.out().c_str(), &st) == 0 && st.st_ino != before_st.st_ino) {
          pthread_kill(exporter, SIGHUP);
          sent = true;
          return;
        }
      }
    });
  const double r0 = static_cast<double>(kRow10) * kEdge;
  const double c0 = static_cast<double>(kCol10) * kEdge;
  std::string error;
  try {
    mtrs::exportGeoTiff(
      s.layer().string(), s.out().string(),
      opts(cellBox(10, r0 + 10, c0 + 10, r0 + 4010, c0 + 3010)));
  } catch (const std::exception & e) {
    error = e.what();
  }
  done = true;
  watcher.join();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (sent && !g_hup_seen && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ::sigaction(SIGHUP, &old_sa, nullptr);
  ASSERT_TRUE(error.empty()) << error;
  ASSERT_TRUE(sent) << "the rename was never seen";
  ASSERT_TRUE(g_hup_seen) << "the signal was never delivered";
  EXPECT_EQ(g_hup_replaced.load(), before + 1) <<
    "a handler ran with the output replaced but not yet counted";
  EXPECT_EQ(g_hup_sidecar_present.load(), 0) <<
    "a handler ran before the old sidecar was removed";
  EXPECT_EQ(mtrs::exportOutputsReplaced(), before + 1);
  EXPECT_FALSE(fs::exists(aux));
  EXPECT_EQ(readRaster(s.out()).bands, 2);
  EXPECT_TRUE(tmpFiles(s).empty());
}

// ------------------------------------------------------------------ datum ----

TEST(ExportDatum, EmptyDatumWarnsAndLabelsEllipsoidal)
{
  Store s("empty_datum");
  s.writeRegistry(R"({"date": "", "datum": "", "platform": "bizzyboat"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  const mtrs::ExportResult res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(), opts(insideTile(10, kRow10, kCol10)));
  EXPECT_TRUE(hasWarning(res, "source datum conversion not recorded"));
  EXPECT_FALSE(hasWarning(res, "datum unknown"));
  // An empty datum also means "already ellipsoidal": the warning says it is
  // ambiguous rather than simply missing.
  EXPECT_TRUE(hasWarning(res, "ambiguous"));
  EXPECT_TRUE(hasWarning(res, "ALREADY ellipsoidal"));
  EXPECT_EQ(res.vertical_datum, "WGS84_ELLIPSOIDAL");
  ASSERT_TRUE(res.source_vertical_datum.has_value());
  EXPECT_EQ(*res.source_vertical_datum, "UNKNOWN");
  const Raster out = readRaster(s.out());
  EXPECT_EQ(out.md.at("VERTICAL_DATUM"), "WGS84_ELLIPSOIDAL");
  EXPECT_EQ(out.md.at("SOURCE_VERTICAL_DATUM"), "UNKNOWN");
}

TEST(ExportDatum, RecordedDatumIsTheSourceDatum)
{
  Store s("recorded_datum");
  s.writeRegistry(R"({"datum": "mllw"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  const mtrs::ExportResult res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(), opts(insideTile(10, kRow10, kCol10)));
  EXPECT_FALSE(hasWarning(res, "source datum conversion not recorded"));
  EXPECT_EQ(res.vertical_datum, "WGS84_ELLIPSOIDAL");
  EXPECT_EQ(res.source_vertical_datum.value_or(""), "mllw");
  const Raster out = readRaster(s.out());
  EXPECT_EQ(out.md.at("VERTICAL_DATUM"), "WGS84_ELLIPSOIDAL");
  EXPECT_EQ(out.md.at("SOURCE_VERTICAL_DATUM"), "mllw");
}

TEST(ExportDatum, AbsentOrUnparsableRegistryWarnsNeverRefuses)
{
  Store s("absent_registry");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  mtrs::ExportResult res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(), opts(insideTile(10, kRow10, kCol10)));
  EXPECT_TRUE(hasWarning(res, "source datum conversion not recorded"));
  EXPECT_TRUE(hasWarning(res, "registry not found"));
  EXPECT_FALSE(hasWarning(res, "ambiguous")) << "only an empty datum is ambiguous";
  EXPECT_EQ(res.source_vertical_datum.value_or(""), "UNKNOWN");
  s.writeRegistry("{ this is not json");
  res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(), opts(insideTile(10, kRow10, kCol10)));
  EXPECT_TRUE(hasWarning(res, "not a parsable JSON object"));
  EXPECT_EQ(res.source_vertical_datum.value_or(""), "UNKNOWN");
}

TEST(ExportDatum, TrailingSlashLayerDirFindsTheParentRegistry)
{
  Store s("trailing_slash");
  s.writeRegistry(R"({"datum": "mllw"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  const mtrs::ExportResult res = mtrs::exportGeoTiff(
    s.layer().string() + "/", s.out().string(), opts(insideTile(10, kRow10, kCol10)));
  EXPECT_EQ(res.source_vertical_datum.value_or(""), "mllw");
}

TEST(ExportDatum, SymlinkedLayerPathFindsTheStoresOwnRegistry)
{
  Store s("symlinked_registry");
  s.writeRegistry(R"({"datum": "mllw"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  // A link in another directory that holds a decoy registry: the lexical parent
  // of the link would read the decoy.
  const fs::path elsewhere = s.root() / "elsewhere";
  fs::create_directories(elsewhere);
  std::ofstream(elsewhere / "registry.json") << R"({"datum": "decoy"})";
  const fs::path link = elsewhere / "processed_link";
  fs::create_directory_symlink(s.layer(), link);
  EXPECT_EQ(
    fs::path(mtrs::registryPathForLayer(link.string())),
    fs::weakly_canonical(s.registry()));
  EXPECT_EQ(
    fs::path(mtrs::registryPathForLayer(link.string() + "/")),
    fs::weakly_canonical(s.registry()));
  const mtrs::ExportResult res = mtrs::exportGeoTiff(
    link.string(), s.out().string(), opts(insideTile(10, kRow10, kCol10)));
  EXPECT_EQ(res.source_vertical_datum.value_or(""), "mllw");
}

TEST(ExportDatum, BackscatterIsExempt)
{
  Store s("backscatter_datum");
  s.writeRegistry(R"({"calibration_ref": ""})");   // no datum field at all
  writeTile<float>(
    s.layer(), gridAt(10, kRow10, kCol10), 3,
    [](std::size_t, std::size_t, std::size_t b) {return -20.0f - static_cast<float>(b);},
    std::numeric_limits<float>::quiet_NaN());
  const mtrs::ExportResult res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(), opts(insideTile(10, kRow10, kCol10)));
  EXPECT_TRUE(res.warnings.empty());
  EXPECT_EQ(res.vertical_datum, "NOT_APPLICABLE");
  EXPECT_FALSE(res.source_vertical_datum.has_value());
  const Raster out = readRaster(s.out());
  EXPECT_EQ(out.md.at("VERTICAL_DATUM"), "NOT_APPLICABLE");
  EXPECT_EQ(out.md.count("SOURCE_VERTICAL_DATUM"), 0u);
}

// ----------------------------------------------------------------- values ----

TEST(ExportValues, ThreeBandFloat32WithBandNames)
{
  Store s("three_band");
  writeTile<float>(
    s.layer(), gridAt(10, kRow10, kCol10), 3,
    [](std::size_t r, std::size_t c, std::size_t b) {
      return (r == 500 && c == 500) ? std::numeric_limits<float>::quiet_NaN() :
             -30.25f + static_cast<float>(b);
    },
    std::numeric_limits<float>::quiet_NaN());
  const double r0 = static_cast<double>(kRow10) * kEdge, c0 = static_cast<double>(kCol10) * kEdge;
  const mtrs::ExportResult res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(), opts(cellBox(10, r0, c0, r0 + kEdge, c0 + kEdge)));
  EXPECT_EQ(res.band_count, 3u);
  EXPECT_EQ(res.downcast, "NONE");
  EXPECT_EQ(res.nodata_count, (std::vector<std::uint64_t>{1, 1, 1}));
  const Raster out = readRaster(s.out());
  ASSERT_EQ(out.bands, 3);
  EXPECT_EQ(
    out.descriptions,
    (std::vector<std::string>{"mean", "scaled_standard_error", "sample_sd"}));
  EXPECT_EQ(out.md.at("BAND_NAMES"), "mean,scaled_standard_error,sample_sd");
  EXPECT_EQ(out.md.at("DOWNCAST"), "NONE");
  for (int b = 0; b < 3; ++b) {
    EXPECT_EQ(out.types[b], GDT_Float32);
    EXPECT_EQ(out.at(b, 0, 0), -30.25f + static_cast<float>(b));
    EXPECT_TRUE(std::isnan(out.at(b, 500, 959 - 500)));   // GGGS row 500 -> north-up row 459
  }
}

TEST(ExportValues, Float64CastToFloat32PreservingNaN)
{
  Store s("downcast");
  s.writeRegistry(R"({"datum": "mllw"})");
  const double depth = -12.3456789012345;
  const double sigma = 0.123456789012345;
  writeTile<double>(
    s.layer(), gridAt(10, kRow10, kCol10), 2,
    [depth, sigma](std::size_t r, std::size_t c, std::size_t b) {
      if (r == 0 && c == 0) {
        return kNaN;
      }
      return b == 0 ? depth : sigma;
    },
    kNaN);
  const double r0 = static_cast<double>(kRow10) * kEdge, c0 = static_cast<double>(kCol10) * kEdge;
  const mtrs::ExportResult res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(), opts(cellBox(10, r0, c0, r0 + kEdge, c0 + kEdge)));
  EXPECT_EQ(res.downcast, "Float64->Float32");
  EXPECT_EQ(res.band_names, (std::vector<std::string>{"depth", "uncertainty"}));
  const Raster out = readRaster(s.out());
  EXPECT_EQ(out.types[0], GDT_Float32);
  EXPECT_EQ(out.types[1], GDT_Float32);
  EXPECT_EQ(out.descriptions, (std::vector<std::string>{"depth", "uncertainty"}));
  EXPECT_EQ(out.md.at("DOWNCAST"), "Float64->Float32");
  EXPECT_EQ(out.at(0, 1, 1), static_cast<float>(depth));
  EXPECT_EQ(out.at(1, 1, 1), static_cast<float>(sigma));
  // GGGS (0, 0) is the south-west cell: output bottom-left.
  EXPECT_TRUE(std::isnan(out.at(0, 0, out.height - 1)));
  EXPECT_TRUE(std::isnan(out.at(1, 0, out.height - 1)));
  EXPECT_EQ(out.md.at("NODATA_COUNT"), "1");
  EXPECT_EQ(out.md.at("SOURCE_LEVEL"), "10");
  EXPECT_EQ(out.md.at("SOURCE_KIND"), "native");
  EXPECT_DOUBLE_EQ(std::stod(out.md.at("GEOMETRIC_ERROR_M")), gggs::Level(10).cellSize());
}

TEST(ExportValues, UnknownBandCountIsNamedGenericallyWithAWarning)
{
  Store s("one_band");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 4.0, 1);
  const mtrs::ExportResult res = mtrs::exportGeoTiff(
    s.layer().string(), s.out().string(), opts(insideTile(10, kRow10, kCol10)));
  EXPECT_EQ(res.band_names, (std::vector<std::string>{"band_1"}));
  EXPECT_EQ(res.vertical_datum, "UNSPECIFIED");
  EXPECT_FALSE(res.source_vertical_datum.has_value());
  EXPECT_TRUE(hasWarning(res, "match no known layer kind"));
}

// -------------------------------------------------------------------- cap ----

TEST(ExportCap, EqualToTheCapAcceptedOneLessRefused)
{
  Store s("cap");
  s.writeRegistry(R"({"datum": "mllw"})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  const mtrs::ExportBox box = insideTile(10, kRow10, kCol10);
  const mtrs::CellWindow w = mtrs::snapBoxToLevel(box, 10);
  const std::uint64_t pixels = (w.row_end - w.row_begin) * (w.col_end - w.col_begin);
  mtrs::ExportOptions o = opts(box);
  o.max_pixels = pixels - 1;
  try {
    mtrs::exportGeoTiff(s.layer().string(), s.out().string(), o);
    FAIL() << "expected a refusal";
  } catch (const mtrs::ExportRefused & e) {
    EXPECT_NE(std::string(e.what()).find("over the cap"), std::string::npos) << e.what();
  }
  expectNothingWritten(s);
  o.max_pixels = pixels;
  const mtrs::ExportResult res = mtrs::exportGeoTiff(s.layer().string(), s.out().string(), o);
  EXPECT_EQ(res.width * res.height, pixels);
}

TEST(ExportCap, SixtyFourBitOverflowSizedBoxIsRefused)
{
  Store s("overflow");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  mtrs::ExportOptions o = opts(mtrs::ExportBox{-71.0, -180.0, 71.0, 180.0}, 20);
  o.max_pixels = std::numeric_limits<std::uint64_t>::max();
  try {
    mtrs::exportGeoTiff(s.layer().string(), s.out().string(), o);
    FAIL() << "expected a refusal";
  } catch (const mtrs::ExportRefused & e) {
    EXPECT_NE(std::string(e.what()).find("more than 2^64"), std::string::npos) << e.what();
  }
  expectNothingWritten(s);
}

TEST(ExportCap, DimensionOverTheGeoTiffLimitIsRefused)
{
  Store s("dimension");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  // Level 20: a 360-degree-wide, one-cell-tall strip is ~4.5e10 px wide.
  const double c = cellSpan(20);
  mtrs::ExportOptions o = opts(mtrs::ExportBox{43.0, -180.0, 43.0 + 0.5 * c, 180.0}, 20);
  o.max_pixels = std::numeric_limits<std::uint64_t>::max();
  try {
    mtrs::exportGeoTiff(s.layer().string(), s.out().string(), o);
    FAIL() << "expected a refusal";
  } catch (const mtrs::ExportRefused & e) {
    EXPECT_NE(std::string(e.what()).find("dimension is limited"), std::string::npos) << e.what();
  }
  expectNothingWritten(s);
}

// -------------------------------------------------------------------- CLI ----

namespace
{

struct CliRun
{
  int status = -1;
  std::string out, err;
};

CliRun runCli(const Store & s, const std::string & args)
{
  const fs::path o = s.root() / "cli.stdout", e = s.root() / "cli.stderr";
  const std::string cmd = std::string("'") + EXPORT_GEOTIFF_EXE + "' " + args + " >'" +
    o.string() + "' 2>'" + e.string() + "'";
  const int raw = std::system(cmd.c_str());
  CliRun r;
  r.status = WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;
  r.out = readFile(o);
  r.err = readFile(e);
  return r;
}

std::string boxArgs(const mtrs::ExportBox & b)
{
  std::ostringstream ss;
  ss.precision(17);
  ss << "--box " << b.lat_min << " " << b.lon_min << " " << b.lat_max << " " << b.lon_max;
  return ss.str();
}

}  // namespace

TEST(ExportCli, BoxIsRequiredAndUsageErrorsExitTwo)
{
  Store s("cli_usage");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  CliRun r = runCli(s, "'" + s.layer().string() + "' -o '" + s.out().string() + "'");
  EXPECT_EQ(r.status, 2);
  EXPECT_NE(r.err.find("--box is required"), std::string::npos) << r.err;
  r = runCli(s, "'" + s.layer().string() + "' --box 43 -70 42 -69 -o '" + s.out().string() + "'");
  EXPECT_EQ(r.status, 2);
  r = runCli(s, "'" + s.layer().string() + "' --box 43 -70 44 -o x.tif");
  EXPECT_EQ(r.status, 2);
  r = runCli(s, "--help");
  EXPECT_EQ(r.status, 0);
  EXPECT_NE(r.out.find("250000000"), std::string::npos) << "help must document the default cap";
  expectNothingWritten(s);
}

TEST(ExportCli, SuccessPrintsTheSummaryAndTheDatumWarning)
{
  Store s("cli_ok");
  s.writeRegistry(R"({"datum": ""})");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  const CliRun r = runCli(
    s, "'" + s.layer().string() + "' " + boxArgs(insideTile(10, kRow10, kCol10)) + " -o '" +
    s.out().string() + "'");
  EXPECT_EQ(r.status, 0) << r.err;
  EXPECT_NE(r.err.find("WARNING: source datum conversion not recorded"),
    std::string::npos) << r.err;
  EXPECT_NE(r.out.find("level:            10 (native)"), std::string::npos) << r.out;
  EXPECT_TRUE(fs::exists(s.out()));
  EXPECT_TRUE(tmpFiles(s).empty());
}

TEST(ExportCli, RefusalExitsOneAndWritesNothing)
{
  Store s("cli_refused");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  const CliRun r = runCli(
    s, "'" + s.layer().string() + "' " + boxArgs(insideTile(10, kRow10, kCol10)) +
    " --level 11 -o '" + s.out().string() + "'");
  EXPECT_EQ(r.status, 1);
  EXPECT_NE(r.err.find("refusing to upsample"), std::string::npos) << r.err;
  expectNothingWritten(s);
}

// Malformed numbers are usage errors, never silently reinterpreted: strtoull
// alone would read " -1" as UINT64_MAX and switch the cap off.
TEST(ExportCli, MalformedNumbersAreUsageErrors)
{
  Store s("cli_numbers");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  const std::string prefix =
    "'" + s.layer().string() + "' " + boxArgs(insideTile(10, kRow10, kCol10)) + " -o '" +
    s.out().string() + "' ";
  for (const char * bad : {"' -1'", "-1", "+5", "' 5'", "'5 '", "12abc", "0x10", "''",
      "1e9", "0"})
  {
    const CliRun r = runCli(s, prefix + "--max-pixels " + bad);
    EXPECT_EQ(r.status, 2) << "--max-pixels " << bad << ": " << r.err;
    EXPECT_NE(r.err.find("--max-pixels needs a positive integer"), std::string::npos) << r.err;
  }
  for (const char * bad : {"' 3'", "+3", "-3", "3x", "21"}) {
    const CliRun r = runCli(s, prefix + "--level " + bad);
    EXPECT_EQ(r.status, 2) << "--level " << bad << ": " << r.err;
  }
  const CliRun r = runCli(
    s, "'" + s.layer().string() + "' --box ' 43.1' -70.6 43.2 -70.5 -o '" + s.out().string() +
    "'");
  EXPECT_EQ(r.status, 2) << r.err;
  EXPECT_NE(r.err.find("--box: not a number"), std::string::npos) << r.err;
  expectNothingWritten(s);
}

TEST(ExportCli, MaxPixelsOptionIsHonoured)
{
  Store s("cli_cap");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  const CliRun r = runCli(
    s, "'" + s.layer().string() + "' " + boxArgs(insideTile(10, kRow10, kCol10)) +
    " --max-pixels 1000 -o '" + s.out().string() + "'");
  EXPECT_EQ(r.status, 1);
  EXPECT_NE(r.err.find("over the cap of 1000"), std::string::npos) << r.err;
  expectNothingWritten(s);
}

// Ctrl-C (or a SIGTERM) is the normal way to stop a long export: the CLI's
// handler removes the in-flight temporary file and the process dies of the
// signal, leaving an existing output untouched.
TEST(ExportCli, InterruptRemovesTheTemporaryFile)
{
  for (const int sig : {SIGINT, SIGTERM}) {
    SCOPED_TRACE(sig);
    Store s("cli_interrupt");
    writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
    std::ofstream(s.out()) << "previous";
    // A tall box over mostly empty tile positions: about 48 M pixels, so the
    // write takes long enough to be interrupted while the temporary file exists.
    const double r0 = static_cast<double>(kRow10) * kEdge;
    const double c0 = static_cast<double>(kCol10) * kEdge;
    const mtrs::ExportBox box = cellBox(10, r0 + 10, c0 + 10, r0 + 16010, c0 + 3010);
    const auto num = [](double v) {
        std::ostringstream ss;
        ss.precision(17);
        ss << v;
        return ss.str();
      };
    std::vector<std::string> args = {EXPORT_GEOTIFF_EXE, s.layer().string(), "--box",
      num(box.lat_min), num(box.lon_min), num(box.lat_max), num(box.lon_max), "-o",
      s.out().string()};
    std::vector<char *> argv;
    for (std::string & a : args) {
      argv.push_back(a.data());
    }
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    const int spawned = posix_spawn(&pid, EXPORT_GEOTIFF_EXE, &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    ASSERT_EQ(spawned, 0);
    bool seen = false, exited = false;
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!seen && !exited && std::chrono::steady_clock::now() < deadline) {
      seen = !tmpFiles(s).empty();
      if (!seen) {
        exited = waitpid(pid, &status, WNOHANG) == pid;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
    if (!seen) {
      if (!exited) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
      }
      FAIL() << "the export " << (exited ? "finished" : "timed out") <<
        " before its temporary file was seen";
    }
    ASSERT_EQ(kill(pid, sig), 0);
    ASSERT_EQ(waitpid(pid, &status, 0), pid);
    EXPECT_TRUE(WIFSIGNALED(status)) << "status " << status;
    if (WIFSIGNALED(status)) {
      EXPECT_EQ(WTERMSIG(status), sig);
    }
    EXPECT_TRUE(tmpFiles(s).empty()) << "an interrupted export left its temporary file";
    EXPECT_EQ(readFile(s.out()), "previous");
  }
}

// A signal that reaches the CLI once the output has been renamed into place
// must not claim "nothing written". The signal is sent as soon as the output's
// inode changes; the run either dies of it (and must say the output WAS
// replaced) or has already exited 0. A few attempts, so at least one is
// expected to die of the signal (the library holds it back until the
// replacement is counted, and the CLI still prints its summary afterwards).
TEST(ExportCli, InterruptAfterTheRenameSaysTheOutputWasReplaced)
{
  Store s("cli_interrupt_after_rename");
  writeConst(s.layer(), gridAt(10, kRow10, kCol10), 1.0);
  const double r0 = static_cast<double>(kRow10) * kEdge;
  const double c0 = static_cast<double>(kCol10) * kEdge;
  const mtrs::ExportBox box = cellBox(10, r0 + 10, c0 + 10, r0 + 4010, c0 + 3010);
  const auto num = [](double v) {
      std::ostringstream ss;
      ss.precision(17);
      ss << v;
      return ss.str();
    };
  const fs::path err_file = s.root() / "cli_after_rename.stderr";
  int signalled = 0;
  for (int attempt = 0; attempt < 5 && signalled == 0; ++attempt) {
    SCOPED_TRACE(attempt);
    std::ofstream(s.out()) << "previous";
    struct stat before_st;
    ASSERT_EQ(::stat(s.out().c_str(), &before_st), 0);
    std::vector<std::string> args = {EXPORT_GEOTIFF_EXE, s.layer().string(), "--box",
      num(box.lat_min), num(box.lon_min), num(box.lat_max), num(box.lon_max), "-o",
      s.out().string()};
    std::vector<char *> argv;
    for (std::string & a : args) {
      argv.push_back(a.data());
    }
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(
      &fa, STDERR_FILENO, err_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    pid_t pid = 0;
    const int spawned = posix_spawn(&pid, EXPORT_GEOTIFF_EXE, &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    ASSERT_EQ(spawned, 0);
    int status = 0;
    bool exited = false, renamed = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!renamed && !exited && std::chrono::steady_clock::now() < deadline) {
      struct stat st;
      renamed = ::stat(s.out().c_str(), &st) == 0 && st.st_ino != before_st.st_ino;
      if (!renamed) {
        exited = waitpid(pid, &status, WNOHANG) == pid;
      }
    }
    if (renamed) {
      kill(pid, SIGTERM);   // may find a zombie or nothing: both are fine
      ASSERT_EQ(waitpid(pid, &status, 0), pid);
    } else if (!exited) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      FAIL() << "timed out before the rename";
    }
    const std::string err = readFile(err_file);
    EXPECT_EQ(err.find("nothing written"), std::string::npos) << err;
    if (WIFSIGNALED(status)) {
      EXPECT_EQ(WTERMSIG(status), SIGTERM);
      EXPECT_NE(err.find("the output WAS replaced"), std::string::npos) << err;
      ++signalled;
    } else {
      EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0) << "status " << status;
    }
    EXPECT_EQ(readRaster(s.out()).bands, 2);
    EXPECT_TRUE(tmpFiles(s).empty());
  }
  EXPECT_GT(signalled, 0) << "no attempt died of the signal after the rename";
}
