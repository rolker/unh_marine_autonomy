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

#ifndef MARINE_TILED_RASTER_STORE__COVERAGE_MANIFEST_HPP_
#define MARINE_TILED_RASTER_STORE__COVERAGE_MANIFEST_HPP_

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "marine_autonomy/gggs.h"

/// @file
/// @brief Mixed-level coverage declaration for a tiled raster layer
///        (`uma-ADR-0013` D3), with the per-tile geometric error of D1/D2.
///
/// A layer declares the set of `(level, index)` zones it actually holds, at
/// mixed levels, in one object. A conventional pyramid is the degenerate case
/// where that set is ancestrally closed; the ENC chart ladder, a mixed-level
/// `reference/`, and a `draft/` layer with a generated sidecar are all just
/// different coverage sets over the same GGGS.
///
/// **Safety (`uma-ADR-0013` D8): the manifest is DERIVED and ADVISORY.** A stale
/// or absent manifest is a rendering artifact, never a safety one — shoal-
/// finding, least-depth and clearance queries must keep reading the tiles to the
/// finest available level and must never consult a manifest (or an LOD level) to
/// decide what to read.
///
/// **Additive by construction.** A layer with no manifest file still reads:
/// `scanCoverage()` recovers the same set from a directory scan. That scan is
/// this library's own fallback for a producer that needs coverage as *input*; it
/// is a different thing from `uma-ADR-0013`'s consumer-side fallback to
/// level-as-resolution when the per-tile geometric error is absent.
///
/// The run encoding follows OGC 2D TMS 2.0 `TileMatrixSetLimits` and Cesium
/// `layer.json` `available` (the precedents `uma-ADR-0013` D3 names): contiguous
/// column runs within a row, which compresses a dense region and degenerates to
/// explicit pairs for a sparse one.

namespace marine_tiled_raster_store
{

/// @brief One contiguous run of columns within a single row at a single level.
///
/// @c col_min and @c col_max are inclusive. A run is only merged from adjacent
/// columns that agree on @c geometric_error_m, so the error stays **per tile**
/// (`uma-ADR-0013` D1) rather than being widened to a per-row maximum.
struct RowRun
{
  uint32_t row = 0;
  uint32_t col_min = 0;
  uint32_t col_max = 0;
  /// Geometric error in metres for every tile in this run — the error
  /// introduced if the tile is rendered and its children are not
  /// (`uma-ADR-0013` D1). `std::nullopt` means the producer recorded none, and
  /// a consumer falls back to today's level-as-resolution behaviour.
  std::optional<double> geometric_error_m;
};

/// @brief The run-encoded coverage of one level.
struct LevelCoverage
{
  uint8_t level = 0;
  std::vector<RowRun> runs;
};

/// @brief The set of grids a layer holds, across levels, with each grid's
///        optional geometric error.
class CoverageManifest
{
public:
  /// @brief Record @p grid as covered, carrying @p geometric_error_m.
  ///
  /// Re-adding a grid overwrites its recorded error. An invalid grid is ignored
  /// (a caller that could not reconstruct one has already reported the skip).
  void add(
    const gggs::GridIndex & grid,
    std::optional<double> geometric_error_m = std::nullopt);

  /// @brief Whether the layer holds a tile at @p grid.
  bool contains(const gggs::GridIndex & grid) const;

  /// @brief The recorded geometric error for @p grid, or `nullopt` when the grid
  ///        is absent or its producer recorded none.
  std::optional<double> geometricError(const gggs::GridIndex & grid) const;

  /// @brief Number of grids held at @p level.
  std::size_t countAt(uint8_t level) const;

  /// @brief Total number of grids held, across all levels.
  std::size_t size() const {return entries_.size();}
  bool empty() const {return entries_.empty();}

  /// @brief The levels holding at least one grid, ascending (coarsest first).
  std::vector<uint8_t> levels() const;

  /// @brief The grids held at @p level, in GGGS order (row then column).
  std::vector<gggs::GridIndex> gridsAt(uint8_t level) const;

  /// @brief Run-encode the coverage, coarsest level first.
  std::vector<LevelCoverage> encode() const;

