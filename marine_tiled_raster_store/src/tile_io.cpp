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

#include "marine_tiled_raster_store/tile_io.hpp"

#include <gdal_priv.h>
#include <cpl_string.h>
#include <ogr_spatialref.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace marine_tiled_raster_store
{

namespace
{

constexpr int kEdge = static_cast<int>(gggs::cell_rows_per_grid);   // 960
constexpr int kCellCount = kEdge * kEdge;

/// RAII wrapper so a thrown exception still closes the GDAL dataset.
struct DatasetCloser
{
  GDALDataset * ds = nullptr;
  ~DatasetCloser() {if (ds) {GDALClose(ds);}}
};

/// Map a C++ element type to its GDAL band data type. Adding a type here (plus
/// an explicit instantiation at the bottom of this file) is all it takes to
/// extend the supported element types — and keeps GDAL out of the public header.
template<typename T>
GDALDataType gdalType();
template<>
GDALDataType gdalType<double>() {return GDT_Float64;}
template<>
GDALDataType gdalType<float>() {return GDT_Float32;}
template<>
GDALDataType gdalType<std::uint16_t>() {return GDT_UInt16;}
// GDT_Int64 requires GDAL >= 3.5; the workspace targets GDAL >= 3.8 (ROS 2
// jazzy / rolling, 3.8.4 locally). Used by the bathy timestamp tile (#178):
// int64 nanoseconds-since-epoch is ROS-native and exact, unlike Float64 seconds.
template<>
GDALDataType gdalType<std::int64_t>() {return GDT_Int64;}

/// Copy a band between GGGS cell order (row 0 = south) and north-up raster order
/// (row 0 = north) — the two differ only by a vertical row flip. Works in both
/// directions since the flip is its own inverse mapping.
template<typename T>
void flipRows(const std::vector<T> & src, std::vector<T> & dst)
{
  for (int r = 0; r < kEdge; ++r) {
    const std::size_t flipped = static_cast<std::size_t>(kEdge - 1 - r);
    const std::size_t dst_row = static_cast<std::size_t>(r);
    std::copy_n(
      src.begin() + flipped * kEdge, kEdge,
      dst.begin() + dst_row * kEdge);
  }
}

void checkCE(CPLErr err, const char * what)
{
  if (err != CE_None) {
    throw std::runtime_error(std::string("GDAL error: ") + what);
  }
}

}  // namespace

std::string tileFilename(const gggs::GridIndex & grid)
{
  return std::to_string(static_cast<int>(grid.level())) + "_" +
         std::to_string(grid.row()) + "_" + std::to_string(grid.column()) + ".tif";
}

int tileRasterCount(const std::string & path)
{
  GDALAllRegister();
  DatasetCloser in;
  in.ds = GDALDataset::FromHandle(GDALOpen(path.c_str(), GA_ReadOnly));
  if (in.ds == nullptr) {
    throw std::runtime_error("tileRasterCount: could not open " + path);
  }
  return in.ds->GetRasterCount();
}

template<typename T>
void saveTile(
  const TiledRasterTile<T> & tile, const std::string & path,
  const std::vector<std::optional<T>> & band_nodata)
{
  const std::size_t band_count = tile.bandCount();
  if (band_nodata.size() != band_count) {
    throw std::invalid_argument("saveTile: band_nodata size must equal tile.bandCount()");
  }

  GDALAllRegister();
  GDALDriver * driver = GetGDALDriverManager()->GetDriverByName("GTiff");
  if (driver == nullptr) {
    throw std::runtime_error("saveTile: GTiff driver unavailable");
  }

  char ** options = nullptr;
  options = CSLSetNameValue(options, "COMPRESS", "LZW");
  DatasetCloser out;
  out.ds = driver->Create(
    path.c_str(), kEdge, kEdge, static_cast<int>(band_count), gdalType<T>(), options);
  CSLDestroy(options);
  if (out.ds == nullptr) {
    throw std::runtime_error("saveTile: could not create " + path);
  }

  // North-up geotransform from the grid's corners. It is load-bearing: loadTile
  // recovers the GridIndex from it.
  const gggs::GridIndex & grid = tile.index();
  const double west = grid.westLongitude();
  const double north = grid.northLatitude();
  const double pixel_x = grid.longitudinalSpan() / kEdge;
  const double pixel_y = -grid.latitudinalSpan() / kEdge;   // negative: north-up
  double geo_transform[6] = {west, pixel_x, 0.0, north, 0.0, pixel_y};
  checkCE(out.ds->SetGeoTransform(geo_transform), "set geotransform");

  OGRSpatialReference srs;
  srs.SetWellKnownGeogCS("WGS84");
  char * wkt = nullptr;
  if (srs.exportToWkt(&wkt) != OGRERR_NONE || wkt == nullptr) {
    CPLFree(wkt);
    throw std::runtime_error("saveTile: could not build WGS84 WKT for " + path);
  }
  const CPLErr projection_result = out.ds->SetProjection(wkt);
  CPLFree(wkt);
  checkCE(projection_result, "set projection");

  // Flip each band into north-up raster order, then write. A no-data value is
  // metadata only — the sentinel round-trips through the band regardless — so
  // SetNoDataValue's result is intentionally not treated as fatal.
  std::vector<T> raster(kCellCount);
  for (std::size_t b = 0; b < band_count; ++b) {
    const int band_index = static_cast<int>(b) + 1;   // GDAL bands are 1-indexed
    if (band_nodata[b].has_value()) {
      out.ds->GetRasterBand(band_index)->SetNoDataValue(
        static_cast<double>(*band_nodata[b]));
    }
    flipRows<T>(tile.band(b), raster);
    checkCE(
      out.ds->GetRasterBand(band_index)->RasterIO(
        GF_Write, 0, 0, kEdge, kEdge, raster.data(), kEdge, kEdge, gdalType<T>(), 0, 0),
      "write band");
  }

  // Close explicitly and check the result: for a GTiff the (LZW-compressed)
  // raster is flushed on close, so a disk-full / I/O failure surfaces only here,
  // and saveTiles clears the dirty flag once this returns. (DatasetCloser stays
  // as the exception-safety net for the error paths above; nulling out.ds here
  // prevents a double close.) GDALClose returns CPLErr since GDAL 3.7; the
  // workspace targets GDAL >= 3.8 (ROS 2 jazzy / rolling).
  GDALDataset * ds = out.ds;
  out.ds = nullptr;
  if (GDALClose(ds) != CE_None) {
    throw std::runtime_error("saveTile: failed to flush/close " + path);
  }
}

template<typename T>
TiledRasterTile<T> loadTile(
  const std::string & path, const gggs::Level & level, std::size_t band_count)
{
  GDALAllRegister();
  DatasetCloser in;
  in.ds = GDALDataset::FromHandle(GDALOpen(path.c_str(), GA_ReadOnly));
  if (in.ds == nullptr) {
    throw std::runtime_error("loadTile: could not open " + path);
  }
  if (in.ds->GetRasterXSize() != kEdge || in.ds->GetRasterYSize() != kEdge ||
    in.ds->GetRasterCount() < static_cast<int>(band_count))
  {
    throw std::runtime_error("loadTile: unexpected dimensions/bands in " + path);
  }

  // The grid recovery below interprets the geotransform as degrees, so the file
  // must be a geographic WGS84 raster. Reject a projected or otherwise non-WGS84
  // file rather than silently misreading its coordinates as lat/lon.
  const OGRSpatialReference * srs = in.ds->GetSpatialRef();
  OGRErr axis_error = OGRERR_NONE;
  if (srs == nullptr || !srs->IsGeographic() ||
    std::abs(srs->GetSemiMajor(&axis_error) - 6378137.0) > 1.0 || axis_error != OGRERR_NONE)
  {
    throw std::runtime_error("loadTile: not a geographic WGS84 raster: " + path);
  }

  double geo_transform[6];
  if (in.ds->GetGeoTransform(geo_transform) != CE_None) {
    throw std::runtime_error("loadTile: missing geotransform in " + path);
  }
  const double west = geo_transform[0];
  const double pixel_x = geo_transform[1];
  const double north = geo_transform[3];
  const double pixel_y = geo_transform[5];
  const double east = west + pixel_x * kEdge;
  const double south = north + pixel_y * kEdge;

  // Recover the GridIndex: the cell center maps back through the level.
  const double center_lat = 0.5 * (north + south);
  const double center_lon = 0.5 * (west + east);
  const gggs::GridIndex grid = level.gridIndex(center_lat, center_lon);

  // Reject a file whose grid edges don't match a grid at this level (e.g. saved
  // at a different level): require agreement to within half a cell.
  const double tol_lon = 0.5 * std::abs(pixel_x);
  const double tol_lat = 0.5 * std::abs(pixel_y);
  if (std::abs(grid.westLongitude() - west) > tol_lon ||
    std::abs(grid.northLatitude() - north) > tol_lat)
  {
    throw std::runtime_error(
            "loadTile: geotransform does not match any grid at level " +
            std::to_string(level.level()) + " in " + path);
  }

  // Default-fill the bands; every cell is overwritten from the raster below, so
  // the fill value is immaterial. Filled via band accessors (not set()), so the
  // tile stays clean.
  TiledRasterTile<T> tile(grid, band_count, T{});
  std::vector<T> raster(kCellCount);
  for (std::size_t b = 0; b < band_count; ++b) {
    const int band_index = static_cast<int>(b) + 1;
    checkCE(
      in.ds->GetRasterBand(band_index)->RasterIO(
        GF_Read, 0, 0, kEdge, kEdge, raster.data(), kEdge, kEdge, gdalType<T>(), 0, 0),
      "read band");
    flipRows<T>(raster, tile.band(b));
  }
  return tile;
}

template<typename T>
std::size_t saveTiles(
  std::map<gggs::GridIndex, TiledRasterTile<T>> & tiles, const std::string & dir,
  const std::vector<std::optional<T>> & band_nodata)
{
  namespace fs = std::filesystem;
  std::size_t written = 0;
  bool created_dir = false;
  for (auto & [grid, tile] : tiles) {
    if (!tile.dirty()) {
      continue;
    }
    if (!created_dir) {
      fs::create_directories(dir);
      created_dir = true;
    }
    saveTile<T>(tile, (fs::path(dir) / tileFilename(grid)).string(), band_nodata);
    tile.clearDirty();
    ++written;
  }
  return written;
}

template<typename T>
std::size_t loadTiles(
  std::map<gggs::GridIndex, TiledRasterTile<T>> & tiles, const std::string & dir,
  const gggs::Level & level, std::size_t band_count)
{
  namespace fs = std::filesystem;
  std::size_t loaded = 0;
  if (!fs::is_directory(dir)) {
    return 0;
  }
  for (const auto & entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".tif") {
      continue;
    }
    TiledRasterTile<T> tile = loadTile<T>(entry.path().string(), level, band_count);
    const gggs::GridIndex index = tile.index();
    auto it = tiles.find(index);
    if (it == tiles.end()) {
      tiles.emplace(index, std::move(tile));
    } else {
      it->second = std::move(tile);
    }
    ++loaded;
  }
  return loaded;
}

