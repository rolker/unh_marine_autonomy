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

#include "marine_autonomy/utils.h"

// based on "A global geographic grid system for visualizing bathymetry" by Colin Ware et al.
// https://gi.copernicus.org/articles/9/375/2020/
//
// A system for indexing tiles in a quad tree using geographic coordinates.
// At level 0, tiles are 8x8 degrees for most of the earth. Tiles at or above
// 72 degrees of latitude cover 24 degrees of longitude while tiles at or
// above 80 degrees of latitude cover 72 degrees of longitude. Similar
// scale changes are also applied towards the south pole.

namespace gggs
{

/// Default grid size compatible with GEBCO 2020 grid
constexpr uint16_t cell_rows_per_grid = 960;
constexpr uint16_t cell_columns_per_grid = 960;
constexpr uint32_t grid_total_cell_count = cell_rows_per_grid*cell_columns_per_grid;

constexpr double earth_radius_at_equator = marine::WGS84::specs::a;
constexpr double equator_circumference = 2.0 * M_PI * earth_radius_at_equator;

/// approximate size in meters of an 8 degree area around the equator
constexpr double level_0_grid_size = equator_circumference * 8.0 / 360.0;

constexpr uint32_t level_0_row_count = 24; // (96+96)/8;
constexpr uint32_t level_0_column_count = 45; // 360/8;

/// Calculate grid width factor to account for longitudes
/// bunching up near the poles.
inline uint8_t latitudeScaleFactor(double latitude)
{
  if (std::abs(latitude) < 72.0)
    return 1;
  if (std::abs(latitude) < 80.0)
    return 3;
  return 9;
}

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
