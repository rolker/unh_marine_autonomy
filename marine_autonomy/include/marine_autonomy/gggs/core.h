// Copyright 2016-2020 Roland Arsenault
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the Roland Arsenault nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef PROJECT11_GGGS_CORE_H
#define PROJECT11_GGGS_CORE_H

#include <cstdint>
#include <cmath>
#include <sstream>

#include <geographic_msgs/msg/geo_point.hpp>

#include "marine_autonomy/utils.h"

/// @file core.h
/// @brief Core constants and utilities for the Global Geographic Grid System (GGGS).
///
/// GGGS is a quadtree-based spatial indexing system for geographic coordinates,
/// based on "A global geographic grid system for visualizing bathymetry" by
/// Colin Ware et al. (https://gi.copernicus.org/articles/9/375/2020/).
///
/// The system divides the Earth's surface into a hierarchy of grids:
/// - Level 0: 8x8 degree tiles (24 rows x 45 columns at the equator)
/// - Each subsequent level subdivides into 4 (2x2), halving the angular span
/// - 21 levels (0-20) span cell sizes from ~928 m down to ~0.9 mm
///
/// Polar scaling widens grids in longitude near the poles to maintain
/// approximately equal area:
/// - |latitude| < 72°: 1x (standard width)
/// - 72° <= |latitude| < 80°: 3x wider
/// - |latitude| >= 80°: 9x wider
///
/// The latitude range extends from -96° to +96° (not -90° to +90°) to
/// accommodate the polar grid rows that span only 2 degrees each.
///
/// Each grid contains 960x960 cells, chosen for compatibility with the
/// GEBCO 2020 bathymetric grid. The 21-level range supports applications
/// from global datasets down to ROV laser, interferometric sidescan, and
/// photogrammetric surveys.

namespace gggs
{

/// @brief Number of cell rows per grid (compatible with GEBCO 2020).
constexpr uint16_t cell_rows_per_grid = 960;
/// @brief Number of cell columns per grid.
constexpr uint16_t cell_columns_per_grid = 960;
/// @brief Total number of cells in a single grid (960 * 960 = 921,600).
constexpr uint32_t grid_total_cell_count = cell_rows_per_grid*cell_columns_per_grid;

/// @brief WGS84 semi-major axis (equatorial radius) in meters.
constexpr double earth_radius_at_equator = marine::WGS84::specs::a;
/// @brief Circumference of the Earth at the equator in meters.
constexpr double equator_circumference = 2.0 * M_PI * earth_radius_at_equator;

/// @brief Approximate size in meters of a level-0 grid (8° at the equator).
constexpr double level_0_grid_size = equator_circumference * 8.0 / 360.0;

/// @brief Number of grid rows at level 0: (96+96)/8 = 24.
constexpr uint32_t level_0_row_count = 24;
/// @brief Number of grid columns at level 0 (at the equator): 360/8 = 45.
constexpr uint32_t level_0_column_count = 45;

/// @brief Compute the longitude scale factor for a given latitude.
///
/// Returns the multiplier applied to grid longitude width near the poles
/// to keep grid areas approximately uniform.
///
/// @param latitude Latitude in degrees (-90 to 90).
/// @return 1 (equatorial), 3 (sub-polar), or 9 (polar).
inline uint8_t latitudeScaleFactor(double latitude)
{
  if (std::abs(latitude) < 72.0)
    return 1;
  if (std::abs(latitude) < 80.0)
    return 3;
  return 9;
}

/// @brief Wrap a longitude in degrees into the half-open range [-180, 180).
///
/// GGGS positions are carried as plain doubles / @c geographic_msgs::msg::GeoPoint,
/// which (unlike the previous angle-aware type) do not self-normalize. This
/// helper restores that behavior at the API boundary so positions near the
/// antimeridian resolve to the correct grid and cell.
/// @param longitude Longitude in degrees (any range).
/// @return Equivalent longitude in [-180, 180).
inline double normalizeLongitude(double longitude)
{
  longitude = std::fmod(longitude + 180.0, 360.0);
  if (longitude < 0.0)
    longitude += 360.0;
  return longitude - 180.0;
}

/// @brief Build a @c geographic_msgs::msg::GeoPoint from latitude/longitude.
/// @param latitude Latitude in degrees.
/// @param longitude Longitude in degrees.
/// @return GeoPoint with the given latitude/longitude and altitude 0.
inline geographic_msgs::msg::GeoPoint geoPoint(double latitude, double longitude)
{
  geographic_msgs::msg::GeoPoint point;
  point.latitude = latitude;
  point.longitude = longitude;
  point.altitude = 0.0;
  return point;
}

/// @brief Verify that two GGGS objects are at the same quadtree level.
/// @throws std::out_of_range if levels differ.
template<typename T1, typename T2>
void verifySameLevel(const T1& a, const T2& b)
{
  if(a.level() != b.level())
  {
    std::stringstream error_message;
    error_message << "Level mismatch between " << a << " and " << b;
    throw(std::out_of_range(error_message.str()));
  }
}


} // namespace gggs

#endif
