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

#ifndef MARINE_BACKSCATTER__PROCESSED_ACCUMULATOR_HPP_
#define MARINE_BACKSCATTER__PROCESSED_ACCUMULATOR_HPP_

#include <cstdint>
#include <map>

#include "marine_autonomy/gggs.h"
#include "marine_tiled_raster_store/tiled_raster_tile.hpp"

/// @file
/// @brief Best-source compositor for the durable `processed` **sidescan**
///   backscatter layer (ADR-0006 D5/D7). The MBES layer composites differently
///   (Welford mean+variance, ADR-0007 D2) and does not use this — only the
///   registry + quality helpers in this package are cross-layer shared.
///
/// Per GGGS cell it keeps the sample with the highest **quality** and stores that
/// sample's `{intensity, quality, source-id}` — the 3-band `uint16` tile of
/// ADR-0006 D7. Mean stays a *within-pass* concern in each ingest; this
/// composites *across* passes by quality, so a poorer look never overwrites a
/// better one. The `source-id` band is the compact per-store local index that
/// ADR-0005's registry resolves to the global source.

namespace marine_backscatter
{

class ProcessedAccumulator
{
public:
  using Tile = marine_tiled_raster_store::TiledRasterTile<std::uint16_t>;

  /// Band layout of the processed tile (ADR-0006 D7).
  static constexpr std::size_t kIntensity = 0;
  static constexpr std::size_t kQuality = 1;
  static constexpr std::size_t kSource = 2;
  static constexpr std::size_t kBands = 3;

  explicit ProcessedAccumulator(gggs::Level level)
  : level_(level) {}

  /// @brief Fold one sample into @p cell. Replaces the cell **iff** @p quality
  ///   strictly exceeds the cell's stored quality (best-source). A cell starts at
  ///   quality 0 (no-data), so any @p quality > 0 places the first sample; pass
  ///   `quality >= 1` for a real return so it is not mistaken for no-data.
  void add(
    const gggs::CellIndex & cell,
    std::uint16_t intensity, std::uint16_t quality, std::uint16_t source_id);

  /// @brief The composited tiles (mutable so `saveTiles` can clear dirty flags).
  std::map<gggs::GridIndex, Tile> & tiles() {return output_;}
  const std::map<gggs::GridIndex, Tile> & tiles() const {return output_;}

  const gggs::Level & level() const noexcept {return level_;}

private:
  Tile & ensureOutput(const gggs::GridIndex & grid);

  gggs::Level level_;
  std::map<gggs::GridIndex, Tile> output_;
};

}  // namespace marine_backscatter

#endif  // MARINE_BACKSCATTER__PROCESSED_ACCUMULATOR_HPP_