  /// @brief Rebuild a manifest from its run encoding.
  ///
  /// Each `(level, row, col)` is reconstructed through the same geographic
  /// round-trip `gridFromTileName` uses, so a run naming a grid that does not
  /// exist at that level is skipped loudly rather than fabricated.
  static CoverageManifest decode(const std::vector<LevelCoverage> & levels);

private:
  // GridIndex's operator< orders by level, then row, then column — exactly the
  // order the run encoder and the per-level accessors want, so one ordered map
  // serves lookup, grouping, and encoding without a secondary index.
  std::map<gggs::GridIndex, std::optional<double>> entries_;
  std::map<uint8_t, std::size_t> count_by_level_;
};

/// @brief Reconstruct the GridIndex named `<level>_<row>_<col>` through the
///        public geographic lookup.
///
/// The `(level, row, col)` constructor is `Level`-private by design, so the
/// filename parts give the grid's south/west corner via the level spec and the
/// centre point maps back through `Level::gridIndex`. The `tileFilename`
/// round-trip verifies the arithmetic: a mismatch (say a future level-spec
/// change) makes this **skip the file loudly** — warn on `std::cerr` and return
/// an invalid GridIndex — instead of folding it into the wrong parent. It never
/// throws, so one malformed name can never abort a whole run.
///
/// @param name The filename the parts came from, used only for the warning text
///   and the round-trip comparison.
///
/// @note The column width uses the latitude-based `gggs::latitudeScaleFactor`
///   overload, which disagrees with the authoritative row-based
///   `LevelSpecs::latitudeScaleFactor(row)` exactly on the 72/80 degree polar
///   band boundaries. The survey envelope is non-polar (|lat| < 72; see
///   `tile_io.hpp`), so the two agree here; on a polar tile they could diverge,
///   but the round-trip check would then fail and skip the file rather than
///   mis-place it — so the assumption fails safe.
gggs::GridIndex gridFromTileName(
  uint8_t level, uint32_t row, uint32_t col, const std::string & name);

/// @brief Enumerate the `<level>_<row>_<col>.tif` grids in @p dir (names only —
///        nothing is loaded).
///
/// @param level When set, only tiles at that level are returned; when
///   `std::nullopt`, tiles at **every** level are returned. The all-level form
///   is what a mixed-level layer needs (`uma-ADR-0013` D3).
/// @param skipped Incremented for every tile-shaped name that could not be
///   turned into a grid. A name whose level field **overflowed** is counted
///   regardless of @p level — that field is exactly what could not be trusted,
///   so the filter cannot honestly exclude it. Every other skip (a parsed level
///   that is not a GGGS level, an out-of-range row/column, a failed geographic
///   reconstruction) is counted only for names the @p level filter admits, so
///   the level-filtered form ignores stray out-of-level files exactly as a
///   single-level builder always has. A caller that treats any skip as fatal
///   therefore has a wider refusal surface under the all-level form —
///   deliberately so: under level discovery an unreadable name at any level is
///   coverage that will be missing from every level built beneath it.
/// @return The reconstructed grids; a non-existent @p dir yields an empty
///   vector rather than an error.
std::vector<gggs::GridIndex> gridsInDir(
  const std::string & dir, std::optional<uint8_t> level, std::size_t & skipped);

/// @brief Recover a layer's coverage by scanning @p dir at every level.
///
/// The fallback for a layer with no manifest file, and the input path for a
/// producer that must know which regions hold data at which level before it can
/// pyramid them. Carries no geometric error: a scan can only see filenames.
CoverageManifest scanCoverage(const std::string & dir, std::size_t & skipped);

/// @brief Write @p manifest to @p path as `coverage-manifest/1` JSON.
///
/// Atomic publish: the document is written to `<path>.tmp` and renamed over
/// @p path, so a reader sees either the old file or the new one whole (the
/// `registry.cpp` precedent in `marine_bathymetry_store`).
///
/// @param kind Free-form provenance label recorded in the document — `"derived"`
///   for a generated `overviews/` sidecar, `"native"` for imported tiles.
/// @throws std::runtime_error on any write or rename failure.
void saveCoverageManifest(
  const CoverageManifest & manifest, const std::string & path,
  const std::string & kind);

/// @brief Read a `coverage-manifest/1` document.
///
/// **Tolerant**: a missing file, malformed JSON, an unknown schema tag, or a
/// structurally wrong document warns on `std::cerr` and returns `std::nullopt`
/// rather than throwing. The manifest is advisory (D8) — a consumer that cannot
/// read it falls back to `scanCoverage()`, which is never worse than what it had
/// before the manifest existed.
std::optional<CoverageManifest> loadCoverageManifest(const std::string & path);

/// @brief Filename of a layer's coverage manifest: `coverage.json`.
///
/// Not a `.tif`, so the flat-layout tile loaders already skip it silently and no
/// loader change is needed to introduce it into a layer or sidecar directory.
inline const char * coverageManifestFilename() {return "coverage.json";}

}  // namespace marine_tiled_raster_store

#endif  // MARINE_TILED_RASTER_STORE__COVERAGE_MANIFEST_HPP_
