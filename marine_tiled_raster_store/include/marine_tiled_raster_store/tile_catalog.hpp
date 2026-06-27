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

#ifndef MARINE_TILED_RASTER_STORE__TILE_CATALOG_HPP_
#define MARINE_TILED_RASTER_STORE__TILE_CATALOG_HPP_

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "marine_autonomy/gggs.h"

namespace marine_tiled_raster_store
{

/// @file
/// @brief Payload-agnostic anti-entropy tile-sync: the catalog/reconcile logic
///        a source uses to advertise its tiles and a consumer uses to converge
///        its cache to exactly the source's set. ROS-free and tile-content-free
///        — it reasons over tile **indices + versions only**, so it serves both
///        the light display-tile transport (ADR-0008) and a future full-tile
///        store-to-store sync. The node boundary adapts the `marine_interfaces`
///        wire messages (`TileCatalog` / `TileRequest` / `SonarVisualizationTile`)
///        to/from these types.
///
/// @note **Not thread-safe.** Builder/reconciler hold plain `std::map` state; a
///   node touching one from multiple threads (e.g. a subscription callback and a
///   timer) must serialize access externally.
/// @note **Single monotonic source clock.** Versions and `generation_time` must
///   come from one monotonically-increasing source (the boat). The prune gate
///   compares held versions against a catalog's `generation_time`, so cross-clock
///   skew can over-/under-prune. Invalid `gggs::GridIndex` values are ignored at
///   ingestion (they are not real tiles).

/// @brief A per-tile version stamp — monotonic per source, newest-wins.
///
/// Nanoseconds-since-epoch is the natural carrier (cells already carry
/// timestamps; ADR-0008 D3), but any source-monotonic integer works. Kept a
/// plain integer so this logic stays ROS-free: the node boundary flattens
/// `builtin_interfaces/Time` to/from this.
using TileVersion = std::int64_t;

/// @brief One catalog entry: a tile's GGGS index plus its current version.
struct TileCatalogEntry
{
  gggs::GridIndex index;
  TileVersion version{0};
};

/// @brief A **complete** snapshot of every tile a source holds, plus the time
///        it was generated.
///
/// Completeness is a contract: prune-on-absence is only valid against a full
/// snapshot (ADR-0008 D4a). `generation_time` is the prune gate (D4b) — a held
/// tile absent from the catalog is pruned only if older than this time. It must
/// be at least as new as every entry's version (the source stamps "now"); a
/// `generation_time` of 0 (e.g. an un-stamped or sim-time-0 catalog) disables
/// pruning entirely, since no held version can be strictly older than 0.
struct TileCatalog
{
  TileVersion generation_time{0};
  std::vector<TileCatalogEntry> entries;
};

/// @brief The two action lists a reconcile produces.
struct ReconcileResult
{
  /// Missing or stale on the consumer. Bare indices (no version) — this mirrors
  /// the `marine_interfaces/TileRequest` wire shape, and the requested version
  /// is unnecessary: the delivered `SonarVisualizationTile` self-describes its
  /// version, which the consumer passes to `markHave`.
  std::vector<gggs::GridIndex> to_request;
  /// Held but absent from the catalog and older than its generation_time (gated).
  std::vector<gggs::GridIndex> to_prune;
};

/// @brief Source-side (boat) authoritative tile→version registry that emits
///        complete catalog snapshots. ROS-free.
class TileCatalogBuilder
{
public:
  /// @brief Record (or bump) a tile's version. Newest-wins: an older version is
  ///        ignored, so an out-of-order update cannot roll a tile back.
  void update(const gggs::GridIndex & index, TileVersion version);

  /// @brief Forget a tile (it left the source). It is then absent from future
  ///        catalogs, which drives prune-on-absence downstream.
  void remove(const gggs::GridIndex & index);

  /// @brief The current version of @p index, or std::nullopt if unknown.
  std::optional<TileVersion> versionOf(const gggs::GridIndex & index) const;

  /// @brief Number of tiles currently held.
  std::size_t size() const noexcept {return versions_.size();}

  /// @brief Build a **complete** catalog snapshot stamped @p generation_time.
  ///        Pass a time at least as new as every held tile's version.
  TileCatalog buildCatalog(TileVersion generation_time) const;

private:
  std::map<gggs::GridIndex, TileVersion> versions_;
};

/// @brief Consumer-side (operator) anti-entropy reconciler.
///
/// Holds what the consumer currently has and at what version, and computes —
/// against a received complete catalog — which tiles to **request** (missing or
/// stale) and which to **prune** (held but absent, timestamp-gated).
///
/// `reconcile()` is **pure**: it does not mutate state. The consumer acts on the
/// returned lists (sends requests, deletes pruned tiles) and reports completion
/// back via `markHave()` / `drop()`, modelling the real async flow. Repeated
/// reconciles converge the cache to exactly the catalog set under loss/reorder.
class TileCatalogReconciler
{
public:
  /// @brief Record that the consumer now holds @p index at @p version (e.g. a
  ///        tile was received and applied). Newest-wins: an older version is
  ///        ignored, so a reordered stale arrival cannot lower the held version.
  ///        Calling this after `drop()` re-adds the tile — intentional, so a
  ///        re-received tile returns. An invalid @p index is ignored. The node
  ///        author bounds cache growth (e.g. evict by area/recency).
  void markHave(const gggs::GridIndex & index, TileVersion version);

  /// @brief Forget @p index (the consumer deleted it, e.g. after a prune).
  void drop(const gggs::GridIndex & index);

  /// @brief Whether the consumer currently holds @p index.
  bool has(const gggs::GridIndex & index) const;

  /// @brief The held version of @p index, or std::nullopt if absent.
  std::optional<TileVersion> versionOf(const gggs::GridIndex & index) const;

  /// @brief Number of tiles currently cached.
  std::size_t size() const noexcept {return cache_.size();}

  /// @brief Compute the request + prune lists against a **complete** @p catalog.
  ///
  /// Request: every catalog tile the consumer is missing or holds at an older
  /// version. Prune: every held tile **absent** from the catalog whose held
  /// version predates @p catalog.generation_time — the timestamp gate
  /// (ADR-0008 D4b) that stops a late/reordered catalog from deleting a
  /// just-pushed fresh tile. Pure: does not mutate this reconciler.
  ReconcileResult reconcile(const TileCatalog & catalog) const;

private:
  std::map<gggs::GridIndex, TileVersion> cache_;
};

}  // namespace marine_tiled_raster_store

#endif  // MARINE_TILED_RASTER_STORE__TILE_CATALOG_HPP_
