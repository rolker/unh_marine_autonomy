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

#include "marine_sidescan_mosaic/decode.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace marine_sidescan_mosaic
{

namespace
{
// Decode @p data as packed @p T values to doubles, honouring endianness.
template<typename T>
std::vector<double> decodeAs(const std::vector<std::uint8_t> & data, bool big_endian)
{
  const std::size_t n = data.size() / sizeof(T);
  std::vector<double> out(n);
  for (std::size_t i = 0; i < n; ++i) {
    T value;
    std::memcpy(&value, data.data() + i * sizeof(T), sizeof(T));
    if (big_endian && sizeof(T) > 1) {
      auto * bytes = reinterpret_cast<std::uint8_t *>(&value);
      std::reverse(bytes, bytes + sizeof(T));
    }
    out[i] = static_cast<double>(value);
  }
  return out;
}
}  // namespace

std::vector<double> decodeSamples(const marine_acoustic_msgs::msg::RawSonarImage & msg)
{
  using SonarImageData = marine_acoustic_msgs::msg::SonarImageData;
  const auto & data = msg.image.data;
  const bool be = msg.image.is_bigendian;
  switch (msg.image.dtype) {
    case SonarImageData::DTYPE_UINT8: return decodeAs<std::uint8_t>(data, be);
    case SonarImageData::DTYPE_INT8: return decodeAs<std::int8_t>(data, be);
    case SonarImageData::DTYPE_UINT16: return decodeAs<std::uint16_t>(data, be);
    case SonarImageData::DTYPE_INT16: return decodeAs<std::int16_t>(data, be);
    case SonarImageData::DTYPE_UINT32: return decodeAs<std::uint32_t>(data, be);
    case SonarImageData::DTYPE_FLOAT32: return decodeAs<float>(data, be);
    default: return decodeAs<std::uint8_t>(data, be);
  }
}

}  // namespace marine_sidescan_mosaic
