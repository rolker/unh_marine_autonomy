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

#include "s102/convert.hpp"

#include <gdal_priv.h>
#include <gdal_utils.h>
#include <gdalwarper.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace marine_bathymetry_store::s102
{

namespace
{

constexpr double kS102NoData = 1.0e6;

struct GDALDatasetCloser
{
  void operator()(GDALDataset * ds) const
  {
    if (ds != nullptr) {
      GDALClose(ds);
    }
  }
};
using DatasetPtr = std::unique_ptr<GDALDataset, GDALDatasetCloser>;

/// Locate depth/uncertainty bands by Description; fall back to 1/2.
void locateBands(GDALDataset & ds, int & depth_band, int & uncertainty_band)
{
  depth_band = 0;
  uncertainty_band = 0;
  for (int b = 1; b <= ds.GetRasterCount(); ++b) {
    const char * desc = ds.GetRasterBand(b)->GetDescription();
    const std::string name = (desc != nullptr) ? desc : "";
    if (name == "depth") {
      depth_band = b;
    } else if (name == "uncertainty") {
      uncertainty_band = b;
    }
  }
  if (depth_band == 0 || uncertainty_band == 0) {
    if (ds.GetRasterCount() < 2) {
      throw std::runtime_error("expected 2 bands (depth, uncertainty)");
    }
    std::cerr << "s102: bands lack depth/uncertainty descriptions; "
      "assuming band 1 = depth, band 2 = uncertainty\n";
    depth_band = 1;
    uncertainty_band = 2;
  }
}

}  // namespace

void convertTile(
  const std::string & src_path,
  const std::string & out_path,
  const VerticalDatumProvider & datum)
{
  GDALAllRegister();

  // Pin the S102 driver's interpretation so a GDAL upgrade can't silently
  // flip sign (elevation vs depth) or row order. Unknown options only warn
  // for other drivers, so the synthetic GeoTIFF test fixtures pass through.
  const char * open_options[] = {
    "DEPTH_OR_ELEVATION=DEPTH", "NORTH_UP=YES", nullptr};
  DatasetPtr src(static_cast<GDALDataset *>(GDALOpenEx(
      src_path.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
      nullptr, open_options, nullptr)));
  if (src == nullptr) {
    throw std::runtime_error("cannot open " + src_path);
  }

  // Hard gate: only MLLW tiles may be shifted with an MLLW provider. A Great
  // Lakes tile (LWD) silently shifted as MLLW would be wrong by the full
  // datum separation — refuse anything not explicitly MLLW.
  const char * datum_abbrev = src->GetMetadataItem("VERTICAL_DATUM_ABBREV");
  if (datum_abbrev == nullptr || std::string(datum_abbrev) != "MLLW") {
    throw std::runtime_error(
            src_path + ": VERTICAL_DATUM_ABBREV is '" +
            (datum_abbrev ? datum_abbrev : "<absent>") +
            "', need MLLW — refusing to apply an MLLW datum shift");
  }

  int depth_band = 0;
  int uncertainty_band = 0;
  locateBands(*src, depth_band, uncertainty_band);

  // Make the source nodata explicit for the warper, PER BAND — depth and
  // uncertainty are not guaranteed to share a nodata value (they do for
  // S-102's 1e6, but don't assume it). `-srcnodata` takes a space-separated
  // value per source band in band order; read each band's own (S102 declares
  // 1e6; synthetic fixtures may rely on the fallback).
  std::string src_nodata_str;
  for (int b = 1; b <= src->GetRasterCount(); ++b) {
    int has_nodata = 0;
    const double declared = src->GetRasterBand(b)->GetNoDataValue(&has_nodata);
    const double nodata = has_nodata != 0 ? declared : kS102NoData;
    if (has_nodata == 0) {
      std::cerr << "s102: band " << b << " declares no nodata; assuming " <<
        kS102NoData << "\n";
    }
    if (!src_nodata_str.empty()) {
      src_nodata_str += " ";
    }
    src_nodata_str += std::to_string(nodata);
  }

  // Warp to geographic WGS84. Nearest resampling: value-preserving, keeps
  // each depth/σ pair coherent, and (being non-blending) cannot smear nodata
  // — declared srcnodata still propagates to NaN in the Float64 output.
  const char * warp_argv[] = {
    "-of", "MEM",
    "-t_srs", "EPSG:4326",
    "-r", "near",
    "-ot", "Float64",
    "-srcnodata", src_nodata_str.c_str(),
    "-dstnodata", "nan",
    nullptr};
  GDALWarpAppOptions * warp_options =
    GDALWarpAppOptionsNew(const_cast<char **>(warp_argv), nullptr);
  GDALDatasetH src_handle = GDALDataset::ToHandle(src.get());
  int usage_error = FALSE;
  DatasetPtr warped(GDALDataset::FromHandle(GDALWarp(
      "", nullptr, 1, &src_handle, warp_options, &usage_error)));
  GDALWarpAppOptionsFree(warp_options);
  if (warped == nullptr || usage_error != FALSE) {
    throw std::runtime_error("warp to geographic WGS84 failed for " + src_path);
  }

  const int width = warped->GetRasterXSize();
  const int height = warped->GetRasterYSize();
  double gt[6] = {0., 1., 0., 0., 0., 1.};
  if (warped->GetGeoTransform(gt) != CE_None) {
    throw std::runtime_error("warped dataset has no geotransform");
  }

  std::vector<double> depth(static_cast<size_t>(width) * height);
  std::vector<double> uncertainty(depth.size());
  if (warped->GetRasterBand(depth_band)->RasterIO(
      GF_Read, 0, 0, width, height, depth.data(), width, height,
      GDT_Float64, 0, 0) != CE_None ||
    warped->GetRasterBand(uncertainty_band)->RasterIO(
      GF_Read, 0, 0, width, height, uncertainty.data(), width, height,
      GDT_Float64, 0, 0) != CE_None)
  {
    throw std::runtime_error("raster read failed for " + src_path);
  }

  // Per-cell datum shift at cell centers: height = mllw_z − depth (S-102
  // depth is positive down from MLLW; store wants up-positive ellipsoidal).
  // nullopt from the provider ⇒ nodata — never silently unshifted.
  const double nan = std::numeric_limits<double>::quiet_NaN();
  for (int row = 0; row < height; ++row) {
    for (int col = 0; col < width; ++col) {
      const size_t i = static_cast<size_t>(row) * width + col;
      if (!std::isfinite(depth[i])) {
        depth[i] = nan;
        uncertainty[i] = nan;
        continue;
      }
      const double lon = gt[0] + (col + 0.5) * gt[1] + (row + 0.5) * gt[2];
      const double lat = gt[3] + (col + 0.5) * gt[4] + (row + 0.5) * gt[5];
      const auto mllw_z = datum.mllwHeight(lat, lon);
      if (!mllw_z.has_value()) {
        depth[i] = nan;
        uncertainty[i] = nan;
        continue;
      }
      depth[i] = *mllw_z - depth[i];
      if (!std::isfinite(uncertainty[i])) {
        uncertainty[i] = nan;
      }
    }
  }

  GDALDriver * gtiff = GetGDALDriverManager()->GetDriverByName("GTiff");
  if (gtiff == nullptr) {
    throw std::runtime_error("GTiff driver unavailable");
  }
  const char * create_options[] = {"COMPRESS=DEFLATE", nullptr};
  DatasetPtr out(gtiff->Create(
      out_path.c_str(), width, height, 2, GDT_Float64,
      const_cast<char **>(create_options)));
  if (out == nullptr) {
    throw std::runtime_error("cannot create " + out_path);
  }
  out->SetGeoTransform(gt);
  out->SetProjection(warped->GetProjectionRef());
  out->GetRasterBand(1)->SetDescription("height");
  out->GetRasterBand(1)->SetNoDataValue(nan);
  out->GetRasterBand(2)->SetDescription("uncertainty");
  out->GetRasterBand(2)->SetNoDataValue(nan);
  if (out->GetRasterBand(1)->RasterIO(
      GF_Write, 0, 0, width, height, depth.data(), width, height,
      GDT_Float64, 0, 0) != CE_None ||
    out->GetRasterBand(2)->RasterIO(
      GF_Write, 0, 0, width, height, uncertainty.data(), width, height,
      GDT_Float64, 0, 0) != CE_None)
  {
    throw std::runtime_error("raster write failed for " + out_path);
  }
}

}  // namespace marine_bathymetry_store::s102
