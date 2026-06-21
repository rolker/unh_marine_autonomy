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

#ifndef MARINE_MBES_BACKSCATTER_STORE__MBES_STORE_HPP_
#define MARINE_MBES_BACKSCATTER_STORE__MBES_STORE_HPP_

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <string>

#include "marine_autonomy/gggs.h"
#include "marine_mbes_backscatter_store/mbes_cell.hpp"
#include "marine_mbes_backscatter_store/mbes_tile.hpp"

namespace marine_mbes_backscatter_store
{

class MbesBackscatterStore;
class SourceRegistry;
// Persistence free functions (defined in tile_io.cpp). Forward-declared here so
// the store can friend them: `load` must populate any layer from disk, which the
// public API otherwise routes only through set().
std::size_t save(
  MbesBackscatterStore & store, const std::string & dir, const SourceRegistry * registry);
std::size_t load(
  MbesBackscatterStore & store, const std::string & dir, SourceRegistry * registry);

/// @brief In-memory, GGGS-tiled, two-layer MBES backscatter store (ADR-0007 phase 3).
///
/// Holds one tile map per `SourceLayer` (`Draft`, `Processed`), keyed by
/// `gggs::GridIndex`. All tiles live at a single GGGS level fixed at
/// construction. Source priority is a **non-destructive query-time overlay**
/// (see `query.hpp`): the layers are independent, so a live draft write never
/// clobbers a durable processed cell and a later processed build never has to
/// merge with draft (ADR-0007 D7, mirroring ADR-0002 §D3).
///
/// **Draft recency policy (ADR-0007 D7):** `set(Draft, …)` is *newest-valid-wins*
/// — it overwrites the existing cell unconditionally. Callers supply
/// angle-corrected node-output values (ADR-0007 D2/D3); the store does **no**
/// accumulation. Welford accumulation lives upstream in the CUBE node-output
/// pass (cube#54), not here.
///
/// This phase has no importers, ROS interface, or distribution — just storage,
/// in-process queries (`query.hpp`), per-tile GeoTIFF persistence
/// (`tile_io.hpp`), and the source registry (`registry.hpp`).
class MbesBackscatterStore
{
public:
  /// @brief Construct a store whose tiles live at GGGS quadtree @p gggs_level.
  /// @throws std::out_of_range if the level is invalid (via gggs::Level).
  explicit MbesBackscatterStore(uint8_t gggs_level)
  : level_(gggs_level) {}

  /// @brief Construct a store at the coarsest GGGS level whose cells are no
  ///        larger than @p cell_size_m (clamped to the finest level, 20).
  static MbesBackscatterStore fromCellSize(float cell_size_m)
  {
    return MbesBackscatterStore(gggs::Level::fromCellSize(cell_size_m).level());
  }

  /// @brief The GGGS level all tiles in this store use.
  const gggs::Level & level() const noexcept {return level_;}

  /// @brief CellIndex at this store's level for a geographic position.
  ///
  /// Convenience for callers who think in coordinates — guarantees the returned
  /// CellIndex is at the store's level (so `set`/`get` won't be rejected).
  gggs::CellIndex cellIndex(double latitude, double longitude) const
  {
    return level_.cellIndex(gggs::geoPoint(latitude, longitude));
  }

  /// @brief Write @p value into @p layer at @p cell (newest-valid-wins overwrite).
  ///
  /// Creates the backing tile on first write to its grid. The cell's grid must
  /// be at this store's level. On `Draft`, an existing cell is overwritten
  /// unconditionally (ADR-0007 D7 recency policy) — there is no accumulation.
  /// @throws std::invalid_argument if @p cell is invalid or at the wrong level.
  void set(SourceLayer layer, const gggs::CellIndex & cell, const MbesCell & value);

  /// @brief Read the raw cell in a single @p layer (no priority overlay).
  /// @return The cell, or `std::nullopt` if @p layer has no tile for that grid.
  ///         A returned cell may still be no-data (`!hasData()`).
  std::optional<MbesCell> get(SourceLayer layer, const gggs::CellIndex & cell) const;

  /// @brief The tiles of a layer (for persistence / iteration).
  const std::map<gggs::GridIndex, MbesTile> & tiles(SourceLayer layer) const
  {
    return layerMap(layer);
  }

private:
  // Persistence needs to populate any layer from disk, so the free functions in
  // tile_io.cpp are friends.
  friend std::size_t save(
    MbesBackscatterStore & store, const std::string & dir, const SourceRegistry * registry);
  friend std::size_t load(
    MbesBackscatterStore & store, const std::string & dir, SourceRegistry * registry);

  /// @brief Find or create the tile for @p grid in @p layer.
  ///
  /// Private: the only mutable tile access. Public mutation goes through `set`;
  /// persistence reaches this via friendship.
  /// @throws std::invalid_argument if @p grid is invalid or at the wrong level.
  MbesTile & getOrCreateTile(SourceLayer layer, const gggs::GridIndex & grid);

  std::map<gggs::GridIndex, MbesTile> & layerMap(SourceLayer layer)
  {
    return layers_[static_cast<std::size_t>(layer)];
  }
  const std::map<gggs::GridIndex, MbesTile> & layerMap(SourceLayer layer) const
  {
    return layers_[static_cast<std::size_t>(layer)];
  }

  gggs::Level level_;
  std::array<std::map<gggs::GridIndex, MbesTile>, source_layer_count> layers_;
};

}  // namespace marine_mbes_backscatter_store

#endif  // MARINE_MBES_BACKSCATTER_STORE__MBES_STORE_HPP_