// Explicit instantiations — the element types tile_io supports. This is what
// keeps GDAL a PRIVATE dependency: the templates are defined here, and consumers
// (e.g. marine_bathymetry_store with double) link these symbols without ever
// including a GDAL header. Add a type here + a gdalType<> specialization above to
// extend (e.g. std::uint16_t for the sidescan mosaic, #173).
template void saveTile<double>(
  const TiledRasterTile<double> &, const std::string &,
  const std::vector<std::optional<double>> &);
template void saveTile<float>(
  const TiledRasterTile<float> &, const std::string &,
  const std::vector<std::optional<float>> &);
template void saveTile<std::uint16_t>(
  const TiledRasterTile<std::uint16_t> &, const std::string &,
  const std::vector<std::optional<std::uint16_t>> &);
template void saveTile<std::int64_t>(
  const TiledRasterTile<std::int64_t> &, const std::string &,
  const std::vector<std::optional<std::int64_t>> &);

template TiledRasterTile<double> loadTile<double>(
  const std::string &, const gggs::Level &, std::size_t);
template TiledRasterTile<float> loadTile<float>(
  const std::string &, const gggs::Level &, std::size_t);
template TiledRasterTile<std::uint16_t> loadTile<std::uint16_t>(
  const std::string &, const gggs::Level &, std::size_t);
