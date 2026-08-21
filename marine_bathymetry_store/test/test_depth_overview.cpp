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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "geographic_msgs/msg/geo_point.hpp"
#include "marine_autonomy/gggs.h"
#include "marine_autonomy/gggs/index_math.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/overview_pyramid.hpp"
#include "marine_bathymetry_store/tile_io.hpp"
#include "marine_tiled_raster_store/coverage_manifest.hpp"
#include "marine_tiled_raster_store/tile_io.hpp"
#include "marine_tiled_raster_store/tiled_raster_tile.hpp"

#include "depth_overview_regression_fixture.hpp"

// [uma-ADR-0010 D9 / uma-ADR-0011 / uma-ADR-0013] Coverage for the depth
// overview-pyramid builder: the shallowest-preserving fold policy (the shoalest
// child's {depth, σ} pair wins, never a mean, and the pair stays coherent), the
// no-upsample invariant, the NaN no-data gate, the malformed-filename skip ->
// swap-refusal safety property, the end-to-end sidecar swap, the tile_io loader's
// SILENT skip of the overviews/ sidecar, and — from #331 — mixed-level native-wins
// discovery, the generalised level-coverage rule, the derived coverage manifest
// with its saturated geometric error, and a golden-fixture pin that the
// single-level case is unchanged. Exercised on real GeoTIFF tiles, not just the
// header engine.

