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

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_sidescan_mosaic/overview_pyramid.hpp"
#include "marine_tiled_raster_store/tile_io.hpp"
#include "marine_tiled_raster_store/tiled_raster_tile.hpp"

// [#188 / ADR-0011] Production-path coverage for the overview-builder CLI:
// argument parsing, and the on-disk fold (grid reconstruction from filenames,
// per-level fold, level loop, atomic sidecar swap, and the empty-layer guard)
// exercised end to end on real GeoTIFF tiles — not just the header engine.

namespace
{

namespace fs = std::filesystem;
namespace mtrs = marine_tiled_raster_store;
namespace msm = marine_sidescan_mosaic;

constexpr int kFineLevel = 13;                 // sidescan native level (temperate)
constexpr double kLat = 43.07, kLon = -71.42;  // Lake Massabesic — non-polar

// A scratch directory unique per test, removed on construction and destruction.
class ScratchDir
{
public:
  explicit ScratchDir(const std::string & name)
  : path_(fs::path(::testing::TempDir()) / ("msm_ovr_" + name))
  {
    fs::remove_all(path_);
    fs::create_directories(path_);
  }
  ~ScratchDir() {fs::remove_all(path_);}
  const fs::path & path() const {return path_;}

private:
  fs::path path_;
};

// Write a 3-band uint16 fine tile (intensity, quality, source) filled with
// @p intensity / @p quality across every cell, into @p dir under its GGGS name.
void writeFineTile(
  const fs::path & dir, const gggs::GridIndex & grid,
  std::uint16_t intensity, std::uint16_t quality)
{
  mtrs::TiledRasterTile<std::uint16_t> tile(grid, 3, 0);
  for (uint16_t r = 0; r < tile.edge; ++r) {
    for (uint16_t c = 0; c < tile.edge; ++c) {
      tile.set(r, c, 0, intensity);
      tile.set(r, c, 1, quality);
      tile.set(r, c, 2, 0);
    }
  }
  const std::string path = (dir / mtrs::tileFilename(grid)).string();
  mtrs::saveTile<std::uint16_t>(
    tile, path, {std::nullopt, std::nullopt, std::nullopt});
}

// The four fine L13 siblings that tile one L12 parent at the test site.
std::vector<gggs::GridIndex> fineSiblings()
{
  const gggs::GridIndex fine = gggs::Level(kFineLevel).gridIndex(kLat, kLon);
  return gggs::children(gggs::parent(fine));
}

// Count files in @p dir whose name begins with `<level>_`.
std::size_t countLevelFiles(const fs::path & dir, int level)
{
  std::size_t n = 0;
  const std::string prefix = std::to_string(level) + "_";
  for (const auto & e : fs::directory_iterator(dir)) {
    if (e.is_regular_file() && e.path().filename().string().rfind(prefix, 0) == 0) {
      ++n;
    }
  }
  return n;
}

}  // namespace

// --- argument parsing -------------------------------------------------------

TEST(ParseOverviewArgs, ValidArgsPopulateOptions)
{
  char a0[] = "prog", a1[] = "/store/layer", a2[] = "--fine-level", a3[] = "12",
    a4[] = "--min-level", a5[] = "8";
  char * argv[] = {a0, a1, a2, a3, a4, a5};
  msm::OverviewOptions opts;
  EXPECT_EQ(msm::parseOverviewArgs(6, argv, opts), msm::ArgStatus::kOk);
  EXPECT_EQ(opts.layer_dir, "/store/layer");
  EXPECT_EQ(opts.fine_level, 12);
  EXPECT_EQ(opts.min_level, 8);
}

TEST(ParseOverviewArgs, DefaultsWhenOnlyLayerGiven)
{
  char a0[] = "prog", a1[] = "/store/layer";
  char * argv[] = {a0, a1};
  msm::OverviewOptions opts;
  EXPECT_EQ(msm::parseOverviewArgs(2, argv, opts), msm::ArgStatus::kOk);
  EXPECT_EQ(opts.fine_level, 13);
  EXPECT_EQ(opts.min_level, 0);
}

