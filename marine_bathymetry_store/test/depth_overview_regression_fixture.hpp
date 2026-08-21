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

#ifndef DEPTH_OVERVIEW_REGRESSION_FIXTURE_HPP_
#define DEPTH_OVERVIEW_REGRESSION_FIXTURE_HPP_

// Pinned fixture + digest helpers for the single-level regression guard (#331).
//
// WHY A HEADER AND NOT INLINE TEST CODE: the golden digest committed at
// `test/data/depth_overview_single_level_golden.txt` was produced by running the
// **pre-#331 binary** over this exact fixture. A post-change test cannot produce
// "the pre-change build", so the reference has to be captured once, from the old
// code, and committed. The generator that captured it used these same helpers,
// so the fixture is byte-pinned rather than re-derived.
//
// The digest is over DECODED per-band raster values plus the exact tile-name
// set — never file bytes. GeoTIFF file bytes vary with the GDAL version, the
// libtiff build, and the creation-option defaults, so a byte digest would be a
// flaky guard for a property (the fold's output values) that is perfectly
// stable.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_autonomy/gggs/index_math.h"
#include "marine_tiled_raster_store/tile_io.hpp"
#include "marine_tiled_raster_store/tiled_raster_tile.hpp"

namespace depth_overview_regression
{

namespace fs = std::filesystem;
namespace mtrs = marine_tiled_raster_store;

/// The fixture's native level and the coarsest level it is built down to. Both
/// are part of the pin: changing either invalidates the committed golden.
constexpr int kFixtureFineLevel = 13;
constexpr int kFixtureMinLevel = 10;

/// Lake Massabesic — non-polar, inside the store's validated envelope.
constexpr double kFixtureLat = 43.07;
constexpr double kFixtureLon = -71.42;

/// @brief The 16 level-13 grids of the fixture: every child of every child of
///        one level-11 grid. Deterministic, derived purely from GGGS.
inline std::vector<gggs::GridIndex> fixtureFineGrids()
{
  const gggs::GridIndex fine =
    gggs::Level(kFixtureFineLevel).gridIndex(kFixtureLat, kFixtureLon);
  const gggs::GridIndex l11 = gggs::parent(gggs::parent(fine));
  std::vector<gggs::GridIndex> grids;
  for (const gggs::GridIndex & l12 : gggs::children(l11)) {
    for (const gggs::GridIndex & l13 : gggs::children(l12)) {
      grids.push_back(l13);
    }
  }
  return grids;
}

/// @brief The fixture's cell value: a closed-form function of the grid's
///        row/column and the cell's row/column, so the fixture is reproducible
///        with no RNG, no clock, and no filesystem order dependence.
///
/// Band 0 (depth) varies across the 2x2 contributor blocks so the
/// shallowest-preserving fold has a real choice at every parent cell; band 1
/// (uncertainty) varies independently so the pair-coherence path is exercised
/// too. A handful of cells are the NaN no-data sentinel, pinning the valid-cell
/// gate as well.
inline void fixtureCellValue(
  const gggs::GridIndex & grid, uint16_t row, uint16_t col,
  double & depth, double & uncertainty)
{
  const uint32_t k = (grid.row() * 31u + grid.column() * 17u + row * 7u + col * 13u);
  if (k % 97u == 0u) {   // the no-data sentinel, deterministically placed
    depth = std::numeric_limits<double>::quiet_NaN();
    uncertainty = std::numeric_limits<double>::quiet_NaN();
    return;
  }
  depth = -5.0 - static_cast<double>(k % 97u) * 0.25;
  uncertainty = 0.1 + static_cast<double>(k % 17u) * 0.05;
}

/// @brief Write the pinned fixture's fine tiles into @p layer_dir.
inline void writeFixture(const fs::path & layer_dir)
{
  fs::create_directories(layer_dir);
  const double nan = std::numeric_limits<double>::quiet_NaN();
  for (const gggs::GridIndex & grid : fixtureFineGrids()) {
    mtrs::TiledRasterTile<double> tile(grid, 2, nan);
    for (uint16_t r = 0; r < tile.edge; ++r) {
      for (uint16_t c = 0; c < tile.edge; ++c) {
        double depth = nan, unc = nan;
        fixtureCellValue(grid, r, c, depth, unc);
        tile.set(r, c, 0, depth);
        tile.set(r, c, 1, unc);
      }
    }
    mtrs::saveTile<double>(
      tile, (layer_dir / mtrs::tileFilename(grid)).string(),
      {std::optional<double>(nan), std::optional<double>(nan)});
  }
}

/// @brief FNV-1a 64 over the IEEE-754 bit patterns of @p values.
///
/// Hashing the bits (not the printed value) keeps every NaN payload and signed
/// zero distinguishable, and is independent of locale and of any formatting
/// choice. Values are hashed in the tile's own row-major cell order, so the
/// digest is over the DECODED raster, not the file's on-disk layout.
inline uint64_t hashDoubles(const std::vector<double> & values)
{
  uint64_t h = 1469598103934665603ULL;
  for (const double v : values) {
    uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    for (int byte = 0; byte < 8; ++byte) {
      h ^= static_cast<uint64_t>((bits >> (byte * 8)) & 0xFFULL);
      h *= 1099511628211ULL;
    }
  }
  return h;
}

inline std::string hex16(uint64_t v)
{
  static const char * digits = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<std::size_t>(i)] = digits[v & 0xFULL];
    v >>= 4;
  }
  return out;
}

/// @brief Canonical digest of a directory of depth tiles: one sorted line per
///        tile, `<tag> <name> <band0-digest> <band1-digest>`.
///
/// Sorted by filename, so filesystem iteration order cannot perturb it. Each
/// tile is DECODED through `loadTile` at the level its own filename declares,
/// and both bands are digested — the tile-name set and the per-band raster
/// values are pinned together. @p tag separates the fixture-input block from
/// the sidecar block within one golden file, so a fixture drift and a fold
/// regression fail as distinct assertions.
inline std::string digestTileDir(const fs::path & dir, const std::string & tag)
{
  std::vector<std::string> names;
  for (const auto & entry : fs::directory_iterator(dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".tif") {
      names.push_back(entry.path().filename().string());
    }
  }
  std::sort(names.begin(), names.end());

  std::ostringstream out;
  for (const std::string & name : names) {
    const int level = std::stoi(name.substr(0, name.find('_')));
    const mtrs::TiledRasterTile<double> tile = mtrs::loadTile<double>(
      (dir / name).string(), gggs::Level(static_cast<uint8_t>(level)), 2);
    out << tag << ' ' << name << ' ' << hex16(hashDoubles(tile.band(0))) << ' ' <<
      hex16(hashDoubles(tile.band(1))) << '\n';
  }
  return out.str();
}

/// @brief Read the @p tag block of a committed golden digest file, stripping
///        `#` comment lines. Returns an empty string when the tag is absent —
///        the caller asserts on non-emptiness so a missing/renamed data file is
///        a loud failure rather than a vacuous pass.
inline std::string readGolden(const std::string & path, const std::string & tag)
{
  std::ifstream in(path);
  std::ostringstream out;
  std::string line;
  const std::string prefix = tag + " ";
  while (std::getline(in, line)) {
    if (line.rfind(prefix, 0) == 0) {
      out << line << '\n';
    }
  }
  return out.str();
}

}  // namespace depth_overview_regression

#endif  // DEPTH_OVERVIEW_REGRESSION_FIXTURE_HPP_
