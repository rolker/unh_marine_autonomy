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

#ifndef MARINE_BATHYMETRY_STORE__TILE_IO_HPP_
#define MARINE_BATHYMETRY_STORE__TILE_IO_HPP_

#include <cstddef>
#include <string>

#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathy_cell.hpp"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"
#include "marine_bathymetry_store/registry.hpp"

/// @file
/// @brief Per-tile GeoTIFF persistence (ADR-0002 §D5, amended by #178).
///
/// Each GGGS tile is persisted as **three** GeoTIFFs, all WGS84-georeferenced
/// from the same grid corners and written north-up (raster row 0 = north), so
/// persistence flips rows relative to the in-memory GGGS cell order
/// (row 0 = south):
///
/// - `<level>_<row>_<col>.tif` — 2-band `Float64` value tile (depth,
///   uncertainty), NaN no-data on both.
/// - `<level>_<row>_<col>_time.tif` — 1-band `Int64` timestamp tile
///   (nanoseconds since epoch, ROS-native and exact; 0 = unset, no no-data tag).
/// - `<level>_<row>_<col>_source.tif` — 1-band `UInt16` source-index tile
///   (registry index, ADR-0005 D2/D8; 0 = no-data/unset).
///
/// The quality/maturity axis (Processed / Draft / Chart) is still encoded as the
/// on-disk subdirectory (`processed/`, `draft/`, `chart/`); the platform/sensor
/// provenance axis moves to the per-cell source index + the store-wide
/// `registry.json` (ADR-0005 D8). On load, a missing `_time.tif` / `_source.tif`
/// (pre-migration single-platform data) fills timestamp / source_index with 0.
///
/// @note Round-trip persistence is validated for **non-polar** latitudes
/// (|lat| < 72°), the intended lake/coastal survey envelope. Near GGGS's polar
/// longitude-scaling boundaries (±72°, ±80°) and the ±90° latitude clamp, a
/// grid's per-cell angular extent diverges from the single uniform pixel size a
/// GeoTIFF geotransform can express, so the linear geotransform becomes an
/// approximation and the level-match check on load may mis-fire. Polar tiles are
/// out of scope for this phase.

namespace marine_bathymetry_store
{

/// @brief GeoTIFF filename (no directory) for a grid's **value** tile:
///        `<level>_<row>_<col>.tif`.
std::string tileFilename(const gggs::GridIndex & grid);

/// @brief Subdirectory name for a source layer (`"processed"` / `"draft"`).
std::string layerDirName(SourceLayer layer);

/// @brief Write one tile as three GeoTIFFs derived from the value-tile @p path.
///
/// @p path is the value tile (`<grid>.tif`); the `_time.tif` and `_source.tif`
/// companions are written alongside it (same directory, suffixed stem).
/// @throws std::runtime_error on any GDAL failure.
void saveTile(const BathymetryTile & tile, const std::string & path);

/// @brief Load a tile from its three GeoTIFFs, reconstructing its GridIndex at
///        @p level.
///
/// @p path is the value tile (`<grid>.tif`); the `_time.tif` and `_source.tif`
/// companions are read from the derived paths. A **missing** companion fills the
/// corresponding band with 0 (pre-migration backward compatibility). The grid is
/// recovered from the value file's geotransform (the cell center maps back
/// through @p level), so a file written at a different level is rejected. @p
/// level must therefore be the level the file was written at — callers loading a
/// multi-level store recover it from the `<level>_<row>_<col>` filename prefix
/// (see `levelFromTileFilename`) and pass it per-file.
/// The returned tile is clean (not dirty).
/// @throws std::runtime_error on GDAL failure, wrong dimensions, or a
///         geotransform that doesn't match any grid at @p level.
BathymetryTile loadTile(const std::string & path, const gggs::Level & level);

/// @brief Recover the GGGS level encoded in a `<level>_<row>_<col>.tif`
///        filename (the prefix before the first underscore).
///
/// The store is multi-level (ADR-0002 §D2), so tiles at different levels share a
/// directory; the level is not knowable from the store but is encoded in every
/// tile filename. Persistence parses it per-file and passes it to `loadTile`.
/// @throws std::runtime_error if @p filename has no parseable level prefix or a
///         level out of the GGGS range.
uint8_t levelFromTileFilename(const std::string & filename);

/// @brief Persist every **dirty** tile of @p store under @p dir, then clear
///        their dirty flags. Also writes the store-wide `registry.json`.
///
/// Layout: `<dir>/<layer>/<epoch>/<level>_<row>_<col>{,_time,_source}.tif`, plus
/// a per-epoch `provenance` marker file and a store-wide `<dir>/registry.json`.
/// Creates directories as needed. Clean tiles are skipped (incremental save). An
/// epoch flagged `supersedes_disk` (a wholesale import) has its on-disk tile
/// directory cleared first, so a compacted epoch covering fewer grids never
/// resurrects a stale tile on the next load. The registry (a store-wide sidecar,
/// not per-layer) is written once at the end via @p registry; pass `nullptr` to
/// skip it (e.g. a caller with no registry to persist).
/// @return The number of tiles written.
/// @throws std::runtime_error on any GDAL failure; std::filesystem::filesystem_error
///         (a std::runtime_error subclass) on a directory-creation failure.
std::size_t save(
  BathymetryStore & store, const std::string & dir,
  const SourceRegistry * registry = nullptr);

/// @brief Load every tile found under @p dir into @p store, plus the registry.
///
/// Scans `<dir>/<layer>/<epoch>/` for value tiles (`<level>_<row>_<col>.tif`),
/// **skipping** the `_time.tif` / `_source.tif` companions (loaded with their
/// value tile). Each tile's level is recovered from its filename
/// (`levelFromTileFilename`), so a **mixed-level** store loads correctly — the
/// store's default level is irrelevant to loading (ADR-0002 §D2). Each epoch's
/// provenance is read from its `provenance` marker (CRLF-safe; default
/// `live-fused` if absent). If @p registry is non-null, `registry.json` is
/// loaded into it. Loaded tiles are clean.
/// @return The number of tiles loaded.
/// @throws std::runtime_error on any GDAL failure or a per-file level mismatch;
///         std::filesystem::filesystem_error (a std::runtime_error subclass) on a
///         directory-iteration failure.
std::size_t load(
  BathymetryStore & store, const std::string & dir,
  SourceRegistry * registry = nullptr);

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__TILE_IO_HPP_