namespace
{

namespace fs = std::filesystem;
namespace mtrs = marine_tiled_raster_store;
namespace mbs = marine_bathymetry_store;

constexpr int kFineLevel = 13;                 // a non-polar native level
constexpr double kLat = 43.07, kLon = -71.42;  // Lake Massabesic — non-polar
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// A scratch directory unique per test, removed on construction and destruction.
class ScratchDir
{
public:
  explicit ScratchDir(const std::string & name)
  : path_(fs::path(::testing::TempDir()) / ("mbs_depth_ovr_" + name))
  {
    fs::remove_all(path_);
    fs::create_directories(path_);
  }
  ~ScratchDir()
  {
    // Non-throwing overload: a cleanup error in the dtor would std::terminate and
    // mask the real test failure.
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  const fs::path & path() const {return path_;}

private:
  fs::path path_;
};

// Redirect std::cerr to a buffer for the lifetime of the guard, restoring it on
// destruction — so a test can assert on (the absence of) a WARNING line.
class CerrCapture
{
public:
  CerrCapture()
  : old_(std::cerr.rdbuf(buffer_.rdbuf())) {}
  ~CerrCapture() {std::cerr.rdbuf(old_);}
  std::string str() const {return buffer_.str();}

private:
  std::ostringstream buffer_;
  std::streambuf * old_;
};

// Write a 2-band Float64 depth fine tile (depth band 0, uncertainty band 1) filled
// with @p depth / @p unc across every cell, into @p dir under its GGGS name.
void writeUniformDepthTile(
  const fs::path & dir, const gggs::GridIndex & grid, double depth, double unc)
{
  mtrs::TiledRasterTile<double> tile(grid, 2, kNaN);
  for (uint16_t r = 0; r < tile.edge; ++r) {
    for (uint16_t c = 0; c < tile.edge; ++c) {
      tile.set(r, c, 0, depth);
      tile.set(r, c, 1, unc);
    }
  }
  mtrs::saveTile<double>(
    tile, (dir / mtrs::tileFilename(grid)).string(),
    {std::optional<double>(kNaN), std::optional<double>(kNaN)});
}

// Write a checkerboard depth tile: cells with (row+col) even carry
// (@p depth_even, @p unc_even); odd cells carry (@p depth_odd, @p unc_odd). Each
// parent cell's aligned 2x2 contributor block then holds exactly two of each, so
// the shallowest-preserving fold has two distinct {depth, σ} pairs to choose from.
void writeCheckerDepthTile(
  const fs::path & dir, const gggs::GridIndex & grid,
  double depth_even, double unc_even, double depth_odd, double unc_odd)
{
  mtrs::TiledRasterTile<double> tile(grid, 2, kNaN);
  for (uint16_t r = 0; r < tile.edge; ++r) {
    for (uint16_t c = 0; c < tile.edge; ++c) {
      const bool even = ((r + c) % 2) == 0;
      tile.set(r, c, 0, even ? depth_even : depth_odd);
      tile.set(r, c, 1, even ? unc_even : unc_odd);
    }
  }
  mtrs::saveTile<double>(
    tile, (dir / mtrs::tileFilename(grid)).string(),
    {std::optional<double>(kNaN), std::optional<double>(kNaN)});
}

// The four fine siblings that tile one parent at the test site.
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

// Load the single L(kFineLevel-1) parent tile the four fine siblings fold into.
mtrs::TiledRasterTile<double> loadParent(const fs::path & layer_dir)
{
  const gggs::GridIndex parent = gggs::parent(fineSiblings().front());
  return mtrs::loadTile<double>(
    (layer_dir / "overviews" / mtrs::tileFilename(parent)).string(),
    gggs::Level(kFineLevel - 1), 2);
}

geographic_msgs::msg::GeoPoint geoPoint(double lat, double lon)
{
  geographic_msgs::msg::GeoPoint p;
  p.latitude = lat;
  p.longitude = lon;
  p.altitude = 0.0;
  return p;
}

}  // namespace

// --- argument parsing -------------------------------------------------------

TEST(ParseDepthOverviewArgs, ValidArgsPopulateOptions)
{
  char a0[] = "prog", a1[] = "/store/draft", a2[] = "--min-level", a3[] = "8";
  char * argv[] = {a0, a1, a2, a3};
  mbs::DepthOverviewOptions opts;
  EXPECT_EQ(mbs::parseDepthOverviewArgs(4, argv, opts), mbs::DepthArgStatus::kOk);
  EXPECT_EQ(opts.layer_dir, "/store/draft");
  EXPECT_EQ(opts.min_level, 8);
}

TEST(ParseDepthOverviewArgs, DefaultsWhenOnlyLayerGiven)
{
  char a0[] = "prog", a1[] = "/store/processed";
  char * argv[] = {a0, a1};
  mbs::DepthOverviewOptions opts;
  EXPECT_EQ(mbs::parseDepthOverviewArgs(2, argv, opts), mbs::DepthArgStatus::kOk);
  EXPECT_EQ(opts.min_level, 0);
  EXPECT_FALSE(opts.dry_run);
}

TEST(ParseDepthOverviewArgs, HelpRequested)
{
  char a0[] = "prog", a1[] = "--help";
  char * argv[] = {a0, a1};
  mbs::DepthOverviewOptions opts;
  EXPECT_EQ(mbs::parseDepthOverviewArgs(2, argv, opts), mbs::DepthArgStatus::kHelp);
}

TEST(ParseDepthOverviewArgs, NonNumericLevelIsErrorNotSilentZero)
{
  char a0[] = "prog", a1[] = "/store/draft", a2[] = "--min-level", a3[] = "abc";
  char * argv[] = {a0, a1, a2, a3};
  mbs::DepthOverviewOptions opts;
  EXPECT_EQ(mbs::parseDepthOverviewArgs(4, argv, opts), mbs::DepthArgStatus::kError);
}

// `--fine-level` was DELETED by #331, not retained as an assertion: the builder
// discovers the layer's native levels, so declaring one is meaningless for a
// mixed-level layer. It must now be rejected as an unknown flag rather than
// silently ignored — a stale invocation has to fail loudly, not quietly build
// something other than what it asked for.
TEST(ParseDepthOverviewArgs, FineLevelFlagIsRejected)
{
  char a0[] = "prog", a1[] = "/store/draft", a2[] = "--fine-level", a3[] = "13";
  char * argv[] = {a0, a1, a2, a3};
  mbs::DepthOverviewOptions opts;
  EXPECT_EQ(mbs::parseDepthOverviewArgs(4, argv, opts), mbs::DepthArgStatus::kError);
}

// An out-of-range --min-level is still a usage error. The no-upsample check
// (min_level below the layer's FINEST DISCOVERED native level) cannot live here
// any more — argv alone does not know the layer's levels — so it throws from the
// build instead; see NoUpsampleMinLevelAtOrAboveFineThrows.
TEST(ParseDepthOverviewArgs, OutOfRangeMinLevelIsError)
{
  char a0[] = "prog", a1[] = "/store/draft", a2[] = "--min-level", a3[] = "21";
  char * argv[] = {a0, a1, a2, a3};
  char * argv_negative[] = {a0, a1, a2, a3};
  char neg[] = "-1";
  argv_negative[3] = neg;
  mbs::DepthOverviewOptions opts;
  EXPECT_EQ(mbs::parseDepthOverviewArgs(4, argv, opts), mbs::DepthArgStatus::kError);
  EXPECT_EQ(
    mbs::parseDepthOverviewArgs(4, argv_negative, opts), mbs::DepthArgStatus::kError);
}

// --- shallowest-preserving fold + pair coherence ----------------------------

// The coarse cell must carry the SHOALEST child's depth (maximum ellipsoidal
// height), never a mean. even = shallow (-5.0), odd = deep (-12.0); every folded
// parent cell must read the shallow depth.
TEST(DepthOverviewFold, ShallowestPreservingSelectsMaxHeight)
{
  ScratchDir dir("shallowest");
  for (const auto & g : fineSiblings()) {
    writeCheckerDepthTile(dir.path(), g, /*even*/ -5.0, 0.5, /*odd*/ -12.0, 9.0);
  }

  mbs::DepthOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = kFineLevel - 1;   // just the L(fine-1) parent

  const mbs::DepthOverviewBuildResult r = mbs::buildDepthOverviewPyramid(opts);
  EXPECT_TRUE(r.sidecar_replaced);
  EXPECT_EQ(r.tiles_skipped, 0u);

  const auto tile = loadParent(dir.path());
  std::size_t covered = 0;
  for (uint16_t row = 0; row < tile.edge; ++row) {
    for (uint16_t col = 0; col < tile.edge; ++col) {
      if (std::isnan(tile.get(row, col, 0))) {continue;}
      ++covered;
      EXPECT_EQ(tile.get(row, col, 0), -5.0) <<
        "coarse cell must carry the shoalest (max-height) child depth";
    }
  }
  EXPECT_EQ(covered, static_cast<std::size_t>(tile.edge) * tile.edge) <<
    "four full children must fold into a fully-covered parent";
}

// The selected pair travels together: the coarse cell's uncertainty must be the
// σ that belongs to the shoalest child's depth — never the deep cell's σ and never
// a mean. Here the WINNER is the odd cell (depth -2.0), to prove selection is by
// depth, not by parity/position, and its σ (0.25) must survive verbatim.
TEST(DepthOverviewFold, DepthUncertaintyPairStaysCoherent)
{
  ScratchDir dir("paircoherence");
  for (const auto & g : fineSiblings()) {
    writeCheckerDepthTile(dir.path(), g, /*even*/ -20.0, 3.0, /*odd*/ -2.0, 0.25);
  }

  mbs::DepthOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = kFineLevel - 1;
  mbs::buildDepthOverviewPyramid(opts);

  const auto tile = loadParent(dir.path());
  for (uint16_t row = 0; row < tile.edge; ++row) {
    for (uint16_t col = 0; col < tile.edge; ++col) {
      if (std::isnan(tile.get(row, col, 0))) {continue;}
      ASSERT_EQ(tile.get(row, col, 0), -2.0) << "shoalest depth (odd cell) wins";
      // The σ paired with -2.0 is 0.25 — not the deep cell's 3.0, and not a mean
      // (which would be (0.25 + 3.0) / 2 = 1.625, or a count-weighted 1.625).
      ASSERT_EQ(tile.get(row, col, 1), 0.25) <<
        "the winning depth's own uncertainty must travel with it";
    }
  }
}

// --- fold determinism (order independence) ----------------------------------

// The shallowest-preserving fold must be a function of the contributor SET, not
// its order. Contributors reach a parent cell GEOGRAPHICALLY (~4 per cell, from
// possibly different child tiles), and per-bucket order derives from the fold
// engine's filesystem-iteration order (overview_builder.hpp) — not guaranteed. An
// equal-depth / different-σ (or NaN-σ) tie must therefore break deterministically,
// or the sidecar would be non-idempotent. Every permutation of a contributor set
// must fold to one identical {depth, σ}.
TEST(DepthOverviewFold, FoldIsOrderIndependent)
{
  struct Case
  {
    const char * name;
    std::vector<std::vector<double>> contributors;   // each {depth, σ}
    std::vector<double> expected;                    // {depth, σ}
  };
  const std::vector<Case> cases = {
    // Equal-depth tie broken by the smaller σ (the more reliable pair), never by
    // enumeration order. Order the input so a keep-first tie-break would pick the
    // wrong (σ = 2.0) pair.
    {"equal-depth-different-sigma",
      {{-5.0, 2.0}, {-8.0, 1.0}, {-5.0, 0.5}}, {-5.0, 0.5}},
    // Equal-depth tie where one contender's σ is the NaN no-data sentinel: the
    // finite σ is preferred over NaN.
    {"equal-depth-finite-sigma-beats-nan-sigma",
      {{-5.0, kNaN}, {-9.0, 3.0}, {-5.0, 1.5}}, {-5.0, 1.5}},
    // The shoalest pair legitimately carries a NaN σ (real depth, unknown
    // uncertainty): it still wins on depth and its NaN σ travels through.
    {"shoalest-carries-nan-sigma",
      {{-5.0, kNaN}, {-8.0, 1.0}}, {-5.0, kNaN}},
    // All σ NaN at equal depth: the pair is identical whichever contributor wins.
    {"equal-depth-all-nan-sigma",
      {{-5.0, kNaN}, {-5.0, kNaN}}, {-5.0, kNaN}},
  };

  for (const auto & tc : cases) {
    // Permute by INDEX — the contributor vectors may contain NaN, so ordering the
    // vectors themselves with operator< would be undefined.
    std::vector<std::size_t> order(tc.contributors.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    do {
      std::vector<std::vector<double>> permuted;
      permuted.reserve(order.size());
      for (const std::size_t i : order) {
        permuted.push_back(tc.contributors[i]);
      }

      const std::vector<double> got = mbs::detail::depthShallowestFold(permuted);
      ASSERT_EQ(got.size(), 2u) << tc.name;
      EXPECT_DOUBLE_EQ(got[0], tc.expected[0]) <<
        tc.name << ": depth must be order-independent";
      if (std::isnan(tc.expected[1])) {
        EXPECT_TRUE(std::isnan(got[1])) <<
          tc.name << ": the winning pair's NaN σ must travel through";
      } else {
        EXPECT_DOUBLE_EQ(got[1], tc.expected[1]) <<
          tc.name << ": paired σ must be order-independent";
      }
    } while (std::next_permutation(order.begin(), order.end()));
  }
}

// --- no-upsample invariant --------------------------------------------------

// min_level == the finest DISCOVERED native level (and finer) is refused: the
// builder never produces a level equal to or finer than the native tiles. The
// check moved from parse-time to build-time when --fine-level was deleted, so
// this pins that it still fires — and still as std::invalid_argument.
TEST(DepthOverviewFold, NoUpsampleMinLevelAtOrAboveFineThrows)
{
  ScratchDir dir("noupsample_arg");
  for (const auto & g : fineSiblings()) {
    writeUniformDepthTile(dir.path(), g, -5.0, 0.5);
  }
  mbs::DepthOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = kFineLevel;   // not below fine -> would be "upsample"
  EXPECT_THROW(mbs::buildDepthOverviewPyramid(opts), std::invalid_argument);
}

// A build only ever writes parent (coarser) tiles: level fine-1 down to min_level,
// never a tile at the fine level or finer inside the sidecar.
TEST(DepthOverviewFold, BuildProducesOnlyCoarserLevels)
{
  ScratchDir dir("onlycoarser");
  for (const auto & g : fineSiblings()) {
    writeUniformDepthTile(dir.path(), g, -5.0, 0.5);
  }
  mbs::DepthOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = kFineLevel - 2;   // build L(fine-1) and L(fine-2)
  mbs::buildDepthOverviewPyramid(opts);

  const fs::path overviews = dir.path() / "overviews";
  ASSERT_TRUE(fs::is_directory(overviews));
  EXPECT_EQ(countLevelFiles(overviews, kFineLevel - 1), 1u);
  EXPECT_EQ(countLevelFiles(overviews, kFineLevel - 2), 1u);
  // Never a fine-level or finer tile in the sidecar (no upsample).
  EXPECT_EQ(countLevelFiles(overviews, kFineLevel), 0u);
  EXPECT_EQ(countLevelFiles(overviews, kFineLevel + 1), 0u);
}

// --- NaN no-data gate -------------------------------------------------------

// A child whose depth band is NaN is the no-data sentinel: it must not contribute.
// even cells carry a real depth, odd cells are NaN — every folded parent cell must
// read the real depth (the NaN cells are simply excluded, not averaged in).
TEST(DepthOverviewFold, NaNDepthCellsDoNotContribute)
{
  ScratchDir dir("nangate");
  for (const auto & g : fineSiblings()) {
    writeCheckerDepthTile(dir.path(), g, /*even*/ -8.0, 1.0, /*odd NaN*/ kNaN, kNaN);
  }
  mbs::DepthOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = kFineLevel - 1;
  mbs::buildDepthOverviewPyramid(opts);

  const auto tile = loadParent(dir.path());
  for (uint16_t row = 0; row < tile.edge; ++row) {
    for (uint16_t col = 0; col < tile.edge; ++col) {
      // Two real + two NaN contributors per parent cell: the real depth survives,
      // its σ intact; a fold that let NaN in would produce NaN (NaN>anything is
      // false, so a NaN seed would never be replaced).
      ASSERT_EQ(tile.get(row, col, 0), -8.0);
      ASSERT_EQ(tile.get(row, col, 1), 1.0);
    }
  }
}

// A parent all of whose contributors are no-data stays no-data: an all-NaN set of
// children yields a parent tile that is entirely NaN (no cell fabricated).
TEST(DepthOverviewFold, AllNaNChildrenYieldAllNaNParent)
{
  ScratchDir dir("allnan");
  for (const auto & g : fineSiblings()) {
    writeUniformDepthTile(dir.path(), g, kNaN, kNaN);
  }
  mbs::DepthOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = kFineLevel - 1;
  mbs::buildDepthOverviewPyramid(opts);

  const auto tile = loadParent(dir.path());
  for (uint16_t row = 0; row < tile.edge; ++row) {
    for (uint16_t col = 0; col < tile.edge; ++col) {
      EXPECT_TRUE(std::isnan(tile.get(row, col, 0)));
      EXPECT_TRUE(std::isnan(tile.get(row, col, 1)));
    }
  }
}

// --- malformed filename -> skip -> swap refusal -----------------------------

// A fine tile whose name cannot be reconstructed must skip loudly and, because its
// coverage is then missing from the pyramid, REFUSE the swap — a partial pyramid
// must never displace a previously-complete sidecar.
TEST(DepthOverviewFold, MalformedTileNameSkipsAndRefusesSwap)
{
  ScratchDir dir("malformed");
  for (const auto & g : fineSiblings()) {
    writeUniformDepthTile(dir.path(), g, -5.0, 0.5);
  }
  // A row field that overflows std::stoul -> skipped loudly, not an uncaught throw.
  std::ofstream(dir.path() / "13_99999999999999999999_1.tif") << "overflow";

  // A previously-complete sidecar that must survive the refused build.
  const fs::path overviews = dir.path() / "overviews";
  fs::create_directories(overviews);
  const fs::path sentinel = overviews / "12_1_1.tif";
  std::ofstream(sentinel) << "keep";

  mbs::DepthOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = kFineLevel - 1;

  mbs::DepthOverviewBuildResult r;
  ASSERT_NO_THROW(r = mbs::buildDepthOverviewPyramid(opts));
  EXPECT_EQ(r.tiles_skipped, 1u);
  EXPECT_FALSE(r.sidecar_replaced);
  EXPECT_TRUE(fs::exists(sentinel)) << "a partial pyramid must not be swapped in";
  EXPECT_FALSE(fs::exists(dir.path() / "overviews.tmp"));
}

// --- end to end -------------------------------------------------------------

// Full build: sidecar structure, a value-idempotent re-run, and the staging-dir
// run lock (a leftover overviews.tmp/ refuses the next run, untouched).
TEST(DepthOverviewFold, EndToEndSidecarSwapIdempotentAndLocked)
{
  ScratchDir dir("endtoend");
  for (const auto & g : fineSiblings()) {
    writeUniformDepthTile(dir.path(), g, -7.5, 1.25);
  }
  mbs::DepthOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = kFineLevel - 1;

  const mbs::DepthOverviewBuildResult first = mbs::buildDepthOverviewPyramid(opts);
  EXPECT_GT(first.tiles_written, 0u);
  EXPECT_TRUE(first.sidecar_replaced);
  EXPECT_FALSE(fs::exists(dir.path() / "overviews.tmp")) <<
    "a successful build must not leave staging behind";
  const auto a = loadParent(dir.path());

  // Idempotent: regenerating the whole sidecar yields identical values.
  mbs::buildDepthOverviewPyramid(opts);
  const auto b = loadParent(dir.path());
  EXPECT_EQ(a.band(0), b.band(0)) << "fold must be value-idempotent";
  EXPECT_EQ(a.band(1), b.band(1));

  // The staging dir is the per-layer run lock: a leftover overviews.tmp/ (crashed
  // run or concurrent build) refuses the next run and is left untouched.
  const fs::path staging = dir.path() / "overviews.tmp";
  fs::create_directories(staging);
  const fs::path debris = staging / "12_1_1.tif";
  std::ofstream(debris) << "in-flight";
  EXPECT_THROW(mbs::buildDepthOverviewPyramid(opts), std::runtime_error);
  EXPECT_TRUE(fs::exists(debris)) << "the other run's staging dir must survive";
}

// The filename-only layer guard opens one tile: a layer whose tiles are not the
// 2-band depth shape must not be wiped and rebuilt with the depth policy.
TEST(DepthOverviewFold, RefusesLayerWhoseTilesAreNotTheDepthBandShape)
{
  ScratchDir dir("wrongshape");
  for (const auto & g : fineSiblings()) {
    mtrs::TiledRasterTile<double> tile(g, 3, 0.0);   // not the 2-band depth shape
    mtrs::saveTile<double>(
      tile, (dir.path() / mtrs::tileFilename(g)).string(),
      {std::nullopt, std::nullopt, std::nullopt});
  }
  const fs::path overviews = dir.path() / "overviews";
  fs::create_directories(overviews);
  const fs::path sentinel = overviews / "12_1_1.tif";
  std::ofstream(sentinel) << "keep";

  mbs::DepthOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = kFineLevel - 1;

  EXPECT_THROW(mbs::buildDepthOverviewPyramid(opts), std::runtime_error);
  EXPECT_TRUE(fs::exists(sentinel));
  EXPECT_FALSE(fs::exists(dir.path() / "overviews.tmp"));
}

// --- loader silent skip of the overviews/ sidecar ---------------------------

// The flat-layout loader must skip `overviews/` (and its swap transients)
// SILENTLY — the sidecar is an expected part of a draft/processed layer dir, not a
// stale epoch subdir. No WARNING, and the fine tile still loads (ADR-0011).
TEST(TileIoLoader, SilentlySkipsOverviewsSidecar)
{
  ScratchDir store("loader_silent");
  const fs::path processed = store.path() / mbs::layerDirName(mbs::SourceLayer::Processed);
  fs::create_directories(processed);
  const gggs::GridIndex fine = fineSiblings().front();
  writeUniformDepthTile(processed, fine, -6.0, 0.75);

  // A realistic sidecar plus its crash-safe swap transients — all must be skipped
  // silently, not warned about.
  for (const char * sub : {"overviews", "overviews.tmp", "overviews.old"}) {
    fs::create_directories(processed / sub);
    writeUniformDepthTile(processed / sub, gggs::parent(fine), -6.0, 0.75);
  }

  {
    CerrCapture cap;
    mbs::BathymetryStore load_store(kFineLevel);
    const std::size_t n = mbs::load(load_store, store.path().string());
    EXPECT_EQ(n, 1u) << "only the fine tile loads; the sidecar is skipped";
    EXPECT_EQ(cap.str().find("WARNING"), std::string::npos) <<
      "overviews/ must be skipped silently, not warned about:\n" << cap.str();
  }
  {
    CerrCapture cap;
    mbs::BathymetryStore window_store(kFineLevel);
    const std::size_t n = mbs::loadWindow(
      window_store, store.path().string(),
      geoPoint(-85.0, -180.0), geoPoint(85.0, 180.0));
    EXPECT_EQ(n, 1u);
    EXPECT_EQ(cap.str().find("WARNING"), std::string::npos) <<
      "loadWindow must also skip overviews/ silently:\n" << cap.str();
  }
}

// Guard against an over-broad skip: a genuinely-unexpected subdirectory (an old
// epoch-layout dir) must STILL warn — the silent skip is scoped to the sidecar.
TEST(TileIoLoader, StillWarnsOnUnexpectedSubdir)
{
  ScratchDir store("loader_warn");
  const fs::path processed = store.path() / mbs::layerDirName(mbs::SourceLayer::Processed);
  fs::create_directories(processed);
  writeUniformDepthTile(processed, fineSiblings().front(), -6.0, 0.75);
  fs::create_directories(processed / "epoch_2020");   // not a sidecar

  CerrCapture cap;
  mbs::BathymetryStore load_store(kFineLevel);
  mbs::load(load_store, store.path().string());
  EXPECT_NE(cap.str().find("WARNING"), std::string::npos) <<
    "an unexpected subdir must still warn";
  EXPECT_NE(cap.str().find("epoch_2020"), std::string::npos);
}

// --- #331: mixed-level, native-wins ------------------------------------------

namespace
{

// The fine band of the Isles of Shoals `reference/` shape in miniature: the
// level-13 children of one level-12 grid, nested entirely inside a coarser
// compiled band. Kept to one parent's worth of tiles on purpose — each tile is a
// real 960x960 two-band GeoTIFF, so a wider fixture buys nothing the assertions
// use and costs seconds of I/O.
std::vector<gggs::GridIndex> fineBandUnder(const gggs::GridIndex & l12)
{
  return gggs::children(l12);
}

std::size_t countSidecarLevel(const fs::path & layer_dir, int level)
{
  const fs::path overviews = layer_dir / "overviews";
  return fs::is_directory(overviews) ? countLevelFiles(overviews, level) : 0u;
}

}  // namespace

// THE CASE THAT REFUSED ITS OWN SWAP BEFORE #331. When every parent at a level
// is already native, that level writes ZERO derived tiles — and it is
// nevertheless fully covered. The pre-#331 rule ("a level that produced nothing
// means the chain is broken") aborted the whole build there, which is exactly
// what the staged Shoals `reference/` layer does: all ten level-6 ancestors of
// its level-8 harbour band are native.
TEST(MixedLevelPyramid, LevelCoveredEntirelyByNativeTilesStillSwaps)
{
  ScratchDir dir("native_covered_level");
  const gggs::GridIndex l12 = gggs::Level(12).gridIndex(kLat, kLon);
  for (const gggs::GridIndex & g : fineBandUnder(l12)) {
    writeUniformDepthTile(dir.path(), g, -9.0, 0.5);      // the fine band
  }
  writeUniformDepthTile(dir.path(), l12, -30.0, 4.0);      // the compiled band

  mbs::DepthOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = 11;

  const mbs::DepthOverviewBuildResult r = mbs::buildDepthOverviewPyramid(opts);
  EXPECT_TRUE(r.sidecar_replaced) <<
    "a level covered entirely by native tiles must not refuse the swap";
  EXPECT_FALSE(r.level_uncovered);
  EXPECT_EQ(r.tiles_skipped, 0u);
  EXPECT_EQ(r.native_levels, (std::vector<int>{12, 13}));
  // Level 12 is entirely native, so nothing is written there and its parent-slot
  // is counted as suppressed instead.
  EXPECT_EQ(countSidecarLevel(dir.path(), 12), 0u) <<
    "native tiles must never be overwritten by derived ones";
  EXPECT_EQ(r.tiles_suppressed_by_native, 1u);
  EXPECT_EQ(r.derived_by_level.count(12), 0u);
  // Level 11 has no native tile, so it IS derived — from the native level-12
  // tiles, since that level's derived set is empty.
  EXPECT_EQ(countSidecarLevel(dir.path(), 11), 1u);
  EXPECT_EQ(r.derived_by_level.at(11), 1u);
  EXPECT_EQ(r.tiles_written, 1u);
}

// An intermediate level holding no native tiles is derived (the Shoals level-7
// case: 25 parents of the harbour band, with no native level 7 anywhere), while
// the coarser level that IS native is left alone.
TEST(MixedLevelPyramid, IntermediateLevelIsDerivedAndCoarseNativeIsLeftAlone)
{
  ScratchDir dir("intermediate_derived");
  const gggs::GridIndex l11 = gggs::Level(11).gridIndex(kLat, kLon);
  const gggs::GridIndex l12 = gggs::Level(12).gridIndex(kLat, kLon);
  for (const gggs::GridIndex & g : fineBandUnder(l12)) {
    writeUniformDepthTile(dir.path(), g, -9.0, 0.5);
  }
  writeUniformDepthTile(dir.path(), l11, -30.0, 4.0);   // native at 11, none at 12

  mbs::DepthOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = 11;

  const mbs::DepthOverviewBuildResult r = mbs::buildDepthOverviewPyramid(opts);
  ASSERT_TRUE(r.sidecar_replaced);
  EXPECT_EQ(r.native_levels, (std::vector<int>{11, 13}));
  EXPECT_EQ(countSidecarLevel(dir.path(), 12), 1u) <<
    "level 12 holds no native tiles, so it is filled from the fine band";
  EXPECT_EQ(countSidecarLevel(dir.path(), 11), 0u);
  EXPECT_EQ(r.tiles_suppressed_by_native, 1u);
}

// Two region-disjoint native bands at different levels (the S-102 x GRANIT
// shape). Every level from just under the finest down to min_level must be
// covered — neither band may vanish, and the coarse band's own ground must not
// be blank at levels the fine band alone cannot reach.
TEST(MixedLevelPyramid, RegionDisjointBandsAreBothCarriedDown)
{
  ScratchDir dir("region_disjoint");
  const gggs::GridIndex fine_root = gggs::Level(12).gridIndex(kLat, kLon);
  for (const gggs::GridIndex & g : gggs::children(fine_root)) {
    writeUniformDepthTile(dir.path(), g, -9.0, 0.5);        // fine band at L13
  }
  // A second region far away, native at a coarser level only.
  const gggs::GridIndex coarse = gggs::Level(10).gridIndex(40.0, -74.0);
  writeUniformDepthTile(dir.path(), coarse, -25.0, 3.0);
  ASSERT_NE(gggs::Level(10).gridIndex(kLat, kLon), coarse) <<
    "the two bands must sit on genuinely disjoint ground";

  mbs::DepthOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = 8;

  const mbs::DepthOverviewBuildResult r = mbs::buildDepthOverviewPyramid(opts);
  ASSERT_TRUE(r.sidecar_replaced);
  EXPECT_FALSE(r.level_uncovered);
  EXPECT_EQ(r.native_levels, (std::vector<int>{10, 13}));
  // The fine band alone covers 12 and 11; at 10 the coarse band is native, so the
  // fine band's own level-10 parent is derived beside it. Below that both fold
  // together, and each level must hold BOTH regions.
  EXPECT_EQ(countSidecarLevel(dir.path(), 12), 1u);
  EXPECT_EQ(countSidecarLevel(dir.path(), 11), 1u);
  EXPECT_EQ(countSidecarLevel(dir.path(), 10), 1u) <<
    "the fine band's level-10 parent is derived; the coarse band's is native";
  EXPECT_EQ(countSidecarLevel(dir.path(), 9), 2u) << "both regions at level 9";
  EXPECT_EQ(countSidecarLevel(dir.path(), 8), 2u) << "both regions at level 8";
}

// The 2-band depth-shape probe must open ONE TILE PER DISCOVERED LEVEL. A layer
// whose finest level is the right shape but whose coarse level is not is still
// not a depth layer, and must not be rebuilt with the depth policy.
TEST(MixedLevelPyramid, BandShapeProbeCoversEveryDiscoveredLevel)
{
  ScratchDir dir("probe_every_level");
  const gggs::GridIndex l12 = gggs::Level(12).gridIndex(kLat, kLon);
  for (const gggs::GridIndex & g : fineBandUnder(l12)) {
    writeUniformDepthTile(dir.path(), g, -9.0, 0.5);   // correct 2-band shape
  }
  {
    mtrs::TiledRasterTile<double> tile(l12, 3, 0.0);   // wrong shape at level 12
    mtrs::saveTile<double>(
      tile, (dir.path() / mtrs::tileFilename(l12)).string(),
      {std::nullopt, std::nullopt, std::nullopt});
  }

  const fs::path overviews = dir.path() / "overviews";
  fs::create_directories(overviews);
  const fs::path sentinel = overviews / "10_1_1.tif";
  std::ofstream(sentinel) << "keep";

  mbs::DepthOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = 10;

  EXPECT_THROW(mbs::buildDepthOverviewPyramid(opts), std::runtime_error);
  EXPECT_TRUE(fs::exists(sentinel));
  EXPECT_FALSE(fs::exists(dir.path() / "overviews.tmp"));
}

// The mis-pointed-path guard, generalised: with --fine-level gone, the refusal is
// "no usable native tiles at ANY level under <dir>". An empty or wrong directory
// must still never destroy a good sidecar.
TEST(MixedLevelPyramid, EmptyLayerRefusesWithoutTouchingTheSidecar)
{
  ScratchDir dir("empty_layer");
  const fs::path overviews = dir.path() / "overviews";
  fs::create_directories(overviews);
  const fs::path sentinel = overviews / "12_1_1.tif";
  std::ofstream(sentinel) << "keep";

  mbs::DepthOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = 0;

  EXPECT_THROW(mbs::buildDepthOverviewPyramid(opts), std::runtime_error);
  EXPECT_TRUE(fs::exists(sentinel));
  EXPECT_FALSE(fs::exists(dir.path() / "overviews.tmp"));
}

// --- #331: derived coverage manifest + geometric error -----------------------

// uma-ADR-0013 D3: the derived coverage is written INTO STAGING, so it rides
// uma-ADR-0011's rename-aside and is crash-consistent with the sidecar it
// describes. D1/D2: every derived tile carries a saturated conservative
// geometric error.
TEST(DerivedCoverageManifest, RidesTheSwapAndCarriesSaturatedGeometricError)
{
  ScratchDir dir("derived_manifest");
  const gggs::GridIndex l11 = gggs::Level(11).gridIndex(kLat, kLon);
  const gggs::GridIndex l12 = gggs::Level(12).gridIndex(kLat, kLon);
  for (const gggs::GridIndex & g : fineBandUnder(l12)) {
    writeUniformDepthTile(dir.path(), g, -9.0, 0.5);
  }

  mbs::DepthOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = 11;
  const mbs::DepthOverviewBuildResult r = mbs::buildDepthOverviewPyramid(opts);
  ASSERT_TRUE(r.sidecar_replaced);

  const std::string manifest_path =
    (dir.path() / "overviews" / mtrs::coverageManifestFilename()).string();
  ASSERT_TRUE(fs::exists(manifest_path)) <<
    "the derived manifest must land inside overviews/, not beside it";
  const std::optional<mtrs::CoverageManifest> manifest =
    mtrs::loadCoverageManifest(manifest_path);
  ASSERT_TRUE(manifest.has_value());
  EXPECT_EQ(manifest->countAt(12), 1u);
  EXPECT_EQ(manifest->countAt(11), 1u);

  // Saturation (uma-ADR-0013 D2): a parent's error is at least its children's,
  // and where nothing records a finer error it degenerates to the level's own
  // nominal ground sample distance — today's level-as-resolution behaviour.
  ASSERT_TRUE(manifest->geometricError(l12).has_value());
  ASSERT_TRUE(manifest->geometricError(l11).has_value());
  EXPECT_DOUBLE_EQ(*manifest->geometricError(l12), gggs::levels[12].nominal_cell_size);
  EXPECT_DOUBLE_EQ(*manifest->geometricError(l11), gggs::levels[11].nominal_cell_size);
  EXPECT_GT(*manifest->geometricError(l11), *manifest->geometricError(l12)) <<
    "a coarser tile's error must dominate its children's";
}

// The NATIVE coverage is deliberately NOT persisted by this builder: it does not
// own the native tiles, so a file it wrote would go stale on the next import
// with nothing able to notice (the scan fallback fires on absence, not
// staleness). Only the derived manifest is written.
TEST(DerivedCoverageManifest, NativeCoverageIsNotPersisted)
{
  ScratchDir dir("no_native_manifest");
  for (const auto & g : fineSiblings()) {
    writeUniformDepthTile(dir.path(), g, -5.0, 0.5);
  }
  mbs::DepthOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = kFineLevel - 1;
  ASSERT_TRUE(mbs::buildDepthOverviewPyramid(opts).sidecar_replaced);

  EXPECT_FALSE(fs::exists(dir.path() / mtrs::coverageManifestFilename())) <<
    "the builder must not write a native coverage.json it cannot keep fresh";
}

// The saturation formula itself (uma-ADR-0013 D1/D2), directly: max(level GSD,
// max child ε), with an absent child ε standing in as the child level's GSD.
TEST(GeometricError, SaturatesOverChildrenAndFallsBackToLevelGsd)
{
  const double l12_gsd = gggs::levels[12].nominal_cell_size;
  const double l11_gsd = gggs::levels[11].nominal_cell_size;

  // No child records an error (every child native): the level's own GSD wins,
  // because GSD is monotone in level.
  EXPECT_DOUBLE_EQ(
    mbs::detail::saturatedGeometricError(11, 12, {std::nullopt, std::nullopt}),
    l11_gsd);
  // A child with a modest recorded error does not displace the level's GSD.
  EXPECT_DOUBLE_EQ(
    mbs::detail::saturatedGeometricError(11, 12, {l12_gsd, std::nullopt}), l11_gsd);
  // A child with a LARGER error does: saturation is what makes top-down
  // refinement correct.
  EXPECT_DOUBLE_EQ(
    mbs::detail::saturatedGeometricError(11, 12, {l11_gsd * 3.0, l12_gsd}),
    l11_gsd * 3.0);
}

// --- #331: single-level regression pin ---------------------------------------

// draft/processed behaviour must be preserved through the mixed-level
// generalisation. The reference is a digest captured from the PRE-#331 binary
// (see test/data/depth_overview_single_level_golden.txt) — a post-change test
// cannot produce "the pre-change build", so it is committed as data.
//
// The fixture inputs are digested too, and asserted FIRST: if the fixture
// generator drifts, that assertion fails and says so, instead of the sidecar
// assertion failing and being mistaken for a fold regression.
TEST(SingleLevelRegression, SidecarMatchesThePreChangeGolden)
{
  namespace dor = depth_overview_regression;
  ScratchDir dir("regression_pin");
  dor::writeFixture(dir.path());

  const std::string golden_inputs =
    dor::readGolden(DEPTH_OVERVIEW_GOLDEN_FILE, "input");
  ASSERT_FALSE(golden_inputs.empty()) <<
    "golden data file missing or unreadable: " << DEPTH_OVERVIEW_GOLDEN_FILE;
  ASSERT_EQ(dor::digestTileDir(dir.path(), "input"), golden_inputs) <<
    "the pinned fixture drifted — the golden below it no longer describes this "
    "input, so regenerate both from the pre-change binary before trusting it";

  mbs::DepthOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = dor::kFixtureMinLevel;
  const mbs::DepthOverviewBuildResult r = mbs::buildDepthOverviewPyramid(opts);
  ASSERT_TRUE(r.sidecar_replaced);
  EXPECT_EQ(r.tiles_suppressed_by_native, 0u) <<
    "a single-level layer has no native tile at any coarser level";

  const std::string golden_sidecar =
    dor::readGolden(DEPTH_OVERVIEW_GOLDEN_FILE, "sidecar");
  ASSERT_FALSE(golden_sidecar.empty());
  EXPECT_EQ(dor::digestTileDir(dir.path() / "overviews", "sidecar"), golden_sidecar) <<
    "the single-level pyramid changed: same tile names, same per-band values is "
    "the contract draft/ and processed/ depend on";
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
