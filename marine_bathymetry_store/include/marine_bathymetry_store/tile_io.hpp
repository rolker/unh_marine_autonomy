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

#include "geographic_msgs/msg/geo_point.hpp"
#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathy_cell.hpp"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"
#include "marine_bathymetry_store/registry.hpp"

/// @file
/// @brief Per-tile GeoTIFF persistence (ADR-0002 §D5, simplified by #248).
///
/// Each GGGS tile is persisted as **one** GeoTIFF, WGS84-georeferenced from its
/// grid corners and written north-up (raster row 0 = north), so persistence flips
/// rows relative to the in-memory GGGS cell order (row 0 = south):
///
/// - `<level>_<row>_<col>.tif` — 2-band `Float64` value tile (depth,
///   uncertainty), NaN no-data on both.
///
/// The pre-#248 `_time.tif` (Int64 ns) and `_source.tif` (UInt16 source index)
/// companions were dropped (ADR-0002 Amendment A2.2): the store is a regenerable
/// cache, the per-cell time band's only reader (the costmap staleness gate) was
/// retired, and per-cell source provenance is a constant for a single platform.
///
/// The quality/maturity axis (Survey / Reference) is encoded as the on-disk
/// subdirectory (`survey/`, `reference/`); each holds **one fused** set of value
/// tiles directly (no per-day epoch subdirectory since #221). Coarse provenance
/// moves to the store-wide `registry.json` `StoreMetadata` (ADR-0005 #248).
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

/// @brief Subdirectory name for a source layer (`"survey"` / `"reference"`).
///        Each holds one fused set of value tiles directly (#221).
std::string layerDirName(SourceLayer layer);

/// @brief Write one tile as a single value GeoTIFF at @p path (`<grid>.tif`).
/// @throws std::runtime_error on any GDAL failure.
void saveTile(const BathymetryTile & tile, const std::string & path);

/// @brief Load a tile from its value GeoTIFF, reconstructing its GridIndex at
///        @p level.
///
/// @p path is the value tile (`<grid>.tif`). The grid is recovered from the
/// value file's geotransform (the cell center maps back through @p level), so a
/// file written at a different level is rejected. @p level must therefore be the
/// level the file was written at — callers loading a multi-level store recover it
/// from the `<level>_<row>_<col>` filename prefix (see `levelFromTileFilename`)
/// and pass it per-file. The returned tile is clean (not dirty).
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
/// Layout: `<dir>/<layer>/<level>_<row>_<col>.tif` plus a store-wide
/// `<dir>/registry.json` (flat, no per-day epoch subdirectory since #221).
/// Creates directories as needed. Clean tiles are skipped (incremental save). The
/// coarse `StoreMetadata` (a store-wide sidecar, not per-layer) is written once at
/// the end via @p metadata; pass `nullptr` to skip it (e.g. a caller with no
/// metadata to persist).
/// @return The number of tiles written.
/// @throws std::runtime_error on any GDAL failure; std::filesystem::filesystem_error
///         (a std::runtime_error subclass) on a directory-creation failure.
std::size_t save(
  BathymetryStore & store, const std::string & dir,
  const StoreMetadata * metadata = nullptr);

/// @brief Load every tile found under @p dir into @p store, plus the metadata.
///
/// Scans `<dir>/<layer>/` for value tiles (`<level>_<row>_<col>.tif`). Each
/// tile's level is recovered from its filename (`levelFromTileFilename`), so a
/// **mixed-level** store loads correctly — the store's default level is irrelevant
/// to loading (ADR-0002 §D2). Subdirectory entries (stale old-style epoch dirs, if
/// any) are ignored — there is no production data to migrate (#221). If @p
/// metadata is non-null, `registry.json` is loaded into it. Loaded tiles are clean.
/// @return The number of tiles loaded.
/// @throws std::runtime_error on any GDAL failure or a per-file level mismatch;
///         std::filesystem::filesystem_error (a std::runtime_error subclass) on a
///         directory-iteration failure.
std::size_t load(
  BathymetryStore & store, const std::string & dir,
  StoreMetadata * metadata = nullptr);

