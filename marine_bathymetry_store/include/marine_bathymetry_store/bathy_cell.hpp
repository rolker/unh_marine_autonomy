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

#ifndef MARINE_BATHYMETRY_STORE__BATHY_CELL_HPP_
#define MARINE_BATHYMETRY_STORE__BATHY_CELL_HPP_

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace marine_bathymetry_store
{

/// @brief Source layers, ordered by query priority (highest first).
///
/// A cell's source is implied by which layer holds it (the store keeps one
/// tile map per layer), so it is not stored in the per-cell record. This is a
/// deliberate, cleaner realization of ADR-0002 §D3's "each cell stores a
/// source layer": the layer is the map it lives in.
///
/// The numeric value is the priority rank (0 = highest). `Chart` (the broad,
/// coarse contour / S57-derived prior) is lowest priority: best-source falls
/// through to it where no Processed/Draft data exists. Chart cells are
/// ellipsoidal heights converted from chart datum **at import** (ADR-0002 §D7);
/// the store treats Chart as a **read-only prior** so live Draft/CUBE ingest can
/// never clobber it — see `BathymetryStore::set` and the `chart_writable`
/// construction flag the importer opts into.
enum class SourceLayer : uint8_t
{
  Processed = 0,  ///< Externally produced grids (bathy-BAG / GeoTIFF). Highest confidence.
  Draft = 1,      ///< Real-time CUBE output. Valuable but potentially noisy.
  Chart = 2,      ///< Contour / S57-derived prior. Broad, coarse, read-only.
};

/// @brief Source layers in descending priority order — iterate for best-source.
inline constexpr std::array<SourceLayer, 3> source_layers_by_priority{
  SourceLayer::Processed, SourceLayer::Draft, SourceLayer::Chart};

/// @brief Number of source layers present in this phase.
inline constexpr std::size_t source_layer_count = source_layers_by_priority.size();

/// @brief Per-cell bathymetric record.
///
/// `depth` and `uncertainty` are `double` (a 2-band `Float64` value tile); the
/// **timestamp is `int64_t` nanoseconds since the Unix epoch** (ROS-native:
/// `rclcpp::Time` is int64 ns), persisted as a separate 1-band `Int64` tile so
/// it keeps nanosecond exactness (a `Float64` seconds band resolved absolute
/// 2026 stamps to only ~0.4 µs). The `source_index` is a small interning handle
/// into the store-wide `registry.json` (ADR-0005 D2/D8): which platform/sensor
/// contributed this cell. 0 = no-data/unset (ADR-0005 D4 sentinel) and is the
/// default for pre-existing single-platform data. The on-disk layout is three
/// tiles per grid — value (`<grid>.tif`), time (`<grid>_time.tif`), and source
/// (`<grid>_source.tif`); see `BathymetryTile` and `tile_io`.
struct BathyCell
{
  /// Ellipsoidal height (WGS84), metres. NaN = no data.
  double depth = std::numeric_limits<double>::quiet_NaN();
  /// 1-sigma vertical uncertainty, metres. NaN = unknown.
  double uncertainty = std::numeric_limits<double>::quiet_NaN();
  /// Acquisition / import time, nanoseconds since the Unix epoch. 0 = unset.
  int64_t timestamp = 0;
  /// Local source index into the store-wide registry. 0 = no-data/unset.
  uint16_t source_index = 0;

  /// @brief True if this cell carries a usable depth (depth is not NaN).
  ///
  /// Data presence is **depth-only**: `source_index` is provenance, not a
  /// data-presence signal, so a cell with a registered source but NaN depth is
  /// still no-data.
  bool hasData() const noexcept
  {
    return !std::isnan(depth);
  }
};

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__BATHY_CELL_HPP_