TEST(ParseOverviewArgs, UnknownFlagIsError)
{
  char a0[] = "prog", a1[] = "/store/layer", a2[] = "--bogus";
  char * argv[] = {a0, a1, a2};
  msm::OverviewOptions opts;
  EXPECT_EQ(msm::parseOverviewArgs(3, argv, opts), msm::ArgStatus::kError);
}

TEST(ParseOverviewArgs, FlagMissingValueIsError)
{
  char a0[] = "prog", a1[] = "/store/layer", a2[] = "--fine-level";
  char * argv[] = {a0, a1, a2};
  msm::OverviewOptions opts;
  EXPECT_EQ(msm::parseOverviewArgs(3, argv, opts), msm::ArgStatus::kError);
}

TEST(ParseOverviewArgs, HelpRequested)
{
  char a0[] = "prog", a1[] = "--help";
  char * argv[] = {a0, a1};
  msm::OverviewOptions opts;
  EXPECT_EQ(msm::parseOverviewArgs(2, argv, opts), msm::ArgStatus::kHelp);
}

TEST(ParseOverviewArgs, EmptyStringArgIsErrorNotCrash)
{
  char a0[] = "prog", a1[] = "";   // an empty argv token must not be indexed blind
  char * argv[] = {a0, a1};
  msm::OverviewOptions opts;
  EXPECT_EQ(msm::parseOverviewArgs(2, argv, opts), msm::ArgStatus::kError);
}

TEST(ParseOverviewArgs, MinLevelNotBelowFineIsError)
{
  char a0[] = "prog", a1[] = "/store/layer", a2[] = "--fine-level", a3[] = "5",
    a4[] = "--min-level", a5[] = "5";
  char * argv[] = {a0, a1, a2, a3, a4, a5};
  msm::OverviewOptions opts;
  EXPECT_EQ(msm::parseOverviewArgs(6, argv, opts), msm::ArgStatus::kError);
}

// --- the on-disk build ------------------------------------------------------

TEST(BuildOverviewPyramid, WritesLevelDistinguishedSidecar)
{
  ScratchDir dir("sidecar");
  for (const auto & g : fineSiblings()) {
    writeFineTile(dir.path(), g, 100, 200);
  }

  msm::OverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.fine_level = kFineLevel;
  opts.min_level = kFineLevel - 2;   // build L12 and L11

  const msm::OverviewBuildResult r = msm::buildOverviewPyramid(opts);
  EXPECT_GT(r.tiles_written, 0u);
  EXPECT_FALSE(r.early_empty);

  const fs::path overviews = dir.path() / "overviews";
  ASSERT_TRUE(fs::is_directory(overviews));
  // Levels ride in the filename (flat sidecar) — one L12 parent, one L11.
  EXPECT_EQ(countLevelFiles(overviews, kFineLevel - 1), 1u);
  EXPECT_EQ(countLevelFiles(overviews, kFineLevel - 2), 1u);
  // The atomic staging dir must not survive a successful build.
  EXPECT_FALSE(fs::exists(dir.path() / "overviews.tmp"));
}

TEST(BuildOverviewPyramid, MeanFoldRunsEndToEnd)
{
  ScratchDir dir("meanfold");
  for (const auto & g : fineSiblings()) {
    writeFineTile(dir.path(), g, 100, 200);   // uniform -> every parent cell = mean
  }

  msm::OverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.fine_level = kFineLevel;
  opts.min_level = kFineLevel - 1;   // just L12

  msm::buildOverviewPyramid(opts);

  const gggs::GridIndex parent12 = gggs::parent(fineSiblings().front());
  const fs::path path =
    dir.path() / "overviews" / mtrs::tileFilename(parent12);
  const auto tile = mtrs::loadTile<std::uint16_t>(
    path.string(), gggs::Level(kFineLevel - 1), 3);

  // Four uniform children tile the whole parent: every cell folds to the mean.
  std::size_t folded = 0;
  for (uint16_t r = 0; r < tile.edge; ++r) {
    for (uint16_t c = 0; c < tile.edge; ++c) {
      if (tile.get(r, c, 0) == 0) {continue;}
      ++folded;
      EXPECT_EQ(tile.get(r, c, 0), 100);   // mean intensity
      EXPECT_EQ(tile.get(r, c, 1), 200);   // mean quality
      EXPECT_EQ(tile.get(r, c, 2), 0);     // source band 0 in overviews
    }
  }
  EXPECT_GT(folded, 0u);
}

