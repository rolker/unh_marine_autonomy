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

// [uma-ADR-0013 D1/D2/D3] Mixed-level coverage declaration for a tiled raster
// layer, plus the geographic grid reconstruction that both the coverage scan and
// the overview builders need.
//
// The grid-reconstruction helpers here (`gridFromTileName`, `gridsInDir`) were
// duplicated near-verbatim in the depth and sidescan overview builders; they
// live here now because a coverage scan is exactly "reconstruct every tile name
// in a directory", and both builders take their input from that scan.

#include "marine_tiled_raster_store/coverage_manifest.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <system_error>

#include <nlohmann/json.hpp>

#include "marine_tiled_raster_store/tile_io.hpp"

namespace marine_tiled_raster_store
{

namespace
{

namespace fs = std::filesystem;

constexpr const char * kSchema = "coverage-manifest/1";

// The name a tile at (level, row, col) would carry, used to drive the
// reconstruction round-trip when decoding a manifest run (which has no file
// behind it).
std::string syntheticTileName(uint8_t level, uint32_t row, uint32_t col)
{
  return std::to_string(static_cast<unsigned>(level)) + "_" +
         std::to_string(row) + "_" + std::to_string(col) + ".tif";
}

}  // namespace

gggs::GridIndex gridFromTileName(
  uint8_t level, uint32_t row, uint32_t col, const std::string & name)
{
  if (static_cast<std::size_t>(level) >= gggs::levels.size()) {
    std::cerr << "warning: skipping " << name <<
      " (level " << static_cast<unsigned>(level) << " is not a GGGS level)\n";
    return gggs::GridIndex();
  }
  const double span = gggs::levels[level].grid_angular_span;
  const double south = -96.0 + row * span;
  const double lat = south + span / 2.0;
  const double lon_span = span * gggs::latitudeScaleFactor(lat);
  const double west = -180.0 + col * lon_span;
  const double lon = west + lon_span / 2.0;
  gggs::GridIndex grid;
  try {
    // A row/column field far out of range puts `lat`/`lon` outside the geodetic
    // domain and gggs::Level::gridIndex throws — one malformed name must skip
    // its own file, not abort the whole run.
    grid = gggs::Level(level).gridIndex(lat, lon);
  } catch (const std::exception & e) {
    std::cerr << "warning: skipping " << name <<
      " (grid reconstruction failed: " << e.what() << ")\n";
    return gggs::GridIndex();
  }
  if (tileFilename(grid) != name) {
    std::cerr << "warning: skipping " << name <<
      " (grid reconstruction mismatch)\n";
    return gggs::GridIndex();
  }
  return grid;
}

std::vector<gggs::GridIndex> gridsInDir(
  const std::string & dir, std::optional<uint8_t> level, std::size_t & skipped)
{
  // `.tif` only: tileFilename() emits nothing else, so a `.tiff` here is not one
  // of our tiles — matching it would only produce a misleading "grid
  // reconstruction mismatch" for a file we never wrote. A `coverage.json`
  // sitting in the same directory is likewise not matched.
  static const std::regex kName(R"((\d+)_(\d+)_(\d+)\.tif)");
  std::vector<gggs::GridIndex> grids;
  const fs::path dir_path(dir);
  if (!fs::is_directory(dir_path)) {
    return grids;
  }
  for (const auto & entry : fs::directory_iterator(dir_path)) {
    std::smatch m;
    const std::string name = entry.path().filename().string();
    if (!entry.is_regular_file() || !std::regex_match(name, m, kName)) {
      continue;
    }
    // The regex only proves the fields are digits — an overlong one still
    // overflows std::stoul. A malformed name must skip its own file loudly, as
    // documented, not abort the whole run with an uncaught out_of_range.
    unsigned long parts[3] = {0, 0, 0};   // NOLINT(runtime/int) — stoul's type
    bool parsed = true;
    for (std::size_t p = 0; p < 3 && parsed; ++p) {
      try {
        parts[p] = std::stoul(m[p + 1]);
      } catch (const std::exception &) {
        parsed = false;
      }
    }
    constexpr unsigned long kMaxIndex = 0xFFFFFFFFUL;   // NOLINT(runtime/int)
    constexpr unsigned long kMaxLevel = 20UL;           // NOLINT(runtime/int)
    if (!parsed || parts[0] > kMaxLevel ||
      parts[1] > kMaxIndex || parts[2] > kMaxIndex)
    {
      // Counted whatever @p level asks for: the level field is precisely the
      // part that could not be trusted, so this name cannot be filtered out of
      // the caller's attention on the strength of it.
      std::cerr << "warning: skipping " << name <<
        " (level/row/column out of representable range)\n";
      ++skipped;
      continue;
    }
    if (level.has_value() && parts[0] != *level) {
      continue;
    }
    const gggs::GridIndex grid = gridFromTileName(
      static_cast<uint8_t>(parts[0]), static_cast<uint32_t>(parts[1]),
      static_cast<uint32_t>(parts[2]), name);
    if (grid.valid()) {
      grids.push_back(grid);
    } else {
      ++skipped;
    }
  }
  return grids;
}

void CoverageManifest::add(
  const gggs::GridIndex & grid, std::optional<double> geometric_error_m)
{
  if (!grid.valid()) {
    return;
  }
  const auto inserted = entries_.emplace(grid, geometric_error_m);
  if (inserted.second) {
    ++count_by_level_[grid.level()];
  } else {
    inserted.first->second = geometric_error_m;
  }
}

bool CoverageManifest::contains(const gggs::GridIndex & grid) const
{
  return grid.valid() && entries_.find(grid) != entries_.end();
}

std::optional<double> CoverageManifest::geometricError(
  const gggs::GridIndex & grid) const
{
  const auto it = entries_.find(grid);
  return it == entries_.end() ? std::nullopt : it->second;
}

std::size_t CoverageManifest::countAt(uint8_t level) const
{
  const auto it = count_by_level_.find(level);
  return it == count_by_level_.end() ? 0u : it->second;
}

std::vector<uint8_t> CoverageManifest::levels() const
{
  std::vector<uint8_t> out;
  out.reserve(count_by_level_.size());
  for (const auto & entry : count_by_level_) {
    out.push_back(entry.first);
  }
  return out;   // std::map iterates ascending: coarsest level first
}

std::vector<gggs::GridIndex> CoverageManifest::gridsAt(uint8_t level) const
{
  std::vector<gggs::GridIndex> out;
  out.reserve(countAt(level));
  for (const auto & entry : entries_) {
    if (entry.first.level() == level) {
      out.push_back(entry.first);
    }
  }
  return out;
}

std::vector<LevelCoverage> CoverageManifest::encode() const
{
  std::vector<LevelCoverage> out;
  // entries_ is ordered by level, then row, then column, so a single forward
  // pass emits ascending levels and, within each, ascending rows and columns —
  // exactly the shape a run encoder needs.
  for (const auto & entry : entries_) {
    const gggs::GridIndex & grid = entry.first;
    if (out.empty() || out.back().level != grid.level()) {
      out.push_back(LevelCoverage{grid.level(), {}});
    }
    std::vector<RowRun> & runs = out.back().runs;
    // Extend the open run only when this tile is the next column in the same
    // row AND carries the same geometric error — merging across differing
    // errors would silently widen a per-tile value into a per-run maximum.
    if (!runs.empty() && runs.back().row == grid.row() &&
      runs.back().col_max + 1 == grid.column() &&
      runs.back().geometric_error_m == entry.second)
    {
      runs.back().col_max = grid.column();
      continue;
    }
    runs.push_back(RowRun{grid.row(), grid.column(), grid.column(), entry.second});
  }
  return out;
}

CoverageManifest CoverageManifest::decode(const std::vector<LevelCoverage> & levels)
{
  CoverageManifest manifest;
  for (const LevelCoverage & level : levels) {
    if (static_cast<std::size_t>(level.level) >= gggs::levels.size()) {
      std::cerr << "warning: skipping coverage at level " <<
        static_cast<unsigned>(level.level) << " (not a GGGS level)\n";
      continue;
    }
    for (const RowRun & run : level.runs) {
      if (run.col_max < run.col_min) {
        std::cerr << "warning: skipping coverage run at level " <<
          static_cast<unsigned>(level.level) << " row " << run.row <<
          " (col_max " << run.col_max << " precedes col_min " << run.col_min <<
          ")\n";
        continue;
      }
      // A run wider than the level's own column count cannot describe real
      // coverage, and expanding it would spend minutes warning about tens of
      // millions of grids that do not exist. Refuse the run instead — a
      // hand-edited or corrupt manifest must not become a denial of service.
      if (run.row >= gggs::levels[level.level].row_count ||
        run.col_max - run.col_min >=
        gggs::levels[level.level].columnCount(run.row))
      {
        std::cerr << "warning: skipping coverage run at level " <<
          static_cast<unsigned>(level.level) << " row " << run.row <<
          " columns " << run.col_min << ".." << run.col_max <<
          " (outside the level's grid extent)\n";
        continue;
      }
      for (uint32_t col = run.col_min; col <= run.col_max; ++col) {
        manifest.add(
          gridFromTileName(
            level.level, run.row, col, syntheticTileName(level.level, run.row, col)),
          run.geometric_error_m);
      }
    }
  }
  return manifest;
}

CoverageManifest scanCoverage(const std::string & dir, std::size_t & skipped)
{
  CoverageManifest manifest;
  for (const gggs::GridIndex & grid : gridsInDir(dir, std::nullopt, skipped)) {
    manifest.add(grid);
  }
  return manifest;
}

void saveCoverageManifest(
  const CoverageManifest & manifest, const std::string & path,
  const std::string & kind)
{
  nlohmann::json levels = nlohmann::json::array();
  for (const LevelCoverage & level : manifest.encode()) {
    nlohmann::json runs = nlohmann::json::array();
    for (const RowRun & run : level.runs) {
      nlohmann::json entry{
        {"row", run.row}, {"col_min", run.col_min}, {"col_max", run.col_max}};
      if (run.geometric_error_m.has_value()) {
        entry["geometric_error_m"] = *run.geometric_error_m;
      }
      runs.push_back(std::move(entry));
    }
    levels.push_back(
      nlohmann::json{{"level", static_cast<unsigned>(level.level)},
        {"runs", std::move(runs)}});
  }
  const nlohmann::json doc{
    {"schema", kSchema}, {"kind", kind}, {"levels", std::move(levels)}};

  const fs::path final_path(path);
  const fs::path tmp_path = fs::path(path + ".tmp");
  {
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      throw std::runtime_error(
        "saveCoverageManifest: could not open " + tmp_path.string());
    }
    out << doc.dump(2) << '\n';
    out.flush();
    if (!out) {
      throw std::runtime_error(
        "saveCoverageManifest: write failed for " + tmp_path.string());
    }
  }
  // Atomic publish: rename over the existing file. rename() on the same
  // filesystem is atomic, so a reader sees either the old or the new file whole.
  // On rename failure remove the tmp file rather than leaving an orphan.
  try {
    fs::rename(tmp_path, final_path);
  } catch (...) {
    std::error_code ec;
    fs::remove(tmp_path, ec);
    throw;
  }
}

