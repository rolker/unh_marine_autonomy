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

#include "marine_bathymetry_store/geotiff_import.hpp"

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace marine_bathymetry_store
{

namespace
{

/// RAII wrapper so a thrown exception still closes the GDAL dataset.
struct DatasetCloser
{
  GDALDataset * ds = nullptr;
  ~DatasetCloser() {if (ds) {GDALClose(ds);}}
};

}  // namespace

std::size_t importGeoTiff(
  BathymetryStore & store, SourceLayer layer,
  const std::string & path, const GeoTiffImportOptions & options)
{
  if (options.depth_band < 1) {
    throw std::invalid_argument("importGeoTiff: depth_band must be >= 1");
  }
  if (options.uncertainty_band < 0) {
    throw std::invalid_argument("importGeoTiff: uncertainty_band must be >= 0 (0 = none)");
  }

  // Target import level: a caller-specified level (multi-level, ADR-0002 §D2) or
  // the store's default level.
  const gggs::Level level =
    options.level.has_value() ? gggs::Level(*options.level) : store.level();

  GDALAllRegister();
  DatasetCloser in;
  in.ds = GDALDataset::FromHandle(GDALOpen(path.c_str(), GA_ReadOnly));
  if (in.ds == nullptr) {
    throw std::runtime_error("importGeoTiff: could not open " + path);
  }
  const int width = in.ds->GetRasterXSize();
  const int height = in.ds->GetRasterYSize();
  const int band_count = in.ds->GetRasterCount();
  if (options.depth_band > band_count || options.uncertainty_band > band_count) {
    throw std::runtime_error("importGeoTiff: band index beyond raster bands in " + path);
  }

  // The pixel-center mapping below interprets the geotransform as degrees, so
  // the file must be a geographic WGS84 raster (same guard as tile_io's load).
  const OGRSpatialReference * srs = in.ds->GetSpatialRef();
  OGRErr axis_error = OGRERR_NONE;
  if (srs == nullptr || !srs->IsGeographic() ||
    std::abs(srs->GetSemiMajor(&axis_error) - 6378137.0) > 1.0 || axis_error != OGRERR_NONE)
  {
    throw std::runtime_error("importGeoTiff: not a geographic WGS84 raster: " + path);
  }

  double gt[6];
  if (in.ds->GetGeoTransform(gt) != CE_None) {
    throw std::runtime_error("importGeoTiff: missing geotransform in " + path);
  }
  if (gt[2] != 0.0 || gt[4] != 0.0) {
    throw std::runtime_error("importGeoTiff: rotated geotransform unsupported in " + path);
  }

  // Honor a declared no-data value: it may be finite (e.g. -9999), which the
  // isfinite() skip below would happily import as a 9 km depth.
  int has_nodata = 0;
  const double nodata =
    in.ds->GetRasterBand(options.depth_band)->GetNoDataValue(&has_nodata);
  // Honor the uncertainty band's no-data sentinel too: a positive finite
  // sentinel (e.g. 9999) would otherwise pass the isfinite/>0 gate below and
  // import as a real, huge-but-finite uncertainty rather than being treated as
  // missing -- the same trap the depth band is guarded against.
  int unc_has_nodata = 0;
  double unc_nodata = 0.0;
  if (options.uncertainty_band > 0) {
    unc_nodata =
      in.ds->GetRasterBand(options.uncertainty_band)->GetNoDataValue(&unc_has_nodata);
  }

  std::map<gggs::GridIndex, BathymetryTile> tiles;
  std::size_t imported = 0;

  std::vector<double> depth_row(width);
  std::vector<double> uncertainty_row(width);
  for (int y = 0; y < height; ++y) {
    if (in.ds->GetRasterBand(options.depth_band)->RasterIO(
        GF_Read, 0, y, width, 1, depth_row.data(), width, 1, GDT_Float64, 0, 0) != CE_None)
    {
      throw std::runtime_error("importGeoTiff: depth read failed in " + path);
    }
    if (options.uncertainty_band > 0) {
      if (in.ds->GetRasterBand(options.uncertainty_band)->RasterIO(
          GF_Read, 0, y, width, 1, uncertainty_row.data(), width, 1, GDT_Float64, 0, 0) !=
        CE_None)
      {
        throw std::runtime_error("importGeoTiff: uncertainty read failed in " + path);
      }
    }
    const double latitude = gt[3] + (y + 0.5) * gt[5];
    for (int x = 0; x < width; ++x) {
      if (!std::isfinite(depth_row[x]) || (has_nodata && depth_row[x] == nodata)) {
        continue;   // no-data pixel
      }
      // Vertical conversion at import (§D4): pixel value -> ellipsoidal height.
      const double depth = options.depth_scale * depth_row[x] + options.depth_offset;
      // A non-finite OR non-positive uncertainty is *missing*, not perfect:
      // zero would pass every reliability gate and carry infinite weight in
      // 1/sigma^2 fusion. Such cells get default_uncertainty (NaN by default
      // = never reliable — conservative).
      double uncertainty = options.default_uncertainty;
      if (options.uncertainty_band > 0 && std::isfinite(uncertainty_row[x]) &&
        uncertainty_row[x] > 0.0 &&
        !(unc_has_nodata && uncertainty_row[x] == unc_nodata))
      {
        uncertainty = uncertainty_row[x];
      }
      const double longitude = gt[0] + (x + 0.5) * gt[1];
      // Fill the pixel's full FOOTPRINT of store cells, not just the cell
      // under its centre — a coarser-than-store source (e.g. a 5 m contour
      // prior into a 0.5 m store) must produce coverage, not isolated dots.
      // The box is shrunk by a hair so adjacent pixels never contend for the
      // cells on their shared boundary; for aligned or finer inputs it
      // therefore covers exactly the one containing cell.
      const double half_lon = 0.495 * std::abs(gt[1]);
      const double half_lat = 0.495 * std::abs(gt[5]);
      const auto box_min = gggs::geoPoint(latitude - half_lat, longitude - half_lon);
      const auto box_max = gggs::geoPoint(latitude + half_lat, longitude + half_lon);
      gggs::GridAreaIterator grid_it(
        level.gridIndex(box_min.latitude, box_min.longitude),
        level.gridIndex(box_max.latitude, box_max.longitude));
      for (; grid_it.valid(); grid_it.next()) {
        auto tile_it = tiles.find(*grid_it);
        for (gggs::CellAreaIterator cell_it(*grid_it, box_min, box_max);
          cell_it.valid(); cell_it.next())
        {
          const gggs::CellIndex & cell = *cell_it;
          if (!cell.valid()) {
            continue;   // outside GGGS's usable envelope
          }
          if (tile_it == tiles.end()) {
            tile_it = tiles.emplace(cell.grid(), BathymetryTile(cell.grid())).first;
          }
          // Several input pixels can land in one cell when the input is finer
          // than the store level: keep the lowest-uncertainty value (a finite
          // uncertainty always beats NaN; ties keep the first read).
          const BathyCell existing = tile_it->second.get(cell.row(), cell.column());
          if (existing.hasData()) {
            const bool existing_unc_finite = std::isfinite(existing.uncertainty);
            const bool new_unc_finite = std::isfinite(uncertainty);
            if (!new_unc_finite && existing_unc_finite) {
              continue;
            }
            if (new_unc_finite && existing_unc_finite &&
              uncertainty >= existing.uncertainty)
            {
              continue;
            }
            if (!new_unc_finite && !existing_unc_finite) {
              continue;   // tie among NaNs: keep the first read
            }
          } else {
            ++imported;   // a new cell (replacements don't recount)
          }
          tile_it->second.set(cell.row(), cell.column(), BathyCell{depth, uncertainty});
        }
      }
    }
  }

  // Bulk-insert into the layer's single fused surface (#221). The PreExisting
  // read-only gate is enforced by importTiles (throws logic_error if the store
  // is not pre_existing_writable).
  store.importTiles(layer, std::move(tiles));
  return imported;
}

}  // namespace marine_bathymetry_store
