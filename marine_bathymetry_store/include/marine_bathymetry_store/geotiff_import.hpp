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
#include <limits>
#include <optional>
#include <string>

#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/epoch.hpp"

/// @file
/// @brief GeoTIFF → store epoch importer (ADR-0002 Phase 2).
///
/// Imports a depth/uncertainty GeoTIFF — a `cube_bathymetry` `bag_to_geotiff`
/// replay product, or any north-up geographic WGS84 raster — as **one epoch**
/// of a source layer. Each pixel fills its **footprint** of store-level GGGS
/// cells: when the input lattice is GGGS-aligned at the store's level (cube
/// products are) that is a cell-exact copy with no resampling; a coarser
/// source (e.g. a 5 m contour prior into a 0.5 m store) nearest-neighbour
/// fills every cell it covers; a finer source resolves per cell by lowest
/// uncertainty.
///
/// One epoch = one import call: the epoch is replaced **wholesale**
/// (`BathymetryStore::importEpoch`), per the ADR-0002 §A1.2 compaction
/// semantics. Re-importing supersedes, subject to the provenance ordering.

namespace marine_bathymetry_store
{

/// @brief Options for `importGeoTiff`.
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
  /// non-finite. NaN (the default) makes such cells never-reliable in the
  /// safety query — the conservative choice for an unattributed source.
  double default_uncertainty = std::numeric_limits<double>::quiet_NaN();
  /// Acquisition time (Unix seconds) recorded on every imported cell — a
  /// whole-file product carries no per-cell time. 0 = unset.
  double timestamp = 0.0;
  /// Vertical conversion applied at import (ADR-0002 §D4):
  /// `height = depth_scale * pixel + depth_offset`. The defaults pass
  /// store-convention inputs (ellipsoidal height, up-positive) through
  /// unchanged. A positive-down depths-below-lake-surface product imports
  /// with `depth_scale = -1` and `depth_offset` = the lake surface's
  /// ellipsoidal height.
  double depth_scale = 1.0;
  double depth_offset = 0.0;
};

/// @brief Import @p path as the **whole content** of @p layer's @p epoch.
///
/// Pixels whose depth is non-finite are skipped. When several pixels of a
/// finer-than-store input land in one cell, the **lowest-uncertainty** value
/// wins (a finite uncertainty always beats NaN; among equal candidates the
/// first read is kept).
/// @return The number of cells imported, or `std::nullopt` if the import was
///         refused by the §A1.2 provenance ordering (existing epoch is
///         `Replayed`, @p provenance is `LiveFused`).
/// @throws std::runtime_error on GDAL failure, a non-geographic / non-WGS84
///         raster, or a rotated geotransform; std::invalid_argument on a bad
///         epoch label or band index.
std::optional<std::size_t> importGeoTiff(
  BathymetryStore & store, SourceLayer layer, const Epoch & epoch,
  const std::string & path, Provenance provenance,
  const GeoTiffImportOptions & options = GeoTiffImportOptions{});

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__GEOTIFF_IMPORT_HPP_