template TiledRasterTile<std::int64_t> loadTile<std::int64_t>(
  const std::string &, const gggs::Level &, std::size_t);

template std::size_t saveTiles<double>(
  std::map<gggs::GridIndex, TiledRasterTile<double>> &, const std::string &,
  const std::vector<std::optional<double>> &);
template std::size_t saveTiles<float>(
  std::map<gggs::GridIndex, TiledRasterTile<float>> &, const std::string &,
  const std::vector<std::optional<float>> &);
template std::size_t saveTiles<std::uint16_t>(
  std::map<gggs::GridIndex, TiledRasterTile<std::uint16_t>> &, const std::string &,
  const std::vector<std::optional<std::uint16_t>> &);
template std::size_t saveTiles<std::int64_t>(
  std::map<gggs::GridIndex, TiledRasterTile<std::int64_t>> &, const std::string &,
  const std::vector<std::optional<std::int64_t>> &);

template std::size_t loadTiles<double>(
  std::map<gggs::GridIndex, TiledRasterTile<double>> &, const std::string &,
  const gggs::Level &, std::size_t);
template std::size_t loadTiles<float>(
  std::map<gggs::GridIndex, TiledRasterTile<float>> &, const std::string &,
  const gggs::Level &, std::size_t);
template std::size_t loadTiles<std::uint16_t>(
  std::map<gggs::GridIndex, TiledRasterTile<std::uint16_t>> &, const std::string &,
  const gggs::Level &, std::size_t);
template std::size_t loadTiles<std::int64_t>(
  std::map<gggs::GridIndex, TiledRasterTile<std::int64_t>> &, const std::string &,
  const gggs::Level &, std::size_t);

}  // namespace marine_tiled_raster_store
