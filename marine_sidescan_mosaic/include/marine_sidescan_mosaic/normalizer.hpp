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

#ifndef MARINE_SIDESCAN_MOSAIC__NORMALIZER_HPP_
#define MARINE_SIDESCAN_MOSAIC__NORMALIZER_HPP_

#include <cstdint>
#include <vector>

namespace marine_sidescan_mosaic
{

/// @brief Tuning for the rolling display normalizer.
struct NormalizerConfig
{
  /// The reference intensity maps to this uint16 level (mid-range leaves
  /// headroom for bright targets).
  double target_level = 32768.0;
  /// Per-ping reference statistic: a high-ish quantile = "typical strong
  /// return", robust to the dark nadir zone.
  double percentile = 0.85;
  /// Rolling adaptation rate (per ping) for the reference; smaller = steadier.
  double ema_alpha = 0.05;
  /// Floor on the reference so an all-dark ping can't blow up or divide by zero.
  double min_reference = 1.0;
};

/// @brief Rolling automatic-gain display normalizer (P2).
///
/// Maps raw sidescan sample magnitudes to a `uint16` display range so the live
/// mosaic stays legible as bottom conditions change. Per ping it takes a
/// high-percentile reference, smooths it across pings (EMA), and scales the
/// samples so that reference lands at `target_level`; brighter samples scale up
/// (clamped at 65535), darker down. No per-survey statistics — this is the live
/// stand-in for PINGMapper's whole-survey EGN (the quality product stays offline).
class RollingNormalizer
{
public:
  explicit RollingNormalizer(NormalizerConfig config = {});

  /// @brief Map one ping's raw samples to `uint16`, updating the rolling
  ///   reference from this ping. @p out is resized to @p raw.size().
  void normalize(const std::vector<double> & raw, std::vector<std::uint16_t> & out);

  /// @brief The current rolling reference level (negative before the first ping).
  double reference() const noexcept {return reference_;}

private:
  NormalizerConfig config_;
  double reference_ = -1.0;   ///< < 0 until the first ping initializes it.
};

}  // namespace marine_sidescan_mosaic

#endif  // MARINE_SIDESCAN_MOSAIC__NORMALIZER_HPP_
