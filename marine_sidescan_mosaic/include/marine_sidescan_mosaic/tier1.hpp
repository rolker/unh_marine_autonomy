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

#ifndef MARINE_SIDESCAN_MOSAIC__TIER1_HPP_
#define MARINE_SIDESCAN_MOSAIC__TIER1_HPP_

#include <cstdint>
#include <iosfwd>
#include <vector>

/// @file
/// @brief Tier-1 bottom-agnostic per-ping record + binary I/O (ADR-0006 D2/D3).
///
/// Tier-1 is the decode-once durable archive: per ping it keeps the **baked
/// `earth`→transducer pose** (nav + attitude + mounting), the decoded backscatter
/// indexed by **slant** range, and the acoustic scalars needed to recover ground
/// geometry later — and **no bottom model**. Tier-2 projects this against whatever
/// bathymetry is current (flat-bottom fallback), so refining bathy never re-reads
/// the bags; only a nav/mounting change does.
///
/// The on-disk format here is a deliberately simple length-prefixed binary
/// (host-endian) for the Phase-2 prototype; a columnar format (Parquet/Arrow) is a
/// follow-up and does not change this record's fields.

namespace marine_sidescan_mosaic
{

/// @brief Which transducer channel a Tier-1 ping came from.
enum class Tier1Channel : std::uint8_t
{
  Port = 0,
  Starboard = 1
};

/// @brief One per-ping Tier-1 record (bottom-agnostic).
struct Tier1Ping
{
  std::int64_t stamp_ns = 0;            ///< ping time, ROS-native nanoseconds.
  Tier1Channel channel = Tier1Channel::Port;

  // Baked earth(ECEF)→transducer pose: translation (m) + tf2 quaternion.
  double tx = 0.0, ty = 0.0, tz = 0.0;
  double qx = 0.0, qy = 0.0, qz = 0.0, qw = 1.0;

  double sound_speed = 0.0;            ///< m/s (ping ping_info, else importer fallback).
  double sample_rate = 0.0;            ///< Hz (RawSonarImage.sample_rate).
  std::int32_t sample0 = 0;            ///< near-field gate (RawSonarImage.sample0).
  float nadir_altitude_m = -1.0F;      ///< held transducer height above bottom; <0 = none.

  std::vector<float> samples;          ///< decoded backscatter magnitude, slant-indexed.
};

/// @brief Magic + version for a Tier-1 stream (validated on read).
constexpr std::uint32_t kTier1Magic = 0x53'53'54'31u;  ///< "SST1".
constexpr std::uint32_t kTier1Version = 1u;

/// @brief Write/parse the stream header (call once, first).
void writeTier1Header(std::ostream & os);
bool readTier1Header(std::istream & is);   ///< false on bad magic/version.

/// @brief Append one record / read the next.
void writeTier1Ping(std::ostream & os, const Tier1Ping & p);
bool readTier1Ping(std::istream & is, Tier1Ping & p);   ///< false at clean EOF.

}  // namespace marine_sidescan_mosaic

#endif  // MARINE_SIDESCAN_MOSAIC__TIER1_HPP_
