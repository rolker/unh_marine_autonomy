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

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_autonomy/gggs/index_math.h"
#include "marine_tiled_raster_store/coverage_manifest.hpp"
#include "marine_tiled_raster_store/tile_io.hpp"
#include "marine_tiled_raster_store/tiled_raster_tile.hpp"

// [uma-ADR-0013 D3] Coverage-manifest contract: the all-level directory scan
// (including its mixed-level case and its loud-skip behaviour), run encoding
// that never merges across differing geometric errors, the JSON round-trip, and
// the tolerant read that warns and degrades rather than throwing.

namespace
{

namespace fs = std::filesystem;
namespace mtrs = marine_tiled_raster_store;

constexpr double kLat = 43.07, kLon = -71.42;   // Lake Massabesic — non-polar

class ScratchDir
{
public:
  explicit ScratchDir(const std::string & name)
  : path_(fs::path(::testing::TempDir()) / ("mtrs_coverage_" + name))
  {
    fs::remove_all(path_);
    fs::create_directories(path_);
  }
  ~ScratchDir()
  {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  const fs::path & path() const {return path_;}

private:
  fs::path path_;
};

// Write a minimal 1-band tile under its GGGS name — the scan is filename-only,
// so the content is irrelevant; a real GeoTIFF keeps the fixture honest.
void writeTile(const fs::path & dir, const gggs::GridIndex & grid)
{
  fs::create_directories(dir);
  mtrs::TiledRasterTile<double> tile(grid, 1, 0.0);
  mtrs::saveTile<double>(
    tile, (dir / mtrs::tileFilename(grid)).string(), {std::nullopt});
}

gggs::GridIndex gridAt(int level)
{
  return gggs::Level(static_cast<uint8_t>(level)).gridIndex(kLat, kLon);
}

}  // namespace

// The scan is the fallback for a layer with no manifest, and the input path for
// a producer that must know which regions hold data at which level. It must see
// EVERY level in one pass — a single-level scan is what #331 had to replace.
TEST(CoverageScan, FindsEveryLevelInOnePass)
{
  ScratchDir dir("mixed_levels");
  const gggs::GridIndex fine = gridAt(13);
  writeTile(dir.path(), fine);
  for (const gggs::GridIndex & sibling : gggs::children(gggs::parent(fine))) {
    writeTile(dir.path(), sibling);
  }
  writeTile(dir.path(), gggs::parent(gggs::parent(fine)));   // a native L11

  std::size_t skipped = 0;
  const mtrs::CoverageManifest manifest =
    mtrs::scanCoverage(dir.path().string(), skipped);
  EXPECT_EQ(skipped, 0u);
  EXPECT_EQ(manifest.countAt(13), 4u);   // the four siblings (fine is one of them)
  EXPECT_EQ(manifest.countAt(11), 1u);
  EXPECT_EQ(manifest.countAt(12), 0u);
  const std::vector<uint8_t> levels = manifest.levels();
  ASSERT_EQ(levels.size(), 2u);
  EXPECT_EQ(levels[0], 11) << "levels() must be ascending — coarsest first";
  EXPECT_EQ(levels[1], 13);
  EXPECT_TRUE(manifest.contains(fine));
  EXPECT_FALSE(manifest.contains(gggs::parent(fine)));
}

// A `coverage.json` sitting beside the tiles is not a tile: the `.tif`-only
// regex must ignore it, so introducing the manifest into a layer directory
// cannot perturb the scan (nor trip the callers' skip-refuses-the-swap rule).
TEST(CoverageScan, IgnoresTheManifestFileItself)
{
  ScratchDir dir("ignores_manifest");
  writeTile(dir.path(), gridAt(13));
  std::ofstream(dir.path() / mtrs::coverageManifestFilename()) << "{}";

  std::size_t skipped = 0;
  const mtrs::CoverageManifest manifest =
    mtrs::scanCoverage(dir.path().string(), skipped);
  EXPECT_EQ(skipped, 0u);
  EXPECT_EQ(manifest.size(), 1u);
}

// A tile-shaped name whose fields cannot be represented is skipped and COUNTED,
// whatever level filter the caller asked for — its level field is exactly the
// part that could not be trusted. Callers treat any skip as missing coverage.
TEST(CoverageScan, OutOfRangeNameIsCountedRegardlessOfLevelFilter)
{
  ScratchDir dir("out_of_range");
  writeTile(dir.path(), gridAt(13));
  std::ofstream(dir.path() / "13_99999999999999999999_1.tif") << "overflow";

  std::size_t all_levels = 0;
  const std::vector<gggs::GridIndex> everything =
    mtrs::gridsInDir(dir.path().string(), std::nullopt, all_levels);
  EXPECT_EQ(everything.size(), 1u);
  EXPECT_EQ(all_levels, 1u);

  std::size_t other_level = 0;
  const std::vector<gggs::GridIndex> level_six =
    mtrs::gridsInDir(dir.path().string(), static_cast<uint8_t>(6), other_level);
  EXPECT_TRUE(level_six.empty());
  EXPECT_EQ(other_level, 1u) <<
    "an unrepresentable name is counted even when its (untrusted) level field "
    "is filtered out";
}

TEST(CoverageScan, MissingDirectoryIsEmptyNotAnError)
{
  std::size_t skipped = 0;
  const mtrs::CoverageManifest manifest =
    mtrs::scanCoverage("/nonexistent/definitely/not/here", skipped);
  EXPECT_TRUE(manifest.empty());
  EXPECT_EQ(skipped, 0u);
}

// Adjacent columns in one row collapse into a run; a column gap starts a new
// one. This is the OGC `TileMatrixSetLimits` / Cesium `available` shape.
TEST(CoverageManifestEncoding, MergesAdjacentColumnsAndBreaksOnGaps)
{
  const gggs::GridIndex fine = gridAt(13);
  const std::vector<gggs::GridIndex> siblings = gggs::children(gggs::parent(fine));
  ASSERT_GE(siblings.size(), 4u);

  mtrs::CoverageManifest manifest;
  for (const gggs::GridIndex & grid : siblings) {
    manifest.add(grid);
  }
  const std::vector<mtrs::LevelCoverage> encoded = manifest.encode();
  ASSERT_EQ(encoded.size(), 1u);
  EXPECT_EQ(encoded.front().level, 13);
  // The four siblings are a 2x2 block: two rows, one two-column run each.
  ASSERT_EQ(encoded.front().runs.size(), 2u);
  for (const mtrs::RowRun & run : encoded.front().runs) {
    EXPECT_EQ(run.col_max - run.col_min, 1u);
    EXPECT_FALSE(run.geometric_error_m.has_value());
  }
}

// The geometric error is PER TILE (uma-ADR-0013 D1). Two adjacent tiles with
// different errors must not merge, or the run encoding would silently widen one
// tile's error to its neighbour's.
TEST(CoverageManifestEncoding, DoesNotMergeAcrossDifferingGeometricError)
{
  const std::vector<gggs::GridIndex> siblings =
    gggs::children(gggs::parent(gridAt(13)));
  ASSERT_GE(siblings.size(), 4u);

  mtrs::CoverageManifest manifest;
  double error = 1.0;
  for (const gggs::GridIndex & grid : siblings) {
    manifest.add(grid, error);
    error += 1.0;   // every sibling gets a distinct error
  }
  const std::vector<mtrs::LevelCoverage> encoded = manifest.encode();
  ASSERT_EQ(encoded.size(), 1u);
  EXPECT_EQ(encoded.front().runs.size(), siblings.size()) <<
    "distinct per-tile errors must not be merged into one run";
}

TEST(CoverageManifestIo, RoundTripsThroughJson)
{
  ScratchDir dir("round_trip");
  const gggs::GridIndex fine = gridAt(13);
  mtrs::CoverageManifest manifest;
  for (const gggs::GridIndex & grid : gggs::children(gggs::parent(fine))) {
    manifest.add(grid, 3.5);
  }
  manifest.add(gggs::parent(fine));   // a level with no recorded error

  const std::string path =
    (dir.path() / mtrs::coverageManifestFilename()).string();
  mtrs::saveCoverageManifest(manifest, path, "derived");
  EXPECT_FALSE(fs::exists(path + ".tmp")) << "the tmp file must be renamed away";

  const std::optional<mtrs::CoverageManifest> reloaded =
    mtrs::loadCoverageManifest(path);
  ASSERT_TRUE(reloaded.has_value());
  EXPECT_EQ(reloaded->size(), manifest.size());
  for (const gggs::GridIndex & grid : gggs::children(gggs::parent(fine))) {
    EXPECT_TRUE(reloaded->contains(grid));
    ASSERT_TRUE(reloaded->geometricError(grid).has_value());
    EXPECT_DOUBLE_EQ(*reloaded->geometricError(grid), 3.5);
  }
  EXPECT_TRUE(reloaded->contains(gggs::parent(fine)));
  EXPECT_FALSE(reloaded->geometricError(gggs::parent(fine)).has_value());
}

// The manifest is advisory (uma-ADR-0013 D8): an unreadable one must degrade to
// the scan fallback, never throw into a caller's critical path.
TEST(CoverageManifestIo, TolerantReadOfMalformedDocuments)
{
  ScratchDir dir("tolerant");
  const fs::path path = dir.path() / mtrs::coverageManifestFilename();

  EXPECT_FALSE(mtrs::loadCoverageManifest(path.string()).has_value()) <<
    "absent is normal";

  std::ofstream(path) << "{ not json";
  EXPECT_FALSE(mtrs::loadCoverageManifest(path.string()).has_value());

  std::ofstream(path, std::ios::trunc) << R"({"schema":"something/else"})";
  EXPECT_FALSE(mtrs::loadCoverageManifest(path.string()).has_value());

  std::ofstream(path, std::ios::trunc) << R"({"schema":"coverage-manifest/1"})";
  EXPECT_FALSE(mtrs::loadCoverageManifest(path.string()).has_value());

  std::ofstream(path, std::ios::trunc) <<
    R"({"schema":"coverage-manifest/1","kind":"derived",)"
    R"("levels":[{"level":13,"runs":[{"row":1}]}]})";
  EXPECT_FALSE(mtrs::loadCoverageManifest(path.string()).has_value()) <<
    "a run missing col_min/col_max is malformed, not silently half-read";
}

// A hand-edited or corrupt run wider than the level's own grid extent must be
// refused, not expanded: expanding it would spend minutes warning about tens of
// millions of grids that cannot exist.
TEST(CoverageManifestIo, RefusesRunsOutsideTheLevelExtent)
{
  ScratchDir dir("absurd_run");
  const fs::path path = dir.path() / mtrs::coverageManifestFilename();
  std::ofstream(path) <<
    R"({"schema":"coverage-manifest/1","kind":"derived","levels":[)"
    R"({"level":13,"runs":[{"row":1,"col_min":0,"col_max":4294967295}]}]})";

  const std::optional<mtrs::CoverageManifest> manifest =
    mtrs::loadCoverageManifest(path.string());
  ASSERT_TRUE(manifest.has_value());
  EXPECT_TRUE(manifest->empty()) << "the absurd run must be dropped, not expanded";
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
