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

#include "marine_sidescan_mosaic/tier1.hpp"

#include <cstdint>
#include <istream>
#include <ostream>

namespace marine_sidescan_mosaic
{

namespace
{
// Upper bound on a record's sample count, so a corrupt/truncated stream can't
// drive an unbounded allocation (a single ping is ~2k samples; this is a DoS
// ceiling, not a real limit).
constexpr std::uint32_t kMaxSamplesPerPing = 1u << 20;

template<typename T>
void put(std::ostream & os, const T & v)
{
  os.write(reinterpret_cast<const char *>(&v), sizeof(T));
}

template<typename T>
bool get(std::istream & is, T & v)
{
  is.read(reinterpret_cast<char *>(&v), sizeof(T));
  return static_cast<bool>(is);
}
}  // namespace

// Format contract: fields are written in the writer's native byte order. All
// current hosts are little-endian x86-64. The magic is the endianness guard — a
// stream written on the opposite endianness reads back a byte-swapped magic that
// fails readTier1Header, so a mismatched host rejects the file rather than reading
// garbage doubles. (If a big-endian host is ever introduced, switch to explicit
// LE serialization here; the per-field writes make that a localized change.)
void writeTier1Header(std::ostream & os)
{
  put(os, kTier1Magic);
  put(os, kTier1Version);
}

bool readTier1Header(std::istream & is)
{
  std::uint32_t magic = 0;
  std::uint32_t version = 0;
  if (!get(is, magic) || !get(is, version)) {
    return false;
  }
  return magic == kTier1Magic && version == kTier1Version;
}

void writeTier1Ping(std::ostream & os, const Tier1Ping & p)
{
  put(os, p.stamp_ns);
  put(os, static_cast<std::uint8_t>(p.channel));
  put(os, p.tx);
  put(os, p.ty);
  put(os, p.tz);
  put(os, p.qx);
  put(os, p.qy);
  put(os, p.qz);
  put(os, p.qw);
  put(os, p.sound_speed);
  put(os, p.sample_rate);
  put(os, p.sample0);
  put(os, p.nadir_altitude_m);
  const std::uint32_t n = static_cast<std::uint32_t>(p.samples.size());
  put(os, n);
  if (n > 0) {
    os.write(
      reinterpret_cast<const char *>(p.samples.data()),
      static_cast<std::streamsize>(n * sizeof(float)));
  }
}

bool readTier1Ping(std::istream & is, Tier1Ping & p)
{
  if (!get(is, p.stamp_ns)) {
    return false;   // clean EOF: no bytes left at a record boundary.
  }
  // Past the first field, any short read is a TRUNCATED record (corruption), not a
  // clean EOF — bail without populating a half-valid ping.
  std::uint8_t channel = 0;
  if (!get(is, channel) ||
    !get(is, p.tx) || !get(is, p.ty) || !get(is, p.tz) ||
    !get(is, p.qx) || !get(is, p.qy) || !get(is, p.qz) || !get(is, p.qw) ||
    !get(is, p.sound_speed) || !get(is, p.sample_rate) ||
    !get(is, p.sample0) || !get(is, p.nadir_altitude_m))
  {
    return false;
  }
  p.channel = static_cast<Tier1Channel>(channel);
  std::uint32_t n = 0;
  if (!get(is, n) || n > kMaxSamplesPerPing) {
    return false;   // truncated, or an implausible count from a corrupt stream.
  }
  p.samples.resize(n);
  if (n > 0 &&
    !is.read(
      reinterpret_cast<char *>(p.samples.data()),
      static_cast<std::streamsize>(n * sizeof(float))))
  {
    return false;   // sample block short read.
  }
  return true;
}

}  // namespace marine_sidescan_mosaic
