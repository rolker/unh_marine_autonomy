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

#include "marine_bathymetry_store/tile_io.hpp"

#include <gdal_priv.h>
#include <cpl_string.h>
#include <ogr_spatialref.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace marine_bathymetry_store
{

namespace
{

constexpr int kEdge = BathymetryTile::edge;       // 960
constexpr int kCellCount = kEdge * kEdge;
constexpr int kBandCount = 3;                     // depth, uncertainty, timestamp
constexpr int kDepthBand = 1;                     // GDAL bands are 1-indexed
constexpr int kUncertaintyBand = 2;
constexpr int kTimestampBand = 3;

/// RAII wrapper so a thrown exception still closes the GDAL dataset.
struct DatasetCloser
{
  GDALDataset * ds = nullptr;
  ~DatasetCloser() {if (ds) {GDALClose(ds);}}
};

/// Copy a band between GGGS cell order (row 0 = south) and north-up raster
/// order (row 0 = north) — the two differ only by a vertical row flip.
/// Works in both directions since the flip is its own inverse mapping.
void flipRows(const std::vector<double> & src, std::vector<double> & dst)
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

std::string layerDirName(SourceLayer layer)
{
  switch (layer) {
    case SourceLayer::Processed: return "processed";
    case SourceLayer::Draft: return "draft";
    case SourceLayer::Chart: return "chart";
  }
  throw std::runtime_error("layerDirName: unknown SourceLayer");
}

void saveTile(const BathymetryTile & tile, const std::string & path)
{
  GDALAllRegister();
  GDALDriver * driver = GetGDALDriverManager()->GetDriverByName("GTiff");
  if (driver == nullptr) {
    throw std::runtime_error("saveTile: GTiff driver unavailable");
  }

  char ** options = nullptr;
  options = CSLSetNameValue(options, "COMPRESS", "LZW");
  DatasetCloser out;
  out.ds = driver->Create(path.c_str(), kEdge, kEdge, kBandCount, GDT_Float64, options);
  CSLDestroy(options);
  if (out.ds == nullptr) {
    throw std::runtime_error("saveTile: could not create " + path);
  }

  // North-up geotransform from the grid's corners.
  const gggs::GridIndex & grid = tile.index();
  const double west = grid.westLongitude();
  const double north = grid.northLatitude();
  const double pixel_x = grid.longitudinalSpan() / kEdge;
  const double pixel_y = -grid.latitudinalSpan() / kEdge;   // negative: north-up
  double geo_transform[6] = {west, pixel_x, 0.0, north, 0.0, pixel_y};
  // The geotransform is load-bearing: loadTile recovers the GridIndex from it.
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

  // The depth/uncertainty no-data value is metadata only — NaN round-trips
  // through the Float64 bands regardless — so its result is intentionally not
  // treated as fatal. The timestamp band has no no-data (0 = unset, never NaN).
  const double nan = std::numeric_limits<double>::quiet_NaN();
  out.ds->GetRasterBand(kDepthBand)->SetNoDataValue(nan);
  out.ds->GetRasterBand(kUncertaintyBand)->SetNoDataValue(nan);

  // Flip each band into north-up raster order, then write.
  std::vector<double> raster(kCellCount);
  const std::array<std::pair<int, const std::vector<double> *>, kBandCount> bands{{
    {kDepthBand, &tile.depthBand()},
    {kUncertaintyBand, &tile.uncertaintyBand()},
    {kTimestampBand, &tile.timestampBand()},
  }};
  for (const auto & [band_index, data] : bands) {
    flipRows(*data, raster);
    checkCE(
      out.ds->GetRasterBand(band_index)->RasterIO(
        GF_Write, 0, 0, kEdge, kEdge, raster.data(), kEdge, kEdge, GDT_Float64, 0, 0),
      "write band");
  }

  // Close explicitly and check the result. For a GTiff the (LZW-compressed)
  // raster and directory are flushed to disk on close, so a disk-full / I/O
  // failure surfaces only here — and saveTile must report it, because save()
  // clears the tile's dirty flag once this returns. (The DatasetCloser stays
  // as an exception-safety net for the error paths above; releasing the
  // handle here prevents a double close.)
  // GDALClose returns CPLErr since GDAL 3.7; the workspace targets GDAL >= 3.8
  // (ROS 2 jazzy / rolling), so checking it here is well-defined.
  GDALDataset * ds = out.ds;
  out.ds = nullptr;
  if (GDALClose(ds) != CE_None) {
    throw std::runtime_error("saveTile: failed to flush/close " + path);
  }
}

BathymetryTile loadTile(const std::string & path, const gggs::Level & level)
{
  GDALAllRegister();
  DatasetCloser in;
  in.ds = GDALDataset::FromHandle(GDALOpen(path.c_str(), GA_ReadOnly));
  if (in.ds == nullptr) {
    throw std::runtime_error("loadTile: could not open " + path);
  }
  if (in.ds->GetRasterXSize() != kEdge || in.ds->GetRasterYSize() != kEdge ||
    in.ds->GetRasterCount() < kBandCount)
  {
    throw std::runtime_error("loadTile: unexpected dimensions/bands in " + path);
  }

  // The grid recovery below interprets the geotransform as degrees, so the file
  // must be a geographic WGS84 raster. Reject a projected (e.g. UTM, coordinates
  // in metres) or otherwise non-WGS84 file — accidental copy, corruption — rather
  // than silently misreading its coordinates as lat/lon.
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

  // Recover the GridIndex: the cell center maps back through the level. (We
  // cannot construct a GridIndex from raw row/col — that ctor is GGGS-private.)
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

  BathymetryTile tile(grid);
  std::vector<double> raster(kCellCount);
  const std::array<std::pair<int, std::vector<double> *>, kBandCount> bands{{
    {kDepthBand, &tile.depthBand()},
    {kUncertaintyBand, &tile.uncertaintyBand()},
    {kTimestampBand, &tile.timestampBand()},
  }};
  for (const auto & [band_index, data] : bands) {
    checkCE(
      in.ds->GetRasterBand(band_index)->RasterIO(
        GF_Read, 0, 0, kEdge, kEdge, raster.data(), kEdge, kEdge, GDT_Float64, 0, 0),
      "read band");
    flipRows(raster, *data);   // north-up raster back to GGGS cell order
  }
  // Filled via band accessors (not set()), so the tile stays clean.
  return tile;
}

namespace
{

constexpr const char * kProvenanceSidecar = "provenance";

/// Write the epoch's provenance sidecar (one token + newline).
void writeProvenance(const std::filesystem::path & epoch_dir, Provenance provenance)
{
  const std::filesystem::path path = epoch_dir / kProvenanceSidecar;
  std::ofstream out(path);
  out << provenanceToken(provenance) << '\n';
  if (!out) {
    throw std::runtime_error("save: could not write " + path.string());
  }
}

/// Read an epoch's provenance sidecar. A missing or unreadable sidecar is
/// conservatively `LiveFused` — the lower rank, so a later compaction can
/// still supersede the epoch (ADR-0002 §A1.2).
Provenance readProvenance(const std::filesystem::path & epoch_dir)
{
  std::ifstream in(epoch_dir / kProvenanceSidecar);
  std::string token;
  if (!in || !std::getline(in, token)) {
    return Provenance::LiveFused;
  }
  // Trim trailing whitespace before parsing: a sidecar written with CRLF
  // line endings (e.g. on a Windows host) leaves a '\r' on the token, which
  // would otherwise fail the exact-match parse and silently downgrade a
  // Replayed epoch to LiveFused — allowing live writes over replayed data.
  token.erase(
    std::find_if(
      token.rbegin(), token.rend(),
      [](unsigned char c) {return !std::isspace(c);}).base(),
    token.end());
  try {
    return provenanceFromToken(token);
  } catch (const std::invalid_argument &) {
    return Provenance::LiveFused;
  }
}

}  // namespace

std::size_t save(BathymetryStore & store, const std::string & dir)
{
  namespace fs = std::filesystem;
  std::size_t written = 0;
  for (const SourceLayer layer : source_layers_by_priority) {
    const fs::path layer_dir = fs::path(dir) / layerDirName(layer);
    // Iterate the layer's epochs (a const view) and write each epoch's dirty
    // tiles. Flags are cleared via getOrCreateTile()/getOrCreateEpoch() on the
    // same (existing) keys: those are lookups, not inserts, and flipping a bool
    // on the value mutates neither map, so the iterators stay valid.
    for (const auto & [epoch, epoch_tiles] : store.epochs(layer)) {
      const fs::path epoch_dir = layer_dir / epoch;
      const bool supersedes = epoch_tiles.supersedes_disk;
      if (supersedes) {
        // A wholesale import may cover fewer grids than the surface it
        // replaced; clear the directory so stale tiles can't be resurrected.
        fs::remove_all(epoch_dir);
      }
      bool wrote_any = false;
      for (const auto & [grid, tile] : epoch_tiles.tiles) {
        if (!supersedes && !tile.dirty()) {
          continue;
        }
        if (!wrote_any) {
          fs::create_directories(epoch_dir);
        }
        saveTile(tile, (epoch_dir / tileFilename(grid)).string());
        store.getOrCreateTile(layer, epoch, grid).clearDirty();
        ++written;
        wrote_any = true;
      }
      if (wrote_any || supersedes) {
        // (Re)write the sidecar — compaction changes the provenance. Ensure
        // the directory exists even for a superseding epoch with no tiles.
        fs::create_directories(epoch_dir);
        writeProvenance(epoch_dir, epoch_tiles.provenance);
        store.getOrCreateEpoch(layer, epoch, epoch_tiles.provenance)
        .supersedes_disk = false;
      }
    }
  }
  return written;
}

std::size_t load(BathymetryStore & store, const std::string & dir)
{
  namespace fs = std::filesystem;
  std::size_t loaded = 0;
  for (const SourceLayer layer : source_layers_by_priority) {
    const fs::path layer_dir = fs::path(dir) / layerDirName(layer);
    if (!fs::is_directory(layer_dir)) {
      continue;
    }
    for (const auto & epoch_entry : fs::directory_iterator(layer_dir)) {
      if (!epoch_entry.is_directory()) {
        continue;   // epochs are subdirectories; skip stray files
      }
      const Epoch epoch = epoch_entry.path().filename().string();
      validateEpochLabel(epoch);
      const Provenance provenance = readProvenance(epoch_entry.path());
      // Create the epoch even if it turns out to hold no tiles, so its
      // provenance is represented in memory.
      store.getOrCreateEpoch(layer, epoch, provenance);
      for (const auto & entry : fs::directory_iterator(epoch_entry.path())) {
        if (!entry.is_regular_file() || entry.path().extension() != ".tif") {
          continue;
        }
        BathymetryTile tile = loadTile(entry.path().string(), store.level());
        store.getOrCreateTile(layer, epoch, tile.index()) = std::move(tile);
        ++loaded;
      }
    }
  }
  return loaded;
}

}  // namespace marine_bathymetry_store
