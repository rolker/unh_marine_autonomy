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

#ifndef S102__CONVERT_HPP_
#define S102__CONVERT_HPP_

#include <optional>
#include <string>

/// @file
/// @brief S-102 convert stage (#278): projected MLLW depth tile → geographic
/// WGS84 store-convention GeoTIFF (ellipsoidal height, up-positive, NaN
/// nodata) with the per-cell MLLW→ellipsoid shift applied.
///
/// The store only accepts geographic WGS84 rasters (`geotiff_import.cpp`
/// rejects projected input) while NOAA S-102 tiles are UTM-projected, so the
/// warp is mandatory, not cosmetic. Nearest-neighbour resampling keeps each
/// depth/σ pair coherent and never invents values; nodata is declared to the
/// warper so it cannot smear.

namespace marine_bathymetry_store::s102
{

/// @brief Per-point vertical-datum seam (#274).
///
/// Returns the MLLW surface's ellipsoidal height at (lat, lon), or nullopt
/// where no answer exists. The converter turns nullopt cells into nodata —
/// a cell is never passed through silently unshifted.
class VerticalDatumProvider
{
public:
  virtual ~VerticalDatumProvider() = default;
  /// @return MLLW height relative to the WGS84 ellipsoid (m), or nullopt.
  virtual std::optional<double> mllwHeight(double lat, double lon) const = 0;
};

/// @brief Fixed-offset provider: tests and explicit scratch-store use.
class ConstantOffsetProvider : public VerticalDatumProvider
{
public:
  explicit ConstantOffsetProvider(double mllw_z)
  : mllw_z_(mllw_z) {}
  std::optional<double> mllwHeight(double, double) const override
  {
    return mllw_z_;
  }

private:
  double mllw_z_;
};

/// @brief Convert one S-102 tile to a store-convention GeoTIFF.
///
/// Pipeline: open @p src_path (S102 open options pinned:
/// `DEPTH_OR_ELEVATION=DEPTH`, `NORTH_UP=YES` — a GDAL upgrade can't silently
/// flip sign or row order); hard-fail unless dataset metadata declares
/// `VERTICAL_DATUM_ABBREV=MLLW` (a Great Lakes LWD tile must not be shifted
/// as MLLW); locate the depth/uncertainty bands by band Description (falling
/// back to bands 1/2 with a warning when descriptions are absent); read each
/// band's declared nodata (fallback 1e6); warp both bands to geographic WGS84
/// (nearest, nodata-aware); apply `height = mllw_z(lat, lon) − depth` per
/// cell; write @p out_path as 2-band Float64 GeoTIFF (band 1 ellipsoidal
/// height, band 2 uncertainty, NaN nodata).
///
/// Works on any 2-band GDAL raster honouring the same metadata contract —
/// the test suite uses synthetic projected GeoTIFFs, no S102 driver needed.
///
/// @throws std::runtime_error on open failure, wrong/missing vertical datum,
/// missing bands, or write failure.
void convertTile(
  const std::string & src_path,
  const std::string & out_path,
  const VerticalDatumProvider & datum);

}  // namespace marine_bathymetry_store::s102

#endif  // S102__CONVERT_HPP_
