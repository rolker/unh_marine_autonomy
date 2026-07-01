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

#ifndef MARINE_BATHYMETRY_STORE__GEOTIFF_IMPORT_HPP_
#define MARINE_BATHYMETRY_STORE__GEOTIFF_IMPORT_HPP_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include "marine_bathymetry_store/bathymetry_store.hpp"

/// @file
/// @brief Import a depth/uncertainty GeoTIFF into a store layer's single fused
/// surface (ADR-0002 Phase 2, §D4). Footprint fill, datum conversion at import,
/// and lowest-uncertainty contention resolution. Per-cell provenance stamping was
/// dropped in #248 (coarse provenance is store-level now, ADR-0005 #248 amendment);
/// the importer bulk-inserts its tiles via `BathymetryStore::importTiles`.

namespace marine_bathymetry_store
{

/// @brief Options governing a GeoTIFF import.
struct GeoTiffImportOptions
{
  /// 1-based raster band holding depth as **ellipsoidal height** (WGS84, m,
  /// up-positive — the store convention; `bag_to_geotiff` band 1). Non-finite
  /// pixels are no-data and skipped.
  int depth_band = 1;
  /// 1-based raster band holding 1-sigma vertical uncertainty (m), or 0 if
  /// the file has none (cells then get `default_uncertainty`).
  int uncertainty_band = 2;
  /// Uncertainty assigned when `uncertainty_band == 0` or the band reads
  /// non-finite **or non-positive** (zero uncertainty is treated as missing,
  /// not perfect — it would pass every reliability gate and carry infinite
  /// weight in 1/sigma^2 fusion). NaN (the default) makes such cells
  /// never-reliable in the safety query — the conservative choice.
  double default_uncertainty = std::numeric_limits<double>::quiet_NaN();
  /// Vertical conversion applied at import (ADR-0002 §D4):
  /// `height = depth_scale * pixel + depth_offset`. The defaults pass
  /// store-convention inputs (ellipsoidal height, up-positive) through
  /// unchanged. A positive-down depths-below-lake-surface product imports
  /// with `depth_scale = -1` and `depth_offset` = the lake surface's
  /// ellipsoidal height.
  double depth_scale = 1.0;
  double depth_offset = 0.0;
  /// GGGS level to import at. The store is multi-level (ADR-0002 §D2): a coarse
  /// chart prior imports at a coarse level, a fine survey grid at a fine level,
  /// and both can share the store. `std::nullopt` (the default) uses the
  /// store's default level (`store.level()`), preserving the single-level
  /// caller's behaviour; pass a level to match the source resolution.
  std::optional<uint8_t> level = std::nullopt;
};

/// @brief Import @p path into @p layer's single fused surface.
///
/// Reads the GeoTIFF, fills each pixel's **footprint** of GGGS cells at the
/// target level (every store cell a pixel covers, so a coarser-than-store source
/// still produces coverage), converts the vertical datum at import (§D4), keeps
/// the lowest-uncertainty value on contention, and bulk-inserts the resulting
/// tiles into @p layer via `BathymetryStore::importTiles` (merging into any
/// existing surface; last-write-wins per cell).
///
/// @return The number of distinct cells imported.
/// @throws std::invalid_argument on a bad band index;
///         std::logic_error if @p layer is `PreExisting` and the store is not
///         `pre_existing_writable` (the read-only-prior gate);
///         std::runtime_error on GDAL failure, a non-WGS84 / rotated raster, or
///         a missing geotransform.
std::size_t importGeoTiff(
  BathymetryStore & store, SourceLayer layer,
  const std::string & path,
  const GeoTiffImportOptions & options = GeoTiffImportOptions{});

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__GEOTIFF_IMPORT_HPP_
