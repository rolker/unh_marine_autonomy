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

#ifndef MARINE_SIDESCAN_MOSAIC__DECODE_HPP_
#define MARINE_SIDESCAN_MOSAIC__DECODE_HPP_

#include <vector>

#include "marine_acoustic_msgs/msg/raw_sonar_image.hpp"

/// @file
/// @brief Shared RawSonarImage payload decode (the live node and the offline
///   importer use one implementation — issue #184 engine extraction).

namespace marine_sidescan_mosaic
{

/// @brief Decode a single-beam `RawSonarImage` payload to a 1-D magnitude array,
///   honouring the declared dtype + endianness. Handles the types the Garmin GCV
///   emits (uint8, uint16) plus a few common others; an unknown dtype falls back
///   to uint8.
std::vector<double> decodeSamples(const marine_acoustic_msgs::msg::RawSonarImage & msg);

}  // namespace marine_sidescan_mosaic

#endif  // MARINE_SIDESCAN_MOSAIC__DECODE_HPP_
