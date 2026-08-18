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

#ifndef MARINE_SIDESCAN_MOSAIC__BATHY_DEM_HPP_
#define MARINE_SIDESCAN_MOSAIC__BATHY_DEM_HPP_

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_tiled_raster_store/tiled_raster_tile.hpp"

/// @file
/// @brief Read-only bathymetric DEM lookup over a `marine_bathymetry_store`
///   value-tile directory (issue #297, ADR-0006 D9).
///
/// ADR-0006 D9 specifies the Tier-2 projection reads the bathy store's GeoTIFF
/// tiles **directly** — "a file-level dependency, no `marine_bathymetry_store`
/// package dependency, keeping the importer decoupled". This reader honours that:
/// it uses only `marine_tiled_raster_store` (the generic tile core both stores
/// build on) plus GGGS, and holds the store's layer directory names as its own
/// configurable strings rather than calling `marine_bathymetry_store::layerDirName`.
///
/// The on-disk contract it consumes (`marine_bathymetry_store/tile_io.hpp`):
/// `<store_root>/<layer>/<level>_<row>_<col>.tif`, 2-band `Float64` — band 0 is
/// depth as a **WGS84 ellipsoidal height, up-positive** (NaN = no data), band 1 is
/// 1-sigma vertical uncertainty (read but unused in v1, so a future sigma-weighted
/// variant needs no format change).

namespace marine_sidescan_mosaic
{

/// @brief Default layer search order for @ref BathyDem.
///
/// Mirrors the store's own `source_layers_by_priority` prefix (Survey, then
/// Reference). `chart` is deliberately **not** in the default: charted soundings
/// are cartographically shoal-biased for navigation safety, and a shoal-biased
/// vertical term would bias sample placement. That bias is wanted in the store's
/// **safety** query — the shallowest-reliable mode of ADR-0002 D7, refined by
/// ADR-0010 D4 — not in a geometric placement term. (ADR-0010 D7 is the separate
/// decision that the chart layer is regenerated wholesale from the ENC corpus.)
/// `chart` can be opted into explicitly where it is the only coverage.
extern const char kDefaultBathyLayers[];

/// @brief Split a comma-separated list, trimming whitespace and dropping empties.
std::vector<std::string> splitCsv(const std::string & csv);

/// @brief Bilinear DEM lookup over a bathy value-tile store.
///
/// Construction **hard-fails** (throws `std::runtime_error`) when the store is
/// unusable — none of the requested layer directories exists, or the scan finds
/// zero tiles. A mistyped path, a store whose layer directory was renamed, or an
/// empty store therefore aborts the run at startup instead of silently degrading
/// every sample to the flat-bottom fallback.
///
/// @note Not thread-safe: `depthAt` mutates the tile LRU and the hit counters.
///   The offline Tier-2 build is single-threaded.
class BathyDem
{
public:
  /// @brief Band index of depth in the store's 2-band value tile.
  static constexpr std::size_t kDepthBand = 0;
  /// @brief Bands read from each value tile (depth + uncertainty).
  static constexpr std::size_t kBands = 2;
  /// @brief Default resident-tile budget.
  ///
  /// A tile is 960×960 cells × @ref kBands `double` = **~14.7 MB**, so this
  /// default costs ~118 MB resident. Both bands are loaded even though v1 reads
  /// only @ref kDepthBand — the store's on-disk tile is 2-band, and a future
  /// sigma-weighted variant wants band 1 anyway. Size the budget against one
  /// lookup's working set: `resolveSource` probes up to `layers × levels` tiles
  /// before the 4-cell stencil, so a deep search order needs more than this
  /// (`sidescan_tier2_processed --bathy-cache-tiles N`).
  static constexpr std::size_t kDefaultCacheTiles = 8;

  /// @brief Scan @p store_root for the requested layers' tiles.
  ///
  /// @param store_root Bathy store root (the directory holding the layer dirs).
  /// @param layer_names Layer directory names in **descending priority**.
  /// @param cache_tiles Resident-tile budget for the LRU (>= 1).
  /// @throws std::runtime_error if no requested layer directory exists, or if the
  ///   scan finds no `<level>_<row>_<col>.tif` tiles at all.
  BathyDem(
    const std::string & store_root,
    const std::vector<std::string> & layer_names,
    std::size_t cache_tiles = kDefaultCacheTiles);

