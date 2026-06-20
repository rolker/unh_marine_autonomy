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

#include "marine_sidescan_mosaic/normalizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace marine_sidescan_mosaic
{

namespace
{
// The p-quantile of @p values (0..1), by partial sort. Takes a copy (mutates).
double percentileOf(std::vector<double> values, double p)
{
  if (values.empty()) {
    return 0.0;
  }
  p = std::clamp(p, 0.0, 1.0);
  const std::size_t k =
    static_cast<std::size_t>(p * static_cast<double>(values.size() - 1));
  std::nth_element(values.begin(), values.begin() + k, values.end());
  return values[k];
}
}  // namespace

RollingNormalizer::RollingNormalizer(NormalizerConfig config)
: config_(config) {}

void RollingNormalizer::normalize(
  const std::vector<double> & raw, std::vector<std::uint16_t> & out)
{
  out.resize(raw.size());
  if (raw.empty()) {
    return;
  }

  // This ping's reference, floored so an all-dark ping can't blow up the gain.
  double ping_ref = std::max(percentileOf(raw, config_.percentile), config_.min_reference);

  // Roll it into the running reference (first ping initializes directly).
  if (reference_ < 0.0) {
    reference_ = ping_ref;
  } else {
    reference_ = config_.ema_alpha * ping_ref + (1.0 - config_.ema_alpha) * reference_;
  }
  reference_ = std::max(reference_, config_.min_reference);

  const double scale = config_.target_level / reference_;
  for (std::size_t i = 0; i < raw.size(); ++i) {
    const double scaled = std::round(raw[i] * scale);
    // Floor at 1: 0 is reserved as the tile no-data value (untouched cells), so
    // even the darkest real return maps to "covered", not "no data".
    const double clamped = std::clamp(scaled, 1.0, 65535.0);
    out[i] = static_cast<std::uint16_t>(clamped);
  }
}

}  // namespace marine_sidescan_mosaic
