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
#include <vector>

#include "marine_autonomy/gggs.h"
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

/// @brief Result of a GeoTIFF import, including the anti-clobber side effect.
///
/// `draft_tiles_touched` is the cross-layer coordination seam for the display
/// cache (camp#171/#172, ADR-0008): after a `Processed` import clears overlapped
/// `Draft` cells cell-wise (ADR-0010 D8), it lists the `Draft` tiles that were
/// **touched** (marked dirty) — NOT removed. Nothing is removed on disk under
/// cell-wise clearing: a cleared draft cell is written no-data (NaN) in place and
/// persisted through the normal dirty-tile save path. A cache consumer invalidates
/// exactly these tiles. For non-`Processed` imports the draft fields stay
/// zero/empty (only a `Processed` import clears `Draft`).
struct ProcessedImportResult
{
  /// Distinct cells written into @p layer (a new cell; replacements don't recount).
  std::size_t cells_imported = 0;
  /// `Draft` cells transitioned from data → no-data (cell-wise anti-clobber).
  /// Only cells the processed import populated AND that `Draft` already held data
  /// at are counted — a processed no-data cell (gated-drop hole) leaves the
  /// overlapping draft cell intact.
  std::size_t draft_cells_cleared = 0;
  /// `Draft` tiles with ≥1 cleared cell (each appears once). Cache-invalidation
  /// seam for camp#171/#172; these tiles are touched (dirtied), not removed.
  std::vector<gggs::GridIndex> draft_tiles_touched;
};

/// @brief Import @p path into @p layer's single fused surface.
///
/// Reads the GeoTIFF, fills each pixel's **footprint** of GGGS cells at the
/// target level (every store cell a pixel covers, so a coarser-than-store source
/// still produces coverage), converts the vertical datum at import (§D4), keeps
/// the lowest-uncertainty value on contention, and bulk-inserts the resulting
/// tiles into @p layer via `BathymetryStore::importTiles`. That insert replaces
/// **per tile**: each touched tile replaces any resident tile at that grid, so a
/// partial GeoTIFF patch drops previously stored cells inside a touched tile
/// rather than merging per cell — the lowest-uncertainty-on-contention rule
/// applies only among this import's own pixels. See `importTiles`' additive-merge
/// footgun note for the shrinking-re-import consequence.
///
/// **Anti-clobber (ADR-0010 D8):** when @p layer is `Processed`, after the insert
/// the importer clears overlapped `Draft` cells **cell-wise — only where this
/// import has data** — by writing them no-data via `BathymetryStore::set(Draft,
/// …, {})` (persisted through the normal dirty-tile save path; there is no
/// tile/cell-erase API and `save()` never deletes on-disk tiles). Draft cells in
/// the re-run's gated-drop holes (cells this import left no-data) survive — the
/// query overlay resolves them under `Processed > Draft` where the processed
/// surface has data, and shows the surviving draft where it does not. Clearing
/// operates at this import's cell/level granularity; draft data at a *different*
/// GGGS level is not reached (in practice draft and processed both come from CUBE
/// at the store level). Non-`Processed` imports clear nothing.
///
/// @return A `ProcessedImportResult` (cells imported + anti-clobber side effect).
/// @throws std::invalid_argument on a bad band index;
///         std::logic_error if @p layer is `Reference` and the store is not
///         `reference_writable` (the read-only-prior gate);
///         std::runtime_error on GDAL failure, a non-WGS84 / rotated raster, or
///         a missing geotransform.
ProcessedImportResult importGeoTiff(
  BathymetryStore & store, SourceLayer layer,
  const std::string & path,
  const GeoTiffImportOptions & options = GeoTiffImportOptions{});

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__GEOTIFF_IMPORT_HPP_
