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

#ifndef MARINE_SURVEY_INDEX__INTERVAL_ACCUMULATOR_HPP_
#define MARINE_SURVEY_INDEX__INTERVAL_ACCUMULATOR_HPP_

/// @file
/// @brief Per-(tile, sensor, topic) pass-interval accumulator.
///
/// Consecutive pings over the same tile merge into one interval while the
/// inter-ping gap stays within the merge gap; a wider gap closes the interval
/// and opens a new one — so two distinct passes over the same tile (e.g. out
/// and back on adjacent survey lines) index as two intervals.

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace marine_survey_index
{

/// Identity of an accumulation stream: one GGGS tile seen by one sensor
/// channel on one topic.
struct TileKey
{
  std::uint8_t level = 0;
  std::uint32_t tile_row = 0;
  std::uint32_t tile_col = 0;
  std::string sensor_type;
  std::string topic;

  bool operator<(const TileKey & o) const
  {
    return std::tie(level, tile_row, tile_col, sensor_type, topic) <
           std::tie(o.level, o.tile_row, o.tile_col, o.sensor_type, o.topic);
  }
  bool operator==(const TileKey & o) const
  {
    return std::tie(level, tile_row, tile_col, sensor_type, topic) ==
           std::tie(o.level, o.tile_row, o.tile_col, o.sensor_type, o.topic);
  }
};

/// One closed pass interval, ready for the `passes` table.
struct PassInterval
{
  TileKey key;
  std::int64_t t_start_ns = 0;
  std::int64_t t_end_ns = 0;
  std::int64_t ping_count = 0;
};

class IntervalAccumulator
{
public:
  /// @param merge_gap_ns Maximum inter-ping gap (ns) that still extends the
  ///   open interval for a key. Bag streams are chronological per topic;
  ///   a ping earlier than the open interval's end (clock skew, interleaving
  ///   jitter) extends the count without moving the end backwards.
  explicit IntervalAccumulator(std::int64_t merge_gap_ns)
  : merge_gap_ns_(merge_gap_ns) {}

  void addPing(const TileKey & key, std::int64_t t_ns)
  {
    auto it = open_.find(key);
    if (it == open_.end()) {
      open_.emplace(key, PassInterval{key, t_ns, t_ns, 1});
      return;
    }
    PassInterval & open = it->second;
    if (t_ns - open.t_end_ns > merge_gap_ns_) {
      closed_.push_back(open);
      open = PassInterval{key, t_ns, t_ns, 1};
      return;
    }
    open.t_end_ns = std::max(open.t_end_ns, t_ns);
    open.t_start_ns = std::min(open.t_start_ns, t_ns);
    ++open.ping_count;
  }

  /// Close every open interval and return all intervals accumulated so far.
  /// The accumulator is empty afterwards (reusable for the next bag).
  std::vector<PassInterval> flushAll()
  {
    for (auto & entry : open_) {
      closed_.push_back(entry.second);
    }
    open_.clear();
    std::vector<PassInterval> out;
    out.swap(closed_);
    return out;
  }

private:
  std::int64_t merge_gap_ns_;
  std::map<TileKey, PassInterval> open_;
  std::vector<PassInterval> closed_;
};

}  // namespace marine_survey_index

#endif  // MARINE_SURVEY_INDEX__INTERVAL_ACCUMULATOR_HPP_
