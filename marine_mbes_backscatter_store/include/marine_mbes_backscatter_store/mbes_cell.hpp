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

#ifndef MARINE_MBES_BACKSCATTER_STORE__MBES_CELL_HPP_
#define MARINE_MBES_BACKSCATTER_STORE__MBES_CELL_HPP_

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace marine_mbes_backscatter_store
{

/// @brief Source layers, ordered by query priority (highest first).
///
/// As in the bathy store, a cell's source layer is implied by which layer holds
/// it (the store keeps one tile map per layer), so it is not stored in the
/// per-cell record. There is **no Chart layer** here: a contour / S57 prior is a
/// bathymetric concept; MBES backscatter has no chart prior (ADR-0007 D7). The
/// two layers are `Processed` (the durable, quality-arbitrated re-run) and
/// `Draft` (the live, newest-valid-wins survey view).
///
/// The numeric value is the priority rank (0 = highest): best-source prefers
/// `Processed` and falls through to `Draft`.
enum class SourceLayer : uint8_t
{
  Processed = 0,  ///< Durable deferred-settled re-run product (ADR-0007 D7). Highest.
  Draft = 1,      ///< Live CUBE node-output, newest-valid-wins (ADR-0007 D7).
};

/// @brief Source layers in descending priority order — iterate for best-source.
inline constexpr std::array<SourceLayer, 2> source_layers_by_priority{
  SourceLayer::Processed, SourceLayer::Draft};

/// @brief Number of source layers present in this phase.
inline constexpr std::size_t source_layer_count = source_layers_by_priority.size();

/// @brief Per-cell MBES backscatter record (ADR-0007 D4/D6).
///
/// `intensity` and `intensity_variance` are `float` (a 2-band `Float32` value
/// tile): the M3 is AGC/relative, so single-precision is ample, and the variance
/// **is** the quality/uncertainty signal (ADR-0007 D6 — there is no separate
/// integer quality band as in the sidescan store). `intensity_variance` is the
/// Welford estimate variance (D4), shrinking with sample count, mirroring the
/// bathy store's depth uncertainty. The `timestamp` is `int64_t` nanoseconds
/// since the Unix epoch (ROS-native, exact), persisted as a separate 1-band
/// `Int64` tile. The `source_index` is a small interning handle into the
/// store-wide `registry.json` (ADR-0005 D2/D8); 0 = no-data/unset. The on-disk
/// layout is three tiles per grid — value (`<grid>.tif`), time
/// (`<grid>_time.tif`), and source (`<grid>_source.tif`); see `MbesTile` and
/// `tile_io`.
struct MbesCell
{
  /// Corrected backscatter intensity (relative, dB-domain). NaN = no data.
  float intensity = std::numeric_limits<float>::quiet_NaN();
  /// Welford estimate variance of the intensity. NaN = unknown.
  float intensity_variance = std::numeric_limits<float>::quiet_NaN();
  /// Acquisition / import time, nanoseconds since the Unix epoch. 0 = unset.
  int64_t timestamp = 0;
  /// Local source index into the store-wide registry. 0 = no-data/unset.
  uint16_t source_index = 0;

  /// @brief True if this cell carries a usable intensity (intensity is not NaN).
  ///
  /// Data presence is **intensity-only**: `source_index` is provenance, not a
  /// data-presence signal, so a cell with a registered source but NaN intensity
  /// is still no-data.
  bool hasData() const noexcept
  {
    return !std::isnan(intensity);
  }
};

}  // namespace marine_mbes_backscatter_store

#endif  // MARINE_MBES_BACKSCATTER_STORE__MBES_CELL_HPP_