std::optional<CoverageManifest> loadCoverageManifest(const std::string & path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;   // absent is normal: the scan fallback covers it
  }
  nlohmann::json doc;
  try {
    in >> doc;
  } catch (const std::exception & e) {
    std::cerr << "warning: ignoring coverage manifest " << path <<
      " (not valid JSON: " << e.what() << ")\n";
    return std::nullopt;
  }
  if (!doc.is_object()) {
    std::cerr << "warning: ignoring coverage manifest " << path <<
      " (top level is not an object)\n";
    return std::nullopt;
  }
  const auto schema = doc.find("schema");
  if (schema == doc.end() || !schema->is_string() ||
    schema->get<std::string>() != kSchema)
  {
    std::cerr << "warning: ignoring coverage manifest " << path <<
      " (schema is not \"" << kSchema << "\")\n";
    return std::nullopt;
  }
  const auto levels_field = doc.find("levels");
  if (levels_field == doc.end() || !levels_field->is_array()) {
    std::cerr << "warning: ignoring coverage manifest " << path <<
      " (no \"levels\" array)\n";
    return std::nullopt;
  }

  std::vector<LevelCoverage> levels;
  for (const auto & level_doc : *levels_field) {
    if (!level_doc.is_object() || !level_doc.contains("level") ||
      !level_doc["level"].is_number_unsigned() || !level_doc.contains("runs") ||
      !level_doc["runs"].is_array())
    {
      std::cerr << "warning: ignoring coverage manifest " << path <<
        " (a level entry is malformed)\n";
      return std::nullopt;
    }
    LevelCoverage level;
    level.level = static_cast<uint8_t>(level_doc["level"].get<unsigned>());
    for (const auto & run_doc : level_doc["runs"]) {
      if (!run_doc.is_object() || !run_doc.contains("row") ||
        !run_doc["row"].is_number_unsigned() || !run_doc.contains("col_min") ||
        !run_doc["col_min"].is_number_unsigned() ||
        !run_doc.contains("col_max") || !run_doc["col_max"].is_number_unsigned())
      {
        std::cerr << "warning: ignoring coverage manifest " << path <<
          " (a run entry is malformed)\n";
        return std::nullopt;
      }
      RowRun run;
      run.row = run_doc["row"].get<uint32_t>();
      run.col_min = run_doc["col_min"].get<uint32_t>();
      run.col_max = run_doc["col_max"].get<uint32_t>();
      const auto error_field = run_doc.find("geometric_error_m");
      if (error_field != run_doc.end() && error_field->is_number()) {
        run.geometric_error_m = error_field->get<double>();
      }
      level.runs.push_back(run);
    }
    levels.push_back(std::move(level));
  }
  return CoverageManifest::decode(levels);
}

}  // namespace marine_tiled_raster_store