  /// @brief Bathymetric height at (@p lat_deg, @p lon_deg).
  ///
  /// @return WGS84 ellipsoidal height (up-positive, metres), or `nullopt` where
  ///   the store has no data at that position.
  ///
  /// Resolution order: layers in the configured priority order (outer), and within
  /// a layer the **finest** level with data at the position (inner) — so a fine
  /// patch is used where it exists while a coarse regional layer still covers the
  /// gaps. The resolved (layer, level) is then held for the **whole** bilinear
  /// stencil: all four neighbours are read at that level, because resolving
  /// per-neighbour would blend cells of different resolutions and seam wherever a
  /// fine patch ends.
  ///
  /// @note The stencil offsets the neighbours by the **query cell's** angular
  ///   spans. GGGS longitudinal spans step by 3x at the 72-deg and 80-deg bands, so
  ///   a probe across one of those parallels can re-sample the query cell (or skip
  ///   one). The blend stays well-formed — it degrades toward nearest-cell along
  ///   that axis — but the interpolation is only exact away from those two bands.
  ///
  /// Degraded cases: a neighbour that is NaN or lies in an absent tile drops out
  /// of the blend, which is then **renormalised by the weights that remain** — the
  /// bilinear interpolation over the neighbours that do exist, so a missing corner
  /// of negligible weight barely moves the result (an un-renormalised partial sum
  /// would instead bias toward zero, i.e. toward the ellipsoid). When the cell
  /// containing the query point has no data anywhere in the store, the result is
  /// `nullopt`.
  ///
  /// @throws std::runtime_error if a tile the scan listed cannot be loaded (a
  ///   corrupt or truncated tile is an error, never a silent no-coverage).
  std::optional<double> depthAt(double lat_deg, double lon_deg);

  /// @brief The layer directory names actually found, in priority order.
  const std::vector<std::string> & layers() const {return layer_names_;}

  /// @brief Levels seen in the scan, finest (highest number) first.
  const std::vector<int> & levels() const {return all_levels_;}

  /// @brief Total tiles found by the startup scan.
  std::size_t tileCount() const {return tile_count_;}

  /// @brief Successful @ref depthAt **calls** per layer directory name.
  ///
  /// These count store *probes* that returned data, not placed samples: a caller
  /// iterating a ray/bottom intersection calls `depthAt` several times for one
  /// sample, and a per-ping datum check adds one more. Useful for "which layer is
  /// actually feeding this run", never comparable one-for-one with a sample count.
  const std::map<std::string, std::size_t> & lookupsByLayer() const {return lookups_by_layer_;}

  /// @brief One-line human description of the scanned store (for diagnostics).
  std::string describe() const;

  /// @brief Non-fatal problems found by the startup scan, for the caller to print.
  ///
  /// A requested layer directory that is absent, or present with no value tiles,
  /// while **another** requested layer does have tiles: the store is usable but
  /// the search order is narrower than the operator asked for. Reported rather
  /// than thrown (reference-only coverage is legitimate) and never swallowed —
  /// a silently dropped `survey/` would orthorectify a survey against the coarse
  /// regional layer alone. Constructed as a library value so the tool owns the
  /// output stream.
  const std::vector<std::string> & warnings() const {return warnings_;}

private:
  struct Layer
  {
    std::string name;
    std::string dir;
    std::set<std::string> files;   ///< tile filenames present in this layer dir.
    std::vector<int> levels;       ///< finest (highest) first.
  };

  /// @brief Cache key: which layer, which level, which grid.
  using CacheKey = std::tuple<std::size_t, int, std::uint32_t, std::uint32_t>;
  using Tile = marine_tiled_raster_store::TiledRasterTile<double>;

  /// @brief The resolved source of a lookup: layer index + GGGS level.
  struct Source
  {
    std::size_t layer = 0;
    int level = 0;
  };

  std::optional<Source> resolveSource(double lat_deg, double lon_deg);
  std::optional<double> cellValue(std::size_t layer, int level_n, double lat_deg, double lon_deg);
  const Tile * tileFor(std::size_t layer, int level_n, const gggs::GridIndex & grid);

  std::string store_root_;
  std::vector<Layer> layers_;
  std::vector<std::string> layer_names_;
  std::vector<int> all_levels_;
  std::size_t tile_count_ = 0;
  std::size_t cache_tiles_ = kDefaultCacheTiles;

  std::list<std::pair<CacheKey, Tile>> lru_;   ///< front = most recently used.
  std::map<CacheKey, std::list<std::pair<CacheKey, Tile>>::iterator> cache_index_;

  std::map<std::string, std::size_t> lookups_by_layer_;
  std::vector<std::string> warnings_;
};

}  // namespace marine_sidescan_mosaic

#endif  // MARINE_SIDESCAN_MOSAIC__BATHY_DEM_HPP_