// Regression: the no-data sentinel is the QUALITY band, not intensity. An
// acoustic-shadow cell (intensity 0, quality >= 1) is real surveyed data; a fold
// gated on intensity would drop every such cell, leaving an empty parent and
// biasing overviews bright.
TEST(BuildOverviewPyramid, ShadowCellsWithZeroIntensityStillFold)
{
  ScratchDir dir("shadow");
  for (const auto & g : fineSiblings()) {
    writeFineTile(dir.path(), g, 0, 500);   // dark but surveyed: quality floors >= 1
  }

  msm::OverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.fine_level = kFineLevel;
  opts.min_level = kFineLevel - 1;

  const msm::OverviewBuildResult r = msm::buildOverviewPyramid(opts);
  EXPECT_EQ(r.tiles_skipped, 0u);

  const gggs::GridIndex parent12 = gggs::parent(fineSiblings().front());
  const auto tile = mtrs::loadTile<std::uint16_t>(
    (dir.path() / "overviews" / mtrs::tileFilename(parent12)).string(),
    gggs::Level(kFineLevel - 1), 3);

  // Four uniform shadow children tile the whole parent: every cell must carry
  // the folded quality, and intensity must stay 0 (a dark cell, not no-data).
  std::size_t covered = 0;
  for (uint16_t r2 = 0; r2 < tile.edge; ++r2) {
    for (uint16_t c = 0; c < tile.edge; ++c) {
      EXPECT_EQ(tile.get(r2, c, 0), 0);
      if (tile.get(r2, c, 1) == 500) {++covered;}
    }
  }
  EXPECT_EQ(covered, static_cast<std::size_t>(tile.edge) * tile.edge) <<
    "every shadow cell must survive the fold";
}

TEST(BuildOverviewPyramid, IsValueIdempotent)
{
  ScratchDir dir("idempotent");
  for (const auto & g : fineSiblings()) {
    writeFineTile(dir.path(), g, 123, 45);
  }

  msm::OverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.fine_level = kFineLevel;
  opts.min_level = kFineLevel - 1;

  const gggs::GridIndex parent12 = gggs::parent(fineSiblings().front());
  const fs::path path =
    dir.path() / "overviews" / mtrs::tileFilename(parent12);

  msm::buildOverviewPyramid(opts);
  const auto first = mtrs::loadTile<std::uint16_t>(
    path.string(), gggs::Level(kFineLevel - 1), 3);
  msm::buildOverviewPyramid(opts);   // regenerate the whole sidecar again
  const auto second = mtrs::loadTile<std::uint16_t>(
    path.string(), gggs::Level(kFineLevel - 1), 3);

  EXPECT_EQ(first.band(0), second.band(0)) << "fold must be value-idempotent";
  EXPECT_EQ(first.band(1), second.band(1));
}

TEST(BuildOverviewPyramid, RefusesEmptyLayerAndPreservesExistingSidecar)
{
  ScratchDir dir("guard");
  // A previously-good sidecar with a sentinel; the layer has NO fine tiles.
  const fs::path overviews = dir.path() / "overviews";
  fs::create_directories(overviews);
  const fs::path sentinel = overviews / "13_1_1.tif";
  std::ofstream(sentinel) << "keep";

  msm::OverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.fine_level = kFineLevel;
  opts.min_level = 0;

  EXPECT_THROW(msm::buildOverviewPyramid(opts), std::runtime_error);
  // The guard must not have wiped the existing sidecar.
  EXPECT_TRUE(fs::exists(sentinel));
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
