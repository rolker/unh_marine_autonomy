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

#ifndef MARINE_SURVEY_INDEX__NAV_DECIMATION_HPP_
#define MARINE_SURVEY_INDEX__NAV_DECIMATION_HPP_

/// @file
/// @brief Distance-based decimation gate for the indexer's nav track.
///
/// Header-only (like interval_accumulator.hpp) so the gate logic is unit
/// tested without bag I/O. The indexer feeds it every posed ping's ground
/// origin; points that advance less than the stride from the last accepted
/// point are dropped, giving the `nav_track` table uniform *spatial* density —
/// a station-keeping boat adds no points, a fast transit stays fully sampled.

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace marine_survey_index
{

/// Great-circle distance between two WGS-84 positions (degrees in, metres
/// out) via the Haversine formula on a mean-radius sphere. Centimetre-level
/// accuracy at the decimation strides used here (metres to tens of metres);
/// not for geodetic-grade work.
inline double haversineMeters(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg)
{
  constexpr double kMeanEarthRadiusM = 6371008.8;
  constexpr double kDegToRad = M_PI / 180.0;
  const double dlat = (lat2_deg - lat1_deg) * kDegToRad;
  const double dlon = (lon2_deg - lon1_deg) * kDegToRad;
  const double sin_half_dlat = std::sin(dlat / 2.0);
  const double sin_half_dlon = std::sin(dlon / 2.0);
  const double a = sin_half_dlat * sin_half_dlat +
    std::cos(lat1_deg * kDegToRad) * std::cos(lat2_deg * kDegToRad) *
    sin_half_dlon * sin_half_dlon;
  return 2.0 * kMeanEarthRadiusM * std::asin(std::min(1.0, std::sqrt(a)));
}

/// Distance gate: accept() returns true for the first point and for any point
/// at least @c stride_m from the *last accepted* point (accepted points become
/// the new reference; rejected ones do not accumulate). Non-finite input is
/// rejected without touching the reference — a NaN accepted as reference would
/// make every later distance NaN and defeat the gate for the rest of the bag.
class NavDecimator
{
public:
  /// @throws std::invalid_argument unless @p stride_m is finite and > 0 —
  ///   a zero/negative/NaN stride would accept every finite point (defeating
  ///   decimation) and +inf would accept only the first. The CLI validates
  ///   its flag too, but a public header must defend itself.
  explicit NavDecimator(double stride_m)
  : stride_m_(stride_m)
  {
    if (!(stride_m > 0.0) || !std::isfinite(stride_m)) {
      throw std::invalid_argument("NavDecimator: stride_m must be finite and > 0");
    }
  }

  bool accept(double lat_deg, double lon_deg)
  {
    if (!std::isfinite(lat_deg) || !std::isfinite(lon_deg)) {
      return false;
    }
    if (has_last_ &&
      haversineMeters(last_lat_, last_lon_, lat_deg, lon_deg) < stride_m_)
    {
      return false;
    }
    has_last_ = true;
    last_lat_ = lat_deg;
    last_lon_ = lon_deg;
    return true;
  }

private:
  double stride_m_;
  bool has_last_ = false;
  double last_lat_ = 0.0;
  double last_lon_ = 0.0;
};

}  // namespace marine_survey_index

#endif  // MARINE_SURVEY_INDEX__NAV_DECIMATION_HPP_
