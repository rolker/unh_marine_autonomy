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

#ifndef MARINE_BATHYMETRY_STORE__BATHYMETRY_STORE_HPP_
#define MARINE_BATHYMETRY_STORE__BATHYMETRY_STORE_HPP_

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "geographic_msgs/msg/geo_point.hpp"
#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathy_cell.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"

namespace marine_bathymetry_store
{

class BathymetryStore;
struct StoreMetadata;
// Persistence free functions (defined in tile_io.cpp). Forward-declared here so
// the store can friend them: `load` / `loadWindow` must reach getOrCreateTile on
// any layer — including the read-only `PreExisting` prior — which the public API
// otherwise forbids.  `evictOutside` must reach the non-const layerMap() to erase
// tiles without const_cast.
std::size_t save(
  BathymetryStore & store, const std::string & dir, const StoreMetadata * metadata);
std::size_t load(
  BathymetryStore & store, const std::string & dir, StoreMetadata * metadata);
std::size_t loadWindow(
  BathymetryStore & store, const std::string & dir,
  const geographic_msgs::msg::GeoPoint & min_pt,
  const geographic_msgs::msg::GeoPoint & max_pt,
  StoreMetadata * metadata);
std::size_t evictOutside(
  BathymetryStore & store,
  const geographic_msgs::msg::GeoPoint & min_pt,
  const geographic_msgs::msg::GeoPoint & max_pt);

/// @brief In-memory, GGGS-tiled, multi-layer, multi-level bathymetric store.
///
/// Holds, per `SourceLayer`, **one fused** map of tiles keyed by
/// `gggs::GridIndex` (which itself carries its level). The per-day epoch
/// dimension of ADR-0002 Amendment A1 was dropped (#221): a UTC calendar day is
/// a weak proxy for "a survey" and the single-coverage use case does not need
/// change detection, so each layer is one surface again. The store is
/// **level-agnostic**: tiles at heterogeneous GGGS levels coexist within a layer
/// (ADR-0002 §D2, amendment #151/#153). The constructor's `gggs_level` is
/// retained only as a **default for `cellIndex(lat,lon)`** (the write/query
/// convenience that turns coordinates into a cell) — it is *not* an invariant on
/// stored tiles.
///
/// Source priority is a **non-destructive query-time overlay** (see
/// `query.hpp`): the layers are independent, so a live CUBE write never clobbers
/// the read-only prior. The single fused surface within a layer is
/// last-write-wins per cell (there is no provenance ordering); `cube >
/// pre-existing` layer priority is the only provenance axis (ADR-0002 A2.1).
///
/// This phase has no ROS interface or distribution — just storage, in-process
/// queries (`query.hpp`), per-tile GeoTIFF persistence (`tile_io.hpp`), and the
/// GeoTIFF importer (`geotiff_import.hpp`).
class BathymetryStore
{
public:
  /// @brief Construct a store with a default level of GGGS quadtree
  ///        @p gggs_level.
  ///
  /// The default level only governs `cellIndex(lat,lon)` (the coordinate→cell
  /// convenience); stored tiles may be at any level (this store is multi-level,
  /// ADR-0002 §D2).
  ///
  /// @param pre_existing_writable Opt in to per-cell `set()` writes on the
  ///   read-only `PreExisting` prior layer. Default `false` — only the prior
  ///   importer (which converts the contour prior to ellipsoidal at import)
  ///   should pass `true`. Runtime consumers leave it `false` so live CUBE ingest
  ///   can never mutate the prior. `load()` (a friend) may still populate
  ///   PreExisting from disk regardless of this flag — the read-only gate is on
  ///   per-cell mutation (`set`), not on loading the prior. Direct tile mutation
  ///   is impossible from outside: `getOrCreateTile` is private (persistence-only).
  /// @throws std::out_of_range if the level is invalid (via gggs::Level).
  explicit BathymetryStore(uint8_t gggs_level, bool pre_existing_writable = false)
  : level_(gggs_level), pre_existing_writable_(pre_existing_writable) {}

  /// @brief Construct a store whose **default** level is the coarsest GGGS level
  ///        whose cells are no larger than @p cell_size_m (clamped to level 20).
  static BathymetryStore fromCellSize(float cell_size_m, bool pre_existing_writable = false)
  {
    return BathymetryStore(
      gggs::Level::fromCellSize(cell_size_m).level(), pre_existing_writable);
  }

  /// @brief Whether per-cell `set()` may write the read-only `PreExisting` layer.
  bool preExistingWritable() const noexcept {return pre_existing_writable_;}

  /// @brief The store's **default** level (used by `cellIndex(lat,lon)` only).
  ///
  /// Stored tiles are not pinned to this level — the store is multi-level
  /// (ADR-0002 §D2). This is the level the coordinate convenience resolves at.
  const gggs::Level & level() const noexcept {return level_;}

  /// @brief CellIndex at this store's **default** level for a geographic position.
  ///
  /// Convenience for callers who think in coordinates. The returned CellIndex is
  /// at the default level; callers wanting a specific level should build the
  /// CellIndex from a `gggs::Level` of their choosing (the store accepts any).
  gggs::CellIndex cellIndex(double latitude, double longitude) const
  {
    return level_.cellIndex(gggs::geoPoint(latitude, longitude));
  }

