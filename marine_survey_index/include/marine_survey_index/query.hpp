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

#ifndef MARINE_SURVEY_INDEX__QUERY_HPP_
#define MARINE_SURVEY_INDEX__QUERY_HPP_

/// @file
/// @brief Tile-join query over the survey index.

#include <sqlite3.h>

#include <cstdint>
#include <string>
#include <vector>

#include "marine_autonomy/gggs.h"

namespace marine_survey_index
{

/// One pass interval joined with its bag, as returned to the CLI.
struct PassRow
{
  std::string bag_path;
  std::string sensor_type;
  std::string topic;
  std::uint8_t level = 0;
  std::uint32_t tile_row = 0;
  std::uint32_t tile_col = 0;
  std::int64_t t_start_ns = 0;
  std::int64_t t_end_ns = 0;
  std::int64_t ping_count = 0;
};

/// @brief Distinct GGGS levels present in the index (optionally for one
///   sensor filter — same semantics as queryPasses).
std::vector<std::uint8_t> distinctLevels(sqlite3 * db, const std::string & sensor_filter);

/// @brief Passes overlapping any of @p tiles, joined with their bags.
///
/// @param tiles Candidate tiles (mixed levels allowed; each is matched with
///   its own level).
/// @param sensor_filter Empty = all sensors. The literal "sidescan" matches
///   every `sidescan-*` channel (the D3 sensor_class → channel-split mapping);
///   any other value matches `sensor_type` exactly.
/// @return Rows ordered by bag path, then t_start_ns.
std::vector<PassRow> queryPasses(
  sqlite3 * db, const std::vector<gggs::GridIndex> & tiles,
  const std::string & sensor_filter);

}  // namespace marine_survey_index

#endif  // MARINE_SURVEY_INDEX__QUERY_HPP_
