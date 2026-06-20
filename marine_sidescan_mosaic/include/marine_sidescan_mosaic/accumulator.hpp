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

#ifndef MARINE_SIDESCAN_MOSAIC__ACCUMULATOR_HPP_
#define MARINE_SIDESCAN_MOSAIC__ACCUMULATOR_HPP_

#include <cstdint>
#include <map>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_tiled_raster_store/tiled_raster_tile.hpp"

namespace marine_sidescan_mosaic
{

/// @brief How multiple samples landing in one cell are combined.
enum class SplatPolicy
{
  Mean,      ///< Mean of the samples (box-filter downsample of oversampled data).
  MaxHold,   ///< Brightest sample (target-cueing).
  Newest     ///< Newest-valid sample wins (recency; live operator `draft` layer).
};

/// @brief Accumulates georeferenced sample intensities into GGGS-tiled `uint16`
///   mosaic tiles, ready for `marine_tiled_raster_store::saveTiles`.
///
/// Cells are oversampled (~5–15 samples/cell at L13). `Mean` keeps, per cell, a
/// `uint64` sum + `uint32` count (the sum can't overflow even over a long
/// stationary dwell), and the live `uint16` output cell is `sum/count`.
/// `MaxHold` keeps the brightest sample and needs no side state. `Newest`
/// (newest-valid-wins) is the live operator `draft`-layer policy: every real
/// ping overwrites the cell so the operator sees the sonar actively painting,
/// rather than a new pass being visually rejected by a quality/mean arbiter.
/// The output tiles carry the dirty flag (`set` marks dirty), so the node flushes
/// only what changed. A cell value of 0 is "no data" (un-touched).
class MosaicAccumulator
{
public:
  using Tile = marine_tiled_raster_store::TiledRasterTile<std::uint16_t>;

  MosaicAccumulator(gggs::Level level, SplatPolicy policy);

  /// @brief Fold one sample @p value into @p cell (ignored if @p cell is invalid).
  ///
  /// For `Newest`, a `value == 0` sample is treated as no-data and does NOT
  /// overwrite existing coverage; real returns (guaranteed ≥ 1 by the
  /// normalizer's `clamp(scaled, 1.0, 65535.0)` floor) always overwrite. The
  /// zero-guard keeps a dropout from punching holes in already-painted tiles.
  void add(const gggs::CellIndex & cell, std::uint16_t value);

  /// @brief The live mosaic tiles (mutable so `saveTiles` can clear dirty flags).
  std::map<gggs::GridIndex, Tile> & tiles() {return output_;}
  const std::map<gggs::GridIndex, Tile> & tiles() const {return output_;}

  const gggs::Level & level() const noexcept {return level_;}
  SplatPolicy policy() const noexcept {return policy_;}

private:
  struct MeanGrid
  {
    std::vector<std::uint64_t> sum;
    std::vector<std::uint32_t> count;
  };

  Tile & ensureOutput(const gggs::GridIndex & grid);
  MeanGrid & ensureMean(const gggs::GridIndex & grid);

  gggs::Level level_;
  SplatPolicy policy_;
  std::map<gggs::GridIndex, Tile> output_;
  std::map<gggs::GridIndex, MeanGrid> mean_;   // only populated for SplatPolicy::Mean
};

}  // namespace marine_sidescan_mosaic

#endif  // MARINE_SIDESCAN_MOSAIC__ACCUMULATOR_HPP_
