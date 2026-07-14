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

#include "marine_survey_index/query.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace marine_survey_index
{
namespace
{

// Appends the sensor-filter clause and returns the value to bind (empty =
// nothing to bind). "sidescan" expands to the channel-split LIKE.
std::string appendSensorClause(const std::string & sensor_filter, std::string & sql)
{
  if (sensor_filter.empty()) {
    return "";
  }
  if (sensor_filter == "sidescan") {
    sql += " AND p.sensor_type LIKE 'sidescan%'";
    return "";
  }
  sql += " AND p.sensor_type = ?";
  return sensor_filter;
}

void throwSqlite(sqlite3 * db, const std::string & context)
{
  throw std::runtime_error("survey index query (" + context + "): " + sqlite3_errmsg(db));
}

// sqlite3_column_text returns nullptr for a SQL NULL; constructing a
// std::string from nullptr is UB. These columns are NOT NULL today, but read
// them defensively so a future schema change can't corrupt the query path.
std::string columnText(sqlite3_stmt * stmt, int col)
{
  const unsigned char * text = sqlite3_column_text(stmt, col);
  return text ? reinterpret_cast<const char *>(text) : std::string();
}

}  // namespace

std::vector<std::uint8_t> distinctLevels(sqlite3 * db, const std::string & sensor_filter)
{
  std::string sql = "SELECT DISTINCT p.level FROM passes p WHERE 1=1";
  const std::string bind_value = appendSensorClause(sensor_filter, sql);
  sql += " ORDER BY p.level";

  sqlite3_stmt * stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    throwSqlite(db, "distinctLevels prepare");
  }
  if (!bind_value.empty()) {
    sqlite3_bind_text(stmt, 1, bind_value.c_str(), -1, SQLITE_TRANSIENT);
  }
  std::vector<std::uint8_t> levels;
  int rc;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    levels.push_back(static_cast<std::uint8_t>(sqlite3_column_int(stmt, 0)));
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    throwSqlite(db, "distinctLevels step");
  }
  return levels;
}

std::vector<PassRow> queryPasses(
  sqlite3 * db, const std::vector<gggs::GridIndex> & tiles,
  const std::string & sensor_filter)
{
  std::vector<PassRow> rows;
  if (tiles.empty()) {
    return rows;
  }

  // One (level, row, col) triple per candidate tile. Tile sets are small
  // (a query box spans few tiles at store levels), so an OR chain with bound
  // parameters is simple and stays within SQLite's default limits; chunk to
  // be safe for very large boxes.
  constexpr std::size_t kChunk = 200;
  for (std::size_t begin = 0; begin < tiles.size(); begin += kChunk) {
    const std::size_t end = std::min(begin + kChunk, tiles.size());

    std::string sql =
      "SELECT b.path, p.sensor_type, p.topic, p.level, p.tile_row, p.tile_col,"
      " p.t_start_ns, p.t_end_ns, p.ping_count"
      " FROM passes p JOIN bags b ON b.id = p.bag_id"
      " WHERE (";
    for (std::size_t i = begin; i < end; ++i) {
      if (i != begin) {
        sql += " OR ";
      }
      sql += "(p.level = ? AND p.tile_row = ? AND p.tile_col = ?)";
    }
    sql += ")";
    const std::string bind_value = appendSensorClause(sensor_filter, sql);
    sql += " ORDER BY b.path, p.t_start_ns";

    sqlite3_stmt * stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
      throwSqlite(db, "queryPasses prepare");
    }
    int slot = 1;
    for (std::size_t i = begin; i < end; ++i) {
      sqlite3_bind_int(stmt, slot++, tiles[i].level());
      sqlite3_bind_int64(stmt, slot++, tiles[i].row());
      sqlite3_bind_int64(stmt, slot++, tiles[i].column());
    }
    if (!bind_value.empty()) {
      sqlite3_bind_text(stmt, slot++, bind_value.c_str(), -1, SQLITE_TRANSIENT);
    }

    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
      PassRow row;
      row.bag_path = columnText(stmt, 0);
      row.sensor_type = columnText(stmt, 1);
      row.topic = columnText(stmt, 2);
      row.level = static_cast<std::uint8_t>(sqlite3_column_int(stmt, 3));
      row.tile_row = static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 4));
      row.tile_col = static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 5));
      row.t_start_ns = sqlite3_column_int64(stmt, 6);
      row.t_end_ns = sqlite3_column_int64(stmt, 7);
      row.ping_count = sqlite3_column_int64(stmt, 8);
      rows.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
      throwSqlite(db, "queryPasses step");
    }
  }

  // Each chunk's statement sorts independently, so a box spanning more than one
  // chunk yields globally out-of-order rows (and the CLI would repeat a bag
  // header per chunk). Sort the merged result to honour the documented
  // "ordered by bag path, then t_start_ns" contract.
  std::sort(
    rows.begin(), rows.end(),
    [](const PassRow & a, const PassRow & b) {
      if (a.bag_path != b.bag_path) {
        return a.bag_path < b.bag_path;
      }
      return a.t_start_ns < b.t_start_ns;
    });
  return rows;
}

}  // namespace marine_survey_index
