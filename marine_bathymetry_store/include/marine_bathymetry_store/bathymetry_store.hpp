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
#include <map>
#include <optional>

#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathy_cell.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"

namespace marine_bathymetry_store
{

/// @brief In-memory, GGGS-tiled, multi-layer bathymetric store (Phase 1 core).
///
/// Holds one tile map per `SourceLayer`, keyed by `gggs::GridIndex`. All tiles
/// live at a single GGGS level fixed at construction. Source priority is a
/// **non-destructive query-time overlay** (see `query.hpp`): the layers are
/// independent, so a noisy draft write never clobbers a trusted processed cell
/// and a later processed import never has to merge with draft (ADR-0002 §D3).
///
/// This phase has no importers, ROS interface, or distribution — just storage,
/// in-process queries (`query.hpp`), and per-tile GeoTIFF persistence
/// (`tile_io.hpp`).
class BathymetryStore
{
public:
  /// @brief Construct a store whose tiles live at GGGS quadtree @p gggs_level.
  /// @throws std::out_of_range if the level is invalid (via gggs::Level).
  explicit BathymetryStore(uint8_t gggs_level)
  : level_(gggs_level) {}

  /// @brief Construct a store at the finest GGGS level with cells ≥ @p cell_size_m.
  static BathymetryStore fromCellSize(float cell_size_m)
  {
    return BathymetryStore(gggs::Level::fromCellSize(cell_size_m).level());
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

  /// @brief Write @p value into @p layer at @p cell.
  ///
  /// Creates the backing tile on first write to its grid. The cell's grid must
  /// be at this store's level.
  /// @throws std::invalid_argument if @p cell is invalid or at the wrong level.
  void set(SourceLayer layer, const gggs::CellIndex & cell, const BathyCell & value);

  /// @brief Read the raw cell in a single @p layer (no priority overlay).
  /// @return The cell, or `std::nullopt` if @p layer has no tile for that grid.
  ///         A returned cell may still be no-data (`!hasData()`).
  std::optional<BathyCell> get(SourceLayer layer, const gggs::CellIndex & cell) const;

  /// @brief The tiles of a layer (for persistence / iteration).
  const std::map<gggs::GridIndex, BathymetryTile> & tiles(SourceLayer layer) const
  {
    return layerMap(layer);
  }

  /// @brief Find or create the tile for @p grid in @p layer (used by load).
  /// @throws std::invalid_argument if @p grid is invalid or at the wrong level.
  BathymetryTile & getOrCreateTile(SourceLayer layer, const gggs::GridIndex & grid);

private:
  std::map<gggs::GridIndex, BathymetryTile> & layerMap(SourceLayer layer)
  {
    return layers_[static_cast<std::size_t>(layer)];
  }
  const std::map<gggs::GridIndex, BathymetryTile> & layerMap(SourceLayer layer) const
  {
    return layers_[static_cast<std::size_t>(layer)];
  }

  gggs::Level level_;
  std::array<std::map<gggs::GridIndex, BathymetryTile>, source_layer_count> layers_;
};

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__BATHYMETRY_STORE_HPP_
