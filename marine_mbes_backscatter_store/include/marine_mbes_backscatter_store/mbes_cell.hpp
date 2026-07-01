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
/// per-cell record. The pre-#248 `draft`/`processed` two-layer overlay collapsed
/// to a **single `survey` layer** (ADR-0007 #248 amendment A.2): with one platform
/// and one coverage, the live-vs-durable split added no query value.
enum class SourceLayer : uint8_t
{
  Survey = 0,  ///< The CUBE node-output product (live or off-boat re-run). The only layer.
};

/// @brief Source layers in descending priority order — iterate for best-source.
inline constexpr std::array<SourceLayer, 1> source_layers_by_priority{SourceLayer::Survey};

/// @brief Number of source layers present in this phase.
inline constexpr std::size_t source_layer_count = source_layers_by_priority.size();

/// @brief Per-cell MBES backscatter record (ADR-0007 D6, #248 amendment A.1).
///
/// A 3-band `Float32` value tile encoding the **Welford sufficient statistics**
/// so the estimate reconstructs losslessly on reload (rather than collapsing to a
/// mean+variance pair):
///
/// - `mean` — Welford running mean of corrected backscatter. NaN = no data.
/// - `standard_error` — **confidence-scaled** standard error of the mean (D4
///   estimate uncertainty), `scale · sample_sd / √n`. The scale mirrors the bathy
///   store's confidence-scaled uncertainty convention; the consumer divides it
///   out on reload before interpreting it as a true standard error.
/// - `sample_sd` — sample standard deviation of the contributing beams,
///   `√(M2 / (n−1))` — the D4 within-node dispersion / texture signal.
///
/// The store round-trips all three floats bit-exactly and does **no** scaling.
/// From them (dividing the confidence scale out of `standard_error` to recover
/// the true `SE = sample_sd / √n`), a downstream re-run recovers the full Welford
/// state: `n = (sample_sd / SE)²` and `M2 = sample_sd² · (n − 1)`.
///
/// **`n = 1` sentinel:** a single-beam node has no dispersion — `sample_sd == 0`
/// with a **finite** `mean` encodes `n = 1, M2 = 0`, distinct from **no-data**
/// (`mean = NaN`). `hasData()` keys on `mean`, so a one-sample cell is real data.
///
/// The pre-#248 per-cell `timestamp` (`_time.tif`) and `source_index`
/// (`_source.tif`) companions were dropped (#248 amendment A.3); each grid is a
/// single 3-band value tile. See `MbesTile` and `tile_io`.
struct MbesCell
{
  /// Welford running mean of corrected backscatter (relative). NaN = no data.
  float mean = std::numeric_limits<float>::quiet_NaN();
  /// Confidence-scaled standard error of the mean (D4 estimate uncertainty).
  float standard_error = std::numeric_limits<float>::quiet_NaN();
  /// Sample standard deviation of contributing beams (within-node dispersion).
  float sample_sd = std::numeric_limits<float>::quiet_NaN();

  /// @brief True if this cell carries a usable mean (mean is not NaN).
  bool hasData() const noexcept
  {
    return !std::isnan(mean);
  }
};

}  // namespace marine_mbes_backscatter_store

#endif  // MARINE_MBES_BACKSCATTER_STORE__MBES_CELL_HPP_
