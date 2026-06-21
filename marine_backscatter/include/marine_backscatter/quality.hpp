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

#ifndef MARINE_BACKSCATTER__QUALITY_HPP_
#define MARINE_BACKSCATTER__QUALITY_HPP_

#include <cstdint>

/// @file
/// @brief Per-sample best-source quality for the processed backscatter layer.

namespace marine_backscatter
{

/// @brief Mid-swath grazing-angle quality in [1, 65535] from a sample's altitude
///   above the bottom and its horizontal ground range. Peaks at 45° grazing,
///   ~0 at nadir and far range (ADR-0006 D5), so a neighbouring line's mid-swath
///   look wins and the nadir gap fills. Floored to 1 so a real return is never
///   mistaken for the no-data 0.
std::uint16_t grazingQuality(double altitude, double ground_range);

}  // namespace marine_backscatter

#endif  // MARINE_BACKSCATTER__QUALITY_HPP_