  /// @brief Write @p value into @p layer at @p cell.
  ///
  /// Creates the backing tile on first write. The cell may be at **any** valid
  /// GGGS level — the store is multi-level (ADR-0002 §D2). Within a layer the
  /// surface is **last-write-wins** per cell: a second write to the same cell
  /// overwrites the first (there is no per-day epoch ordering since #221).
  /// @return `true` always (no provenance guard rejects a write). The bool
  ///         return is retained for source/API stability with the prior epoch
  ///         model and possible future write gates.
  /// @throws std::invalid_argument if @p cell is invalid.
  /// @throws std::logic_error if @p layer is `PreExisting` and the store was not
  ///   constructed `pre_existing_writable` — the prior is read-only (ADR-0002 §D3).
  bool set(SourceLayer layer, const gggs::CellIndex & cell, const BathyCell & value);

  /// @brief Read the raw cell of one @p layer (no priority overlay).
  /// @return The cell, or `std::nullopt` if the cell's tile is absent.
  ///         A returned cell may still be no-data (`!hasData()`).
  std::optional<BathyCell> get(SourceLayer layer, const gggs::CellIndex & cell) const;

  /// @brief Bulk-insert @p tiles into @p layer (importer path).
  ///
  /// Merges the supplied tile map into the layer's single tile map: each grid's
  /// tile replaces any tile already resident at that grid, and grids not in
  /// @p tiles are left untouched (no wholesale clear — there is no epoch to
  /// supersede). Every inserted tile is marked dirty so it persists. The
  /// PreExisting read-only gate applies, identical to `set()`. Tiles may be at
  /// heterogeneous levels (multi-level, ADR-0002 §D2).
  ///
  /// @note Additive-merge footgun: because this never clears and `save()` never
  ///   deletes on-disk tiles, a *shrinking* re-import (a corrected coverage with
  ///   fewer grids than a prior import) does NOT remove the now-orphaned `.tif`
  ///   files — `load()` will resurrect them. This matches the deliberate
  ///   single-fused-grid merge contract (#221), but a true replace requires
  ///   clearing the on-disk layer dir first. A `--replace` import path is a
  ///   future addition if/when corrected re-imports become a workflow.
  /// @return The number of grids inserted/replaced.
  /// @throws std::invalid_argument if any grid key is invalid or a tile's own
  ///   GridIndex does not match its map key.
  /// @throws std::logic_error if @p layer is `PreExisting` and the store was not
  ///   constructed `pre_existing_writable`.
  std::size_t importTiles(
    SourceLayer layer, std::map<gggs::GridIndex, BathymetryTile> tiles);

  /// @brief A layer's tiles, keyed by `gggs::GridIndex` (ascending). Empty map
  ///        if the layer holds no data.
  const std::map<gggs::GridIndex, BathymetryTile> & tiles(SourceLayer layer) const
  {
    return layerMap(layer);
  }

private:
  // Persistence must reach getOrCreateTile to populate any layer (including the
  // read-only `PreExisting` prior) from disk and to clear dirty flags after a
  // save, so the free functions in tile_io.cpp are friends. With getOrCreateTile
  // private, this is what makes the PreExisting read-only guarantee hold by
  // construction, not just by convention: the only public per-cell mutator is
  // `set` (which gates `PreExisting`), and external code cannot obtain a mutable
  // tile. `loadWindow` needs the same private access as `load`; `evictOutside`
  // needs the non-const layerMap() to erase tiles without const_cast.
  friend std::size_t save(
    BathymetryStore & store, const std::string & dir, const StoreMetadata * metadata);
  friend std::size_t load(
    BathymetryStore & store, const std::string & dir, StoreMetadata * metadata);
  friend std::size_t loadWindow(
    BathymetryStore & store, const std::string & dir,
    const geographic_msgs::msg::GeoPoint & min_pt,
    const geographic_msgs::msg::GeoPoint & max_pt,
    StoreMetadata * metadata);
  friend std::size_t evictOutside(
    BathymetryStore & store,
    const geographic_msgs::msg::GeoPoint & min_pt,
    const geographic_msgs::msg::GeoPoint & max_pt);

  /// @brief Find or create the tile for @p grid in @p layer.
  ///
  /// Private: the only mutable tile access. Public mutation goes through `set`
  /// (which gates `PreExisting`) or `importTiles`; persistence reaches this via
  /// friendship. @p grid may be at any valid level (multi-level store, ADR-0002
  /// §D2).
  /// @throws std::invalid_argument if @p grid is invalid.
  BathymetryTile & getOrCreateTile(SourceLayer layer, const gggs::GridIndex & grid);

  std::map<gggs::GridIndex, BathymetryTile> & layerMap(SourceLayer layer)
  {
    return layers_[static_cast<std::size_t>(layer)];
  }
  const std::map<gggs::GridIndex, BathymetryTile> & layerMap(SourceLayer layer) const
  {
    return layers_[static_cast<std::size_t>(layer)];
  }

  gggs::Level level_;
  bool pre_existing_writable_ = false;
  std::array<std::map<gggs::GridIndex, BathymetryTile>, source_layer_count> layers_;
};

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__BATHYMETRY_STORE_HPP_
