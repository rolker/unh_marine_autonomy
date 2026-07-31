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

#ifndef S102__RUN_HPP_
#define S102__RUN_HPP_

#include <cstddef>
#include <string>

#include "marine_bathymetry_store/bathy_cell.hpp"
#include "s102/catalog.hpp"
#include "s102/convert.hpp"

/// @file
/// @brief S-102 import orchestration (#278): discover → fetch → convert →
/// import, as a library function so the pipeline is testable offline without
/// spawning the CLI.
///
/// **Scratch stores only until uma#276 lands** (ADR-0010 D7 precondition):
/// the current bathymetry_layer treats high-σ cells as reject → LETHAL, so
/// chart-grade σ under a live costmap would render charted regions keepout.
/// Never point `--store` at a store a live costmap reads.

namespace marine_bathymetry_store::s102
{

/// @brief Default public bucket for NOAA S-102 ed3.0.0.
inline constexpr const char * kDefaultBucket =
  "https://noaa-s102-pds.s3.amazonaws.com";

/// @brief Everything one import run needs.
struct S102ImportOptions
{
  BBox area;                   ///< required: geographic query area
  std::string store_dir;       ///< required: target store directory
  SourceLayer layer = SourceLayer::Reference;
  std::string cache_dir;       ///< required: tile corpus cache (single-writer:
                               ///< one run per cache at a time; see fetch.cpp)
  /// Catalog GeoPackage URL or local path. Empty → discover the newest from
  /// `kDefaultBucket` (cached to `<cache>/catalog.gpkg` for offline re-runs).
  std::string catalog;
  bool offline = false;        ///< never touch the network
  bool force = false;          ///< re-convert + re-import unchanged tiles
  double cell_size = 0.5;      ///< store default cell size (as import_geotiff)
  const VerticalDatumProvider * datum = nullptr;  ///< required
};

/// @brief Per-run counts for reporting.
struct S102ImportSummary
{
  std::size_t discovered = 0;  ///< catalog rows intersecting the area
  std::size_t fetched = 0;     ///< tiles downloaded this run (not cache hits)
  std::size_t skipped = 0;     ///< tiles skipped as already imported (no-op)
  std::size_t converted = 0;   ///< tiles converted this run
  std::size_t imported_cells = 0;  ///< store cells written this run
};

/// @brief Run the full pipeline.
///
/// Whole-pipeline idempotency: `<store>/s102_imported.json` records
/// `tile_id → issuance` per store (one cache can feed many stores); tiles
/// whose issuance is unchanged are skipped entirely unless @p options.force.
/// Store-level StoreMetadata provenance is written only when the store has
/// none yet — an S-102 import must not clobber an existing survey's metadata.
///
/// @throws std::runtime_error on any stage failure (fail-loud). A failure
/// *before* the final save leaves the store on disk untouched — fetch/convert
/// work only in the cache (atomic renames) and the in-memory store. The final
/// `save()` is not itself transactional: it writes tiles incrementally with no
/// rollback, so a failure *during* save (e.g. disk full) can leave the store
/// partially written. The sidecar is committed only after a successful save.
S102ImportSummary runS102Import(const S102ImportOptions & options);

}  // namespace marine_bathymetry_store::s102

#endif  // S102__RUN_HPP_