/// @brief Load only tiles whose geographic AABB intersects [@p min_pt, @p max_pt].
///
/// Mirrors `load()` in structure (flat layer-dir scan, level-recovery) but gates
/// each tile on two conditions before paying the GDAL I/O cost:
/// 1. The tile's geographic bounding box (derived from its `<level>_<row>_<col>`
///    filename, not from a file open) intersects the box [min_pt, max_pt]
///    (inclusive on all boundaries — a tile whose edge exactly touches the box
///    boundary is included, matching `forEachCellBestSource` semantics).
/// 2. The tile is not already resident in @p store for this layer (idempotent:
///    a second call on the same box skips already-loaded tiles).
///
/// Tiles at any GGGS level are handled correctly (ADR-0002 §D2): each tile's
/// overlap is tested using its own `GridIndex` geographic accessors rather than
/// a single-level `GridAreaIterator`.
///
/// If @p metadata is non-null, `registry.json` is loaded into it (same as
/// `load()`). Loaded tiles are clean.
///
/// @note Concurrency: safe only if no concurrent writer is active in the same
///   process. A writer racing to dirty a tile between the overlap check and the
///   tile-insertion is undefined behaviour — acceptable before issue #189 because
///   the store is currently single-threaded. The #189 implementer must re-audit.
///
/// @return The number of tiles loaded (already-resident tiles not counted).
/// @throws std::runtime_error on any GDAL failure or a per-file level mismatch;
///         std::filesystem::filesystem_error on a directory-iteration failure.
std::size_t loadWindow(
  BathymetryStore & store, const std::string & dir,
  const geographic_msgs::msg::GeoPoint & min_pt,
  const geographic_msgs::msg::GeoPoint & max_pt,
  StoreMetadata * metadata = nullptr);

/// @brief Remove all **clean** tiles outside [@p min_pt, @p max_pt] from @p store.
///
/// Iterates every layer in the store and erases tiles whose geographic AABB does
/// not intersect [min_pt, max_pt] (same inclusive-boundary semantics as
/// `loadWindow`).
///
/// **Dirty-tile guard (safety invariant)**: a tile flagged `dirty()` is **never**
/// evicted, regardless of its position. A dirty Survey tile is live sensor data that
/// has not yet reached disk; evicting it would lose that data with no reload path.
/// Reference tiles are always clean (never mutated at runtime) and are always
/// safely evictable. Save the store before calling `evictOutside` if you want all
/// outside tiles to be eligible for eviction.
///
/// @note Concurrency: safe only if no concurrent writer is active in the same
///   process. A writer racing to dirty a tile between the dirty check and the
///   erase is undefined behaviour — acceptable before issue #189. See `loadWindow`
///   for the full concurrency note.
///
/// @return The number of tiles evicted (dirty tiles that were skipped not counted).
std::size_t evictOutside(
  BathymetryStore & store,
  const geographic_msgs::msg::GeoPoint & min_pt,
  const geographic_msgs::msg::GeoPoint & max_pt);

/// @brief Atomically replace a store's `chart/` layer with a staged directory
///        (ADR-0010 D7 wholesale regeneration — never a merge).
///
/// The updater builds the new chart layer (tiles + edition registry) into
/// @p staged_chart_dir, then calls this to swap it in. The rename is the
/// single commit point: a failure at any step leaves the previous `chart/`
/// fully intact. Always targets `chart/` — deliberately NOT parameterized by
/// SourceLayer, so no caller can wholesale-replace survey/reference data.
///
/// Sequence: validate the store dir and staged dir (staged exists as a real
/// directory — not a symlink — does not alias the live `chart/` or its backup,
/// contains no symlinked entries, and holds ≥1 `.tif`); recover from
/// an interrupted prior swap (restore `.chart_backup/` → `chart/` when `chart/`
/// is absent — the crash struck mid-commit — otherwise drop the now-stale
/// backup); rename existing `chart/` → `.chart_backup/`; rename staged →
/// `chart/` (atomic on one filesystem); on failure restore the backup and
/// rethrow; on success remove the backup (best-effort — see @throws).
///
/// Caller contract: run only while no consumer holds the store open (D7's
/// enforced nav-down precondition); the staged dir must be on the same
/// filesystem as @p store_dir for rename atomicity — this is now enforced up
/// front (a cross-device staged dir is rejected before the live layer is
/// touched) rather than left to fail mid-swap.
///
/// @throws std::runtime_error on validation failure (missing/empty staged dir,
///         a symlinked staged dir, a symlinked entry inside the staged dir, a
///         staged dir aliasing the live `chart/` or its backup, a non-directory
///         store dir, or a cross-device staged dir — all detected before any swap);
///         std::filesystem::filesystem_error on a filesystem operation up to and
///         including the commit point failing — the commit-point rename (after a
///         best-effort backup restoration that never masks the original error)
///         and the pre-swap crash-recovery steps that run *before* it: restoring
///         an orphaned `.chart_backup/` (`fs::rename`) and clearing a stale one
///         (`fs::remove_all`) both use throwing overloads. Nothing *after* the
///         commit throws: the post-commit backup cleanup uses the `error_code`
///         overload and only warns on `std::cerr`, so a committed swap is never
///         reported to the caller as a failure (a leftover backup is dropped by
///         the next run's crash-recovery step).
void replaceChartLayer(
  const std::string & staged_chart_dir, const std::string & store_dir);

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__TILE_IO_HPP_
