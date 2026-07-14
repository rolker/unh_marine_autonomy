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

/// @file
/// @brief Survey index query CLI: point/box → which bags saw it (#259).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

#include "marine_survey_index/footprint.hpp"
#include "marine_survey_index/query.hpp"
#include "marine_survey_index/schema.hpp"

namespace
{

std::string argValue(int argc, char ** argv, const std::string & flag, const std::string & dflt)
{
  for (int i = 1; i + 1 < argc; ++i) {
    if (flag == argv[i]) {
      return argv[i + 1];
    }
  }
  return dflt;
}

int argIndex(int argc, char ** argv, const std::string & flag)
{
  for (int i = 1; i < argc; ++i) {
    if (flag == argv[i]) {
      return i;
    }
  }
  return -1;
}

double toDouble(const char * s, const std::string & flag)
{
  try {
    return std::stod(s);
  } catch (const std::exception &) {
    std::cerr << "error: expected a number for " << flag << ", got '" << s << "'\n";
    std::exit(2);
  }
}

int toInt(const std::string & s, const std::string & flag)
{
  try {
    return std::stoi(s);
  } catch (const std::exception &) {
    std::cerr << "error: expected an integer for " << flag << ", got '" << s << "'\n";
    std::exit(2);
  }
}

// GGGS levels are 0..20 (gggs::Level throws std::out_of_range at >= 21, and it
// is constructed outside any try/catch → std::terminate). Reject out-of-range
// values here so the CLI exits cleanly instead of aborting.
std::uint8_t toLevel(const std::string & s, const std::string & flag)
{
  const int v = toInt(s, flag);
  if (v < 0 || v > 20) {
    std::cerr << "error: " << flag << " must be in [0, 20], got " << v << "\n";
    std::exit(2);
  }
  return static_cast<std::uint8_t>(v);
}

std::string isoUtc(std::int64_t t_ns)
{
  const std::time_t seconds = static_cast<std::time_t>(t_ns / 1000000000LL);
  std::tm tm_utc{};
  gmtime_r(&seconds, &tm_utc);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
  return buffer;
}

std::string jsonEscape(const std::string & s)
{
  std::string out;
  for (const char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buffer[7];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(c));
          out += buffer;
        } else {
          out += c;
        }
    }
  }
  return out;
}

}  // namespace

int main(int argc, char ** argv)
{
  const int point_at = argIndex(argc, argv, "--point");
  const int box_at = argIndex(argc, argv, "--box");
  if ((point_at < 0) == (box_at < 0)) {  // exactly one of --point / --box
    std::cerr <<
      "usage: survey_index_query (--point LAT LON [--radius M] |\n"
      "                           --box LATMIN LONMIN LATMAX LONMAX)\n"
      "       [--db survey_index.db] [--level N] [--sensor TYPE] [--json]\n"
      "\n"
      "Answers: which bags, which time ranges, which sensor saw this location.\n"
      "--sensor accepts 'mbes-bathy', 'sidescan-port', 'sidescan-stbd', or\n"
      "'sidescan' (matches both channels). Without --level, every level present\n"
      "in the index is queried.\n";
    return 2;
  }

  double lat_min, lon_min, lat_max, lon_max;
  if (point_at >= 0) {
    if (point_at + 2 >= argc) {
      std::cerr << "error: --point needs LAT LON\n";
      return 2;
    }
    const double lat = toDouble(argv[point_at + 1], "--point LAT");
    const double lon = toDouble(argv[point_at + 2], "--point LON");
    const double radius_m = toDouble(argValue(argc, argv, "--radius", "0").c_str(), "--radius");
    // Small-area degree approximation — plenty for a query radius.
    const double dlat = radius_m / 111320.0;
    const double cos_lat = std::max(0.01, std::cos(lat * M_PI / 180.0));
    const double dlon = radius_m / (111320.0 * cos_lat);
    lat_min = lat - dlat;
    lat_max = lat + dlat;
    lon_min = lon - dlon;
    lon_max = lon + dlon;
  } else {
    if (box_at + 4 >= argc) {
      std::cerr << "error: --box needs LATMIN LONMIN LATMAX LONMAX\n";
      return 2;
    }
    lat_min = toDouble(argv[box_at + 1], "--box LATMIN");
    lon_min = toDouble(argv[box_at + 2], "--box LONMIN");
    lat_max = toDouble(argv[box_at + 3], "--box LATMAX");
    lon_max = toDouble(argv[box_at + 4], "--box LONMAX");
  }

  const std::string db_path = argValue(argc, argv, "--db", "survey_index.db");
  const std::string sensor = argValue(argc, argv, "--sensor", "");
  const std::string level_arg = argValue(argc, argv, "--level", "");
  const bool json = argIndex(argc, argv, "--json") >= 0;

  sqlite3 * db = nullptr;
  try {
    db = marine_survey_index::openIndexDb(db_path);
  } catch (const std::exception & e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }

  // Which levels to translate the query geometry into: the explicit --level,
  // or every level the index actually holds (for the sensor filter).
  std::vector<std::uint8_t> levels;
  if (!level_arg.empty()) {
    levels.push_back(toLevel(level_arg, "--level"));
  } else {
    levels = marine_survey_index::distinctLevels(db, sensor);
  }

  std::vector<gggs::GridIndex> tiles;
  for (const auto level_n : levels) {
    const auto level_tiles = marine_survey_index::tilesForBoundingBox(
      lat_min, lon_min, lat_max, lon_max, gggs::Level(level_n));
    tiles.insert(tiles.end(), level_tiles.begin(), level_tiles.end());
  }

  const auto rows = marine_survey_index::queryPasses(db, tiles, sensor);
  sqlite3_close(db);

  if (json) {
    std::cout << "[";
    for (std::size_t i = 0; i < rows.size(); ++i) {
      const auto & r = rows[i];
      std::cout << (i ? ",\n " : "\n ")
                << "{\"bag\":\"" << jsonEscape(r.bag_path) << "\""
                << ",\"sensor\":\"" << jsonEscape(r.sensor_type) << "\""
                << ",\"topic\":\"" << jsonEscape(r.topic) << "\""
                << ",\"level\":" << static_cast<int>(r.level)
                << ",\"tile_row\":" << r.tile_row
                << ",\"tile_col\":" << r.tile_col
                << ",\"t_start_ns\":" << r.t_start_ns
                << ",\"t_end_ns\":" << r.t_end_ns
                << ",\"t_start\":\"" << isoUtc(r.t_start_ns) << "\""
                << ",\"t_end\":\"" << isoUtc(r.t_end_ns) << "\""
                << ",\"ping_count\":" << r.ping_count << "}";
    }
    std::cout << (rows.empty() ? "]\n" : "\n]\n");
    return 0;
  }

  if (rows.empty()) {
    std::cout << "no indexed data covers the query area\n";
    return 0;
  }
  std::string current_bag;
  for (const auto & r : rows) {
    if (r.bag_path != current_bag) {
      current_bag = r.bag_path;
      std::cout << current_bag << "\n";
    }
    const double duration_s =
      static_cast<double>(r.t_end_ns - r.t_start_ns) / 1e9;
    std::cout << "  " << isoUtc(r.t_start_ns) << " .. " << isoUtc(r.t_end_ns)
              << " (" << duration_s << " s, " << r.ping_count << " pings) "
              << r.sensor_type << " L" << static_cast<int>(r.level)
              << " tile " << r.tile_row << "/" << r.tile_col
              << "  [" << r.topic << "]\n";
  }
  return 0;
}
