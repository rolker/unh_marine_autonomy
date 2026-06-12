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
#include <utility>

#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathy_cell.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"
#include "marine_bathymetry_store/epoch.hpp"

namespace marine_bathymetry_store
{

/// @brief One epoch's tile set within a layer, with its provenance.
///
/// `supersedes_disk` is set by a wholesale import (`BathymetryStore::importEpoch`)
/// and tells persistence that any files previously written for this epoch are
/// stale and must be removed before saving — a compacted epoch may legitimately
/// cover fewer grids than the live surface it replaces, and a leftover tile
/// file would otherwise be silently resurrected on the next load.
struct EpochTiles
{
  Provenance provenance = Provenance::LiveFused;
  bool supersedes_disk = false;
  std::map<gggs::GridIndex, BathymetryTile> tiles;
};

/// @brief In-memory, GGGS-tiled, multi-layer, multi-epoch bathymetric store.
///
/// Holds, per `SourceLayer`, a map of **epochs** (dated layer instances,
/// ADR-0002 Amendment A1) each holding a tile map keyed by `gggs::GridIndex`.
/// All tiles live at a single GGGS level fixed at construction. Source priority
/// is a **non-destructive query-time overlay** (see `query.hpp`), and epochs
/// are never fused across days — queries resolve a layer by walking its epochs
/// newest-first (epoch labels sort chronologically; see `Epoch`).
///
/// This phase has no ROS interface or distribution — storage, in-process
/// queries (`query.hpp`), and per-tile GeoTIFF persistence (`tile_io.hpp`).
class BathymetryStore
{
public:
  /// @brief Construct a store whose tiles live at GGGS quadtree @p gggs_level.
  /// @throws std::out_of_range if the level is invalid (via gggs::Level).
  explicit BathymetryStore(uint8_t gggs_level)
  : level_(gggs_level) {}

  /// @brief Construct a store at the coarsest GGGS level whose cells are no
  ///        larger than @p cell_size_m (clamped to the finest level, 20).
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

  /// @brief Write @p value into @p layer's @p epoch at @p cell (live path).
  ///
  /// Creates the epoch (as `LiveFused`) and the backing tile on first write.
  /// The cell's grid must be at this store's level.
  /// @return `false` (a no-op) if the epoch is already `Replayed`: a compacted
  ///         epoch is immutable, and a live write must never regress it
  ///         (ADR-0002 §A1.2 provenance ordering). The caller should log this —
  ///         it means a live snapshot arrived after the day was compacted.
  /// @throws std::invalid_argument if @p cell is invalid or at the wrong level,
  ///         or @p epoch is not a valid label (`validateEpochLabel`).
  bool set(
    SourceLayer layer, const Epoch & epoch, const gggs::CellIndex & cell,
    const BathyCell & value);

  /// @brief Read the raw cell of one @p epoch in one @p layer (no overlay).
  /// @return The cell, or `std::nullopt` if the epoch or its tile is absent.
  ///         A returned cell may still be no-data (`!hasData()`).
  std::optional<BathyCell> get(
    SourceLayer layer, const Epoch & epoch, const gggs::CellIndex & cell) const;

  /// @brief Replace @p layer's @p epoch wholesale with @p tiles (import path).
  ///
  /// Used both for compaction products (@p provenance = `Replayed`: a full-day
  /// replay superseding the live surface) and for whole-epoch imports such as
  /// processed GeoTIFFs. All imported tiles are marked dirty and the epoch is
  /// flagged `supersedes_disk` so persistence removes any stale files first.
  /// @return `false` (a no-op) if the existing epoch is `Replayed` and
  ///         @p provenance is `LiveFused` — the §A1.2 ordering: live-fused
  ///         never replaces replayed. Re-importing at equal-or-higher
  ///         provenance (e.g. a re-compaction) is allowed.
  /// @throws std::invalid_argument if @p epoch is not a valid label, or any
  ///         tile is keyed at the wrong level.
  bool importEpoch(
    SourceLayer layer, const Epoch & epoch,
    std::map<gggs::GridIndex, BathymetryTile> tiles, Provenance provenance);

  /// @brief A layer's epochs, ascending by label (oldest first; `rbegin()` =
  ///        newest). Empty map if the layer holds no data.
  const std::map<Epoch, EpochTiles> & epochs(SourceLayer layer) const
  {
    return layerMap(layer);
  }

  /// @brief Find or create @p epoch in @p layer with @p provenance (load path).
  ///
  /// If the epoch already exists its provenance is left unchanged — creation
  /// is the only time the argument applies.
  /// @throws std::invalid_argument if @p epoch is not a valid label.
  EpochTiles & getOrCreateEpoch(
    SourceLayer layer, const Epoch & epoch, Provenance provenance);

  /// @brief Find or create the tile for @p grid in @p layer's @p epoch.
  ///
  /// Creates the epoch (as `LiveFused`) if absent — used by load and by
  /// persistence to clear dirty flags on existing tiles.
  /// @throws std::invalid_argument if @p grid is invalid or at the wrong
  ///         level, or @p epoch is not a valid label.
  BathymetryTile & getOrCreateTile(
    SourceLayer layer, const Epoch & epoch, const gggs::GridIndex & grid);

private:
  std::map<Epoch, EpochTiles> & layerMap(SourceLayer layer)
  {
    return layers_[static_cast<std::size_t>(layer)];
  }
  const std::map<Epoch, EpochTiles> & layerMap(SourceLayer layer) const
  {
    return layers_[static_cast<std::size_t>(layer)];
  }

  /// @brief Validate a GridIndex against this store's level (shared guard).
  void requireGridAtLevel(const gggs::GridIndex & grid, const char * who) const;

  gggs::Level level_;
  std::array<std::map<Epoch, EpochTiles>, source_layer_count> layers_;
};

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__BATHYMETRY_STORE_HPP_
