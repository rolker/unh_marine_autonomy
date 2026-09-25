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

// [uma#397 / docs/world_store_design.md §7, spine decision 2] Tests for the
// rev-3 MULTI-BAND depth overview writer: the MIN/MEAN/COUNT/σ fold, the batch
// pyramid, the per-parent work unit, and the guards that keep the two tile
// schemas from being written over one another.
//
// The load-bearing assertion is MinBandMatchesTheLegacyGolden: the MIN band of
// a multi-band pyramid must be bit-identical to the single-band pyramid's depth
// band over the same inputs, because "shoalest" is the same selection in both.
// It is checked against the SAME committed golden digest the single-band
// regression guard uses — a value the pre-#331 binary produced — so the two
// writers are pinned to one reference rather than to each other.

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "depth_overview_regression_fixture.hpp"
#include "marine_autonomy/gggs.h"
#include "marine_autonomy/gggs/index_math.h"
#include "marine_bathymetry_store/overview_pyramid.hpp"
#include "marine_tiled_raster_store/coverage_manifest.hpp"
#include "marine_tiled_raster_store/tile_io.hpp"
#include "marine_tiled_raster_store/tiled_raster_tile.hpp"

namespace
{

namespace fs = std::filesystem;
namespace mtrs = marine_tiled_raster_store;
namespace mbs = marine_bathymetry_store;
namespace det = marine_bathymetry_store::detail;

constexpr int kFineLevel = 13;                 // a non-polar native level
constexpr double kLat = 43.07, kLon = -71.42;  // Lake Massabesic — non-polar
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr std::size_t kMB = det::kMultiBandCount;

class ScratchDir
{
public:
  explicit ScratchDir(const std::string & name)
  : path_(fs::path(::testing::TempDir()) / ("mbs_mb_ovr_" + name))
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

// A 4-band contributor cell in the multi-band schema.
std::vector<double> cell(double min_v, double mean_v, double count, double sigma)
{
  std::vector<double> c(kMB);
  c[det::kMultiMinBand] = min_v;
  c[det::kMultiMeanBand] = mean_v;
  c[det::kMultiCountBand] = count;
  c[det::kMultiSigmaBand] = sigma;
  return c;
}

// Write a 2-band Float64 native depth tile filled uniformly.
void writeUniformNativeTile(
  const fs::path & dir, const gggs::GridIndex & grid, double depth, double unc)
{
  fs::create_directories(dir);
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

// Write a 4-band Float64 derived-schema tile filled uniformly.
void writeUniformMultiBandTile(
  const fs::path & dir, const gggs::GridIndex & grid, double depth)
{
  fs::create_directories(dir);
  mtrs::TiledRasterTile<double> tile(grid, kMB, kNaN);
  for (uint16_t r = 0; r < tile.edge; ++r) {
    for (uint16_t c = 0; c < tile.edge; ++c) {
      tile.set(r, c, det::kMultiMinBand, depth);
      tile.set(r, c, det::kMultiMeanBand, depth);
      tile.set(r, c, det::kMultiCountBand, 1.0);
    }
  }
  mtrs::saveTile<double>(
    tile, (dir / mtrs::tileFilename(grid)).string(),
    std::vector<std::optional<double>>(kMB, std::optional<double>(kNaN)));
}

std::string readText(const fs::path & path)
{
  std::ifstream in(path);
  return std::string(
    (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<gggs::GridIndex> fineSiblings()
{
  const gggs::GridIndex fine = gggs::Level(kFineLevel).gridIndex(kLat, kLon);
  return gggs::children(gggs::parent(fine));
}

mtrs::TiledRasterTile<double> loadMultiBand(const fs::path & path, int level)
{
  return mtrs::loadTile<double>(
    path.string(), gggs::Level(static_cast<uint8_t>(level)), kMB);
}

// The band-0 half of the committed golden digest, as a map name -> digest, so
// the multi-band sidecar can be compared tile by tile against the value the
// PRE-#331 single-band binary produced.
std::map<std::string, std::string> goldenMinBandDigests()
{
  namespace dor = depth_overview_regression;
  std::map<std::string, std::string> out;
  std::istringstream lines(dor::readGolden(DEPTH_OVERVIEW_GOLDEN_FILE, "sidecar"));
  std::string tag, name, band0, band1;
  while (lines >> tag >> name >> band0 >> band1) {
    out[name] = band0;
  }
  return out;
}

}  // namespace

// --- the fold ---------------------------------------------------------------

TEST(MultiBandFold, MinBandIsTheShoalestOfTheContributors)
{
  // Ellipsoidal height, so shoalest is the MAXIMUM. The design calls this band
  // MIN because it is the min DEPTH; the number stored is the largest height.
  const std::vector<std::vector<double>> contributors{
    cell(-12.0, -12.0, 1.0, 0.4),
    cell(-3.5, -3.5, 1.0, 0.9),
    cell(-20.0, -20.0, 1.0, 0.1)};
  const std::vector<double> folded = det::depthMultiBandFold(contributors);
  EXPECT_DOUBLE_EQ(folded[det::kMultiMinBand], -3.5);
}

TEST(MultiBandFold, MinBandEqualsTheSingleBandFoldsDepth)
{
  // The one property that makes the two pyramids interchangeable for a
  // navigation view: same contributors, same shoalest number.
  const std::vector<std::vector<double>> pairs{
    {-12.0, 0.4}, {-3.5, 0.9}, {-20.0, 0.1}, {-3.5, 0.2}};
  std::vector<std::vector<double>> promoted;
  for (const auto & p : pairs) {
    promoted.push_back(det::promoteNativeDepthCell(p));
  }
  EXPECT_DOUBLE_EQ(
    det::depthMultiBandFold(promoted)[det::kMultiMinBand],
    det::depthShallowestFold(pairs)[0]);
}

TEST(MultiBandFold, MeanIsCountWeightedAndCountSums)
{
  // Two children summarising 3 and 1 native cells: the mean must follow the
  // lineage, not the number of children.
  const std::vector<std::vector<double>> contributors{
    cell(-10.0, -10.0, 3.0, 0.5),
    cell(-2.0, -2.0, 1.0, 0.5)};
  const std::vector<double> folded = det::depthMultiBandFold(contributors);
  EXPECT_DOUBLE_EQ(folded[det::kMultiCountBand], 4.0);
  EXPECT_DOUBLE_EQ(folded[det::kMultiMeanBand], (-10.0 * 3 + -2.0 * 1) / 4.0);
  EXPECT_DOUBLE_EQ(folded[det::kMultiMinBand], -2.0);
}

TEST(MultiBandFold, SigmaIsNodataWhileTheRuleIsUndecided)
{
  // §7's rule is open, so the band is reserved. Writing anything here — most of
  // all a zero — would be a decision taken by code instead of by the design.
  const std::vector<std::vector<double>> contributors{
    cell(-10.0, -10.0, 1.0, 0.5), cell(-2.0, -2.0, 1.0, 0.7)};
  EXPECT_TRUE(
    std::isnan(
      det::depthMultiBandFold(contributors)[det::kMultiSigmaBand]));
  EXPECT_TRUE(
    std::isnan(
      det::depthMultiBandFold(
        contributors, mbs::SigmaFold::kUndecided)[det::kMultiSigmaBand]));
}

TEST(MultiBandFold, CandidateRulesProduceDifferentSigmas)
{
  // The measurement that decides §7 compares these three; if two of them agreed
  // on every input there would be nothing to decide.
  const std::vector<std::vector<double>> contributors{
    cell(-10.0, -10.0, 1.0, 0.2), cell(-2.0, -2.0, 1.0, 0.8)};
  const double max_child =
    det::depthMultiBandFold(
    contributors, mbs::SigmaFold::kMaxChild)[det::kMultiSigmaBand];
  const double mean_child =
    det::depthMultiBandFold(
    contributors, mbs::SigmaFold::kMeanChild)[det::kMultiSigmaBand];
  const double pooled =
    det::depthMultiBandFold(
    contributors, mbs::SigmaFold::kPooled)[det::kMultiSigmaBand];
  EXPECT_DOUBLE_EQ(max_child, 0.8);
  EXPECT_DOUBLE_EQ(mean_child, 0.5);
  // Pooled: within-child (0.2², 0.8²) plus the spread of the means about -6.
  EXPECT_DOUBLE_EQ(pooled, std::sqrt((0.04 + 16.0 + 0.64 + 16.0) / 2.0));
  EXPECT_GT(pooled, max_child) <<
    "pooled must see the spread BETWEEN the children, which neither "
    "per-child statistic can";
}

TEST(MultiBandFold, PooledLeavesChildrenWithoutSigmaOutOfTheSigmaFold)
{
  // Owner decision 2026-09-25 (uma#397): pool only over the children that
  // carry a σ, weighted by their own counts. Regression: a σ-less child was
  // counted as zero within-variance while its count was added, so a
  // 1000-count σ-less child beside a 1-count σ = 1 m child pooled to ~0.03 m.
  const std::vector<std::vector<double>> big_blind_small_known{
    cell(-10.0, -10.0, 1000.0, kNaN), cell(-10.0, -10.0, 1.0, 1.0)};
  EXPECT_DOUBLE_EQ(
    det::depthMultiBandFold(
      big_blind_small_known, mbs::SigmaFold::kPooled)[det::kMultiSigmaBand],
    1.0);

  // The spread term is taken about the mean of the σ-carrying children only:
  // a σ-less child far from them does not widen (or narrow) the band.
  const std::vector<std::vector<double>> mixed{
    cell(-10.0, -10.0, 1.0, 0.2), cell(-2.0, -2.0, 1.0, 0.8),
    cell(-50.0, -50.0, 7.0, kNaN)};
  const std::vector<std::vector<double>> known_only{
    cell(-10.0, -10.0, 1.0, 0.2), cell(-2.0, -2.0, 1.0, 0.8)};
  EXPECT_DOUBLE_EQ(
    det::depthMultiBandFold(mixed, mbs::SigmaFold::kPooled)[det::kMultiSigmaBand],
    det::depthMultiBandFold(
      known_only, mbs::SigmaFold::kPooled)[det::kMultiSigmaBand]);
  // ... while MEAN and COUNT still fold every contributor.
  EXPECT_DOUBLE_EQ(
    det::depthMultiBandFold(mixed, mbs::SigmaFold::kPooled)[det::kMultiCountBand],
    9.0);
}

TEST(MultiBandFold, PooledReAdmitsSigmaLessDataAboveFirstFold)
{
  // PINS A KNOWN LIMIT, not a desired behaviour. kPooled pools correctly at the
  // first fold only: a tile carries no σ-carrier count/mean, so one level up it
  // weights by the COUNT/MEAN bands, which include the σ-less children the
  // first fold left out. Fixing it needs new bands (a schema change) and is
  // deferred to the open σ-rule decision (design §7; owner, 2026-09-25: "I want
  // to think more deeply about uncertainty at some point, so do what's a good
  // placeholder until that happens"). Writers still emit σ as nodata. When the
  // fix lands, this test should change to expect `one_fold` from `two_folds`.
  const std::vector<double> a = cell(0.0, 0.0, 1.0, 1.0);
  const std::vector<double> blind = cell(0.0, 0.0, 1000.0, kNaN);
  const std::vector<double> b = cell(5.0, 5.0, 1.0, 0.1);

  const double one_fold =
    det::depthMultiBandFold({a, blind, b}, mbs::SigmaFold::kPooled)[
    det::kMultiSigmaBand];
  EXPECT_DOUBLE_EQ(one_fold, std::sqrt((1.0 + 6.25 + 0.01 + 6.25) / 2.0));

  const std::vector<double> first =
    det::depthMultiBandFold({a, blind}, mbs::SigmaFold::kPooled);
  EXPECT_DOUBLE_EQ(first[det::kMultiSigmaBand], 1.0);  // first fold: correct
  EXPECT_DOUBLE_EQ(first[det::kMultiCountBand], 1001.0);  // σ-less count kept
  const double two_folds =
    det::depthMultiBandFold({first, b}, mbs::SigmaFold::kPooled)[
    det::kMultiSigmaBand];
  // Current behaviour: the 1000 σ-less cells re-enter via COUNT/MEAN.
  const double mu = 5.0 / 1002.0;
  EXPECT_DOUBLE_EQ(
    two_folds,
    std::sqrt(
      (1001.0 * (1.0 + mu * mu) + 1.0 * (0.01 + (5.0 - mu) * (5.0 - mu))) /
      1002.0));
  EXPECT_LT(two_folds, one_fold) << "the known limit: σ drifts toward zero";
}

TEST(MultiBandFold, SigmaStaysNodataWhenNoContributorCarriesOne)
{
  // No uncertainty information must read as nodata, never as zero uncertainty —
  // the most dangerous number this band could hold.
  const std::vector<std::vector<double>> contributors{
    cell(-10.0, -10.0, 1.0, kNaN), cell(-2.0, -2.0, 1.0, kNaN)};
  for (const mbs::SigmaFold rule : {mbs::SigmaFold::kPooled,
      mbs::SigmaFold::kMaxChild, mbs::SigmaFold::kMeanChild})
  {
    EXPECT_TRUE(
      std::isnan(
        det::depthMultiBandFold(contributors, rule)[det::kMultiSigmaBand])) <<
      "rule " << mbs::sigmaFoldName(rule);
  }
}

TEST(MultiBandFold, MalformedCountAndMeanSubstituteConservatively)
{
  // A NaN COUNT or MEAN means a malformed upstream tile. The parent must stay
  // self-consistent — a COUNT that does not account for an existing MIN would
  // corrupt the lineage every level above reads.
  const std::vector<std::vector<double>> contributors{
    cell(-10.0, kNaN, kNaN, 0.5),   // no mean, no count
    cell(-2.0, -2.0, 1.0, 0.5)};
  const std::vector<double> folded = det::depthMultiBandFold(contributors);
  EXPECT_DOUBLE_EQ(folded[det::kMultiCountBand], 2.0);
  EXPECT_DOUBLE_EQ(folded[det::kMultiMeanBand], -6.0) <<
    "the malformed contributor's MIN stands in for its missing MEAN";
}

TEST(MultiBandFold, IsOrderIndependent)
{
  // The fold engine buckets contributors in filesystem-iteration order, which
  // is not guaranteed; an order-sensitive fold would make the sidecar
  // non-idempotent.
  std::vector<std::vector<double>> a{
    cell(-10.0, -11.0, 3.0, 0.2), cell(-2.0, -2.5, 1.0, 0.8),
    cell(-7.0, -7.5, 2.0, 0.4)};
  std::vector<std::vector<double>> b{a[2], a[0], a[1]};
  for (const mbs::SigmaFold rule : {mbs::SigmaFold::kUndecided,
      mbs::SigmaFold::kPooled, mbs::SigmaFold::kMaxChild,
      mbs::SigmaFold::kMeanChild})
  {
    const std::vector<double> fa = det::depthMultiBandFold(a, rule);
    const std::vector<double> fb = det::depthMultiBandFold(b, rule);
    for (std::size_t band = 0; band < kMB; ++band) {
      if (std::isnan(fa[band])) {
        EXPECT_TRUE(std::isnan(fb[band])) << "band " << band;
      } else {
        EXPECT_DOUBLE_EQ(fa[band], fb[band]) << "band " << band;
      }
    }
  }
}

TEST(PromoteNativeCell, IsAOneCellSummaryOfItself)
{
  const std::vector<double> promoted =
    det::promoteNativeDepthCell({-4.25, 0.33});
  ASSERT_EQ(promoted.size(), kMB);
  EXPECT_DOUBLE_EQ(promoted[det::kMultiMinBand], -4.25);
  EXPECT_DOUBLE_EQ(promoted[det::kMultiMeanBand], -4.25);
  EXPECT_DOUBLE_EQ(promoted[det::kMultiCountBand], 1.0);
  EXPECT_DOUBLE_EQ(promoted[det::kMultiSigmaBand], 0.33);
}

TEST(SigmaFoldName, IsStableForEveryEnumerator)
{
  // These strings travel into tile sidecars and STAC Items, so they are a
  // contract: re-spelling one changes every fingerprint that quotes it.
  EXPECT_EQ(mbs::sigmaFoldName(mbs::SigmaFold::kUndecided), "undecided");
  EXPECT_EQ(mbs::sigmaFoldName(mbs::SigmaFold::kPooled), "pooled");
  EXPECT_EQ(mbs::sigmaFoldName(mbs::SigmaFold::kMaxChild), "max_child");
  EXPECT_EQ(mbs::sigmaFoldName(mbs::SigmaFold::kMeanChild), "mean_child");
}

// --- the batch pyramid ------------------------------------------------------

TEST(MultiBandPyramid, WritesFourBandTilesWithCountLineageAndNodataSigma)
{
  ScratchDir dir("four_band");
  const std::vector<gggs::GridIndex> fine = fineSiblings();
  writeUniformNativeTile(dir.path(), fine[0], -10.0, 0.5);
  writeUniformNativeTile(dir.path(), fine[1], -4.0, 0.7);
  writeUniformNativeTile(dir.path(), fine[2], -20.0, 0.3);
  writeUniformNativeTile(dir.path(), fine[3], -15.0, 0.9);

  mbs::MultiBandOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = kFineLevel - 1;
  const mbs::DepthOverviewBuildResult r =
    mbs::buildMultiBandDepthOverviewPyramid(opts);
  ASSERT_TRUE(r.sidecar_replaced);
  ASSERT_EQ(r.tiles_written, 1u);

  const gggs::GridIndex parent = gggs::parent(fine.front());
  const mtrs::TiledRasterTile<double> tile = loadMultiBand(
    dir.path() / "overviews" / mtrs::tileFilename(parent), kFineLevel - 1);
  ASSERT_EQ(tile.bandCount(), kMB);
  // Every parent cell gathers native cells from one of the four children, so
  // MIN is that child's depth and COUNT is how many of its cells landed here.
  bool saw_shoalest = false;
  for (uint16_t row = 0; row < tile.edge; ++row) {
    for (uint16_t col = 0; col < tile.edge; ++col) {
      const double min_v = tile.get(row, col, det::kMultiMinBand);
      const double count = tile.get(row, col, det::kMultiCountBand);
      ASSERT_FALSE(std::isnan(min_v));
      EXPECT_GE(count, 1.0);
      EXPECT_DOUBLE_EQ(tile.get(row, col, det::kMultiMeanBand), min_v) <<
        "each parent cell here draws from ONE uniform child, so mean == min";
      EXPECT_TRUE(std::isnan(tile.get(row, col, det::kMultiSigmaBand))) <<
        "the sigma band is reserved while section 7's rule is open";
      if (min_v == -4.0) {saw_shoalest = true;}
    }
  }
  EXPECT_TRUE(saw_shoalest) << "the shoalest child's depth must survive";

  // The schema record rides the same swap as the tiles it describes.
  const fs::path schema = dir.path() / "overviews" / "overview_schema.json";
  ASSERT_TRUE(fs::exists(schema));
  std::ifstream in(schema);
  const std::string text(
    (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_NE(text.find("\"sigma_fold\": \"undecided\""), std::string::npos) << text;
  EXPECT_NE(text.find("\"sigma_band_written\": false"), std::string::npos) << text;
}

TEST(MultiBandPyramid, MinBandMatchesTheSingleBandGolden)
{
  // The pin: over the committed fixture, every multi-band sidecar tile's MIN
  // band must digest to the value the PRE-#331 single-band binary wrote into
  // its depth band. Same tile names, same shoalest numbers, at every level.
  namespace dor = depth_overview_regression;
  ScratchDir dir("golden_min");
  dor::writeFixture(dir.path());

  const std::map<std::string, std::string> golden = goldenMinBandDigests();
  ASSERT_FALSE(golden.empty()) <<
    "golden data file missing or unreadable: " << DEPTH_OVERVIEW_GOLDEN_FILE;

  mbs::MultiBandOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = dor::kFixtureMinLevel;
  const mbs::DepthOverviewBuildResult r =
    mbs::buildMultiBandDepthOverviewPyramid(opts);
  ASSERT_TRUE(r.sidecar_replaced);

  std::size_t compared = 0;
  for (const auto & entry : fs::directory_iterator(dir.path() / "overviews")) {
    if (!entry.is_regular_file() || entry.path().extension() != ".tif") {continue;}
    const std::string name = entry.path().filename().string();
    const auto expected = golden.find(name);
    ASSERT_NE(expected, golden.end()) <<
      "the multi-band pyramid wrote a tile the single-band one did not: " << name;
    const int level = std::stoi(name.substr(0, name.find('_')));
    const mtrs::TiledRasterTile<double> tile = loadMultiBand(entry.path(), level);
    EXPECT_EQ(
      dor::hex16(dor::hashDoubles(tile.band(det::kMultiMinBand))),
      expected->second) <<
      "MIN band differs from the single-band fold for " << name;
    ++compared;
  }
  EXPECT_EQ(compared, golden.size()) <<
    "the two pyramids must cover exactly the same tiles";
}

TEST(MultiBandPyramid, CoverageManifestCarriesSaturatedGeometricError)
{
  // uma-ADR-0013 D2/D3: a rev-3 overview tile is useless to the D7 selection
  // core (uma#395) without a nested per-tile error, and rev 3 did not mention
  // the field at all — this is the producer obligation being met.
  ScratchDir dir("manifest");
  const std::vector<gggs::GridIndex> fine = fineSiblings();
  for (const gggs::GridIndex & g : fine) {
    writeUniformNativeTile(dir.path(), g, -8.0, 0.4);
  }
  mbs::MultiBandOverviewOptions opts;
  opts.layer_dir = dir.path().string();
  opts.min_level = kFineLevel - 2;
  ASSERT_TRUE(mbs::buildMultiBandDepthOverviewPyramid(opts).sidecar_replaced);

  const std::optional<mtrs::CoverageManifest> loaded = mtrs::loadCoverageManifest(
    (dir.path() / "overviews" / mtrs::coverageManifestFilename()).string());
  ASSERT_TRUE(loaded.has_value());
  const mtrs::CoverageManifest & manifest = *loaded;
  ASSERT_FALSE(manifest.empty());
  const gggs::GridIndex parent = gggs::parent(fine.front());
  const gggs::GridIndex grandparent = gggs::parent(parent);
  const std::optional<double> parent_error = manifest.geometricError(parent);
  const std::optional<double> gp_error = manifest.geometricError(grandparent);
  ASSERT_TRUE(parent_error.has_value());
  ASSERT_TRUE(gp_error.has_value());
  EXPECT_GE(*gp_error, *parent_error) <<
    "error nesting is the producer obligation (uma-ADR-0013 D2): a parent's "
    "error must be at least its children's";
}

TEST(MultiBandPyramid, RefusesToReplaceASingleBandSidecar)
{
  // Consumers read these tiles BY BAND INDEX, so a schema swapped in place is
  // the one mistake nothing downstream can detect.
  ScratchDir dir("cross_schema");
  const std::vector<gggs::GridIndex> fine = fineSiblings();
  for (const gggs::GridIndex & g : fine) {
    writeUniformNativeTile(dir.path(), g, -8.0, 0.4);
  }
  mbs::DepthOverviewOptions single;
  single.layer_dir = dir.path().string();
  single.min_level = kFineLevel - 1;
  ASSERT_TRUE(mbs::buildDepthOverviewPyramid(single).sidecar_replaced);

  mbs::MultiBandOverviewOptions multi;
  multi.layer_dir = dir.path().string();
  multi.min_level = kFineLevel - 1;
  EXPECT_THROW(mbs::buildMultiBandDepthOverviewPyramid(multi), std::runtime_error);
  // The refusal must cost the existing sidecar nothing.
  const mtrs::TiledRasterTile<double> survivor = mtrs::loadTile<double>(
    (dir.path() / "overviews" /
    mtrs::tileFilename(gggs::parent(fine.front()))).string(),
    gggs::Level(kFineLevel - 1), 2);
  EXPECT_EQ(survivor.bandCount(), 2u);
}

TEST(MultiBandPyramid, SingleBandWriterRefusesAMultiBandSidecar)
{
  // The guard is symmetric: the legacy batch writer must not wholesale-replace
  // a rev-3 sidecar either.
  ScratchDir dir("cross_schema_rev");
  const std::vector<gggs::GridIndex> fine = fineSiblings();
  for (const gggs::GridIndex & g : fine) {
    writeUniformNativeTile(dir.path(), g, -8.0, 0.4);
  }
  mbs::MultiBandOverviewOptions multi;
  multi.layer_dir = dir.path().string();
  multi.min_level = kFineLevel - 1;
  ASSERT_TRUE(mbs::buildMultiBandDepthOverviewPyramid(multi).sidecar_replaced);

  mbs::DepthOverviewOptions single;
  single.layer_dir = dir.path().string();
  single.min_level = kFineLevel - 1;
  EXPECT_THROW(mbs::buildDepthOverviewPyramid(single), std::runtime_error);
}

// Every file under @p root, by relative path, with its bytes: what "left
// byte-for-byte untouched" is compared against.
std::map<std::string, std::string> snapshotTree(const fs::path & root)
{
  std::map<std::string, std::string> out;
  for (const auto & e : fs::recursive_directory_iterator(root)) {
    if (!e.is_regular_file()) {
      continue;
    }
    std::ifstream in(e.path(), std::ios::binary);
    out[fs::relative(e.path(), root).string()] = std::string(
      (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }
  return out;
}

TEST(MultiBandPyramid, PerParentWritersRefuseASingleBandSidecarBeforeTouchingIt)
{
  // Regression: the per-parent writer ran the cross-schema guard only after
  // its native-wins and no-children paths had already removed a derived tile
  // (and after loading children as 4-band), so a legacy single-band
  // overviews/ could be mutated before the refusal. Prune and remove-level
  // had no guard at all.
  ScratchDir dir("cross_schema_parent");
  const std::vector<gggs::GridIndex> fine = fineSiblings();
  for (const gggs::GridIndex & g : fine) {
    writeUniformNativeTile(dir.path(), g, -8.0, 0.4);
  }
  mbs::DepthOverviewOptions single;
  single.layer_dir = dir.path().string();
  single.min_level = kFineLevel - 1;
  ASSERT_TRUE(mbs::buildDepthOverviewPyramid(single).sidecar_replaced);
  const gggs::GridIndex parent = gggs::parent(fine.front());
  const fs::path overviews = dir.path() / "overviews";
  ASSERT_TRUE(fs::exists(overviews / mtrs::tileFilename(parent)));

  // Native-wins path: a native tile at the parent would remove the derived one.
  writeUniformNativeTile(dir.path(), parent, -3.0, 0.2);
  const std::map<std::string, std::string> before = snapshotTree(overviews);
  EXPECT_THROW(
    mbs::buildMultiBandDepthOverviewParent(
      dir.path().string(), parent.level(), parent.row(), parent.column()),
    std::runtime_error);
  EXPECT_EQ(snapshotTree(overviews), before);
  fs::remove(dir.path() / mtrs::tileFilename(parent));

  // No-children path: with the natives gone the derived tile would be removed.
  for (const gggs::GridIndex & g : fine) {
    fs::remove(dir.path() / mtrs::tileFilename(g));
  }
  EXPECT_THROW(
    mbs::buildMultiBandDepthOverviewParent(
      dir.path().string(), parent.level(), parent.row(), parent.column()),
    std::runtime_error);
  EXPECT_EQ(snapshotTree(overviews), before);

  // Prune and remove-level delete tiles too.
  EXPECT_THROW(
    mbs::pruneMultiBandOverviewLevel(dir.path().string(), parent.level()),
    std::runtime_error);
  EXPECT_THROW(
    mbs::removeMultiBandOverviewLevel(dir.path().string(), parent.level()),
    std::runtime_error);
  EXPECT_EQ(snapshotTree(overviews), before);
}

TEST(MultiBandPyramid, AnUnreadableProbeTileFailsClosed)
{
  // Regression: an unreadable probe tile returned "no schema known", which
  // skipped the cross-schema guard entirely.
  ScratchDir dir("probe_unreadable");
  const std::vector<gggs::GridIndex> fine = fineSiblings();
  for (const gggs::GridIndex & g : fine) {
    writeUniformNativeTile(dir.path(), g, -8.0, 0.4);
  }
  const gggs::GridIndex parent = gggs::parent(fine.front());
  const fs::path overviews = dir.path() / "overviews";
  fs::create_directories(overviews);
  const fs::path garbage = overviews / mtrs::tileFilename(parent);
  std::ofstream(garbage) << "not a GeoTIFF";

  const auto expect_named_refusal = [&](const auto & call) {
      try {
        call();
        ADD_FAILURE() << "an unreadable probe tile must be refused";
      } catch (const std::runtime_error & e) {
        EXPECT_NE(std::string(e.what()).find(garbage.string()), std::string::npos) <<
          e.what();
      }
    };
  expect_named_refusal(
    [&] {
      mbs::buildMultiBandDepthOverviewParent(
        dir.path().string(), parent.level(), parent.row(), parent.column());
    });
  mbs::MultiBandOverviewOptions multi;
  multi.layer_dir = dir.path().string();
  multi.min_level = kFineLevel - 1;
  expect_named_refusal([&] {mbs::buildMultiBandDepthOverviewPyramid(multi);});
  mbs::DepthOverviewOptions single;
  single.layer_dir = dir.path().string();
  single.min_level = kFineLevel - 1;
  expect_named_refusal([&] {mbs::buildDepthOverviewPyramid(single);});
  EXPECT_EQ(readText(garbage), "not a GeoTIFF");
}

TEST(MultiBandPyramid, TheSchemaRecordIsPreferredAndAnUnreadableOneRefused)
{
  // overview_schema.json states the schema; a tile is only probed without it.
  // A record that is present but unreadable is refused, never read as absent.
  ScratchDir dir("schema_record");
  const std::vector<gggs::GridIndex> fine = fineSiblings();
  for (const gggs::GridIndex & g : fine) {
    writeUniformNativeTile(dir.path(), g, -8.0, 0.4);
  }
  mbs::MultiBandOverviewOptions multi;
  multi.layer_dir = dir.path().string();
  multi.min_level = kFineLevel - 1;
  ASSERT_TRUE(mbs::buildMultiBandDepthOverviewPyramid(multi).sidecar_replaced);
  const fs::path overviews = dir.path() / "overviews";
  const fs::path schema = overviews / "overview_schema.json";
  ASSERT_TRUE(fs::exists(schema));

  // With the record in place, even an unreadable tile is not probed.
  const gggs::GridIndex parent = gggs::parent(fine.front());
  std::ofstream(overviews / "12_0_0.tif") << "not a GeoTIFF";
  EXPECT_TRUE(
    mbs::buildMultiBandDepthOverviewParent(
      dir.path().string(), parent.level(), parent.row(), parent.column())
    .written);
  fs::remove(overviews / "12_0_0.tif");

  // A record the single-band writer reads as 4-band refuses it, tiles or not.
  mbs::DepthOverviewOptions single;
  single.layer_dir = dir.path().string();
  single.min_level = kFineLevel - 1;
  EXPECT_THROW(mbs::buildDepthOverviewPyramid(single), std::runtime_error);

  std::ofstream(schema) << "{ not json";
  try {
    mbs::buildMultiBandDepthOverviewParent(
      dir.path().string(), parent.level(), parent.row(), parent.column());
    ADD_FAILURE() << "an unreadable schema record must be refused";
  } catch (const std::runtime_error & e) {
    EXPECT_NE(std::string(e.what()).find(schema.string()), std::string::npos) <<
      e.what();
  }
  EXPECT_EQ(readText(schema), "{ not json");
}

// --- the per-parent work unit -----------------------------------------------

TEST(PerParentOverview, MatchesTheBatchBuilderForTheSameParent)
{
  // The whole point of the per-parent mode is that a DAG can refresh one tile
  // without re-folding the layer. That is only true if it produces the same
  // tile the batch build would have.
  ScratchDir batch_dir("parent_batch");
  ScratchDir per_dir("parent_single");
  const std::vector<gggs::GridIndex> fine = fineSiblings();
  const double depths[4] = {-10.0, -4.0, -20.0, -15.0};
  for (std::size_t i = 0; i < fine.size(); ++i) {
    writeUniformNativeTile(batch_dir.path(), fine[i], depths[i], 0.5);
    writeUniformNativeTile(per_dir.path(), fine[i], depths[i], 0.5);
  }
  mbs::MultiBandOverviewOptions opts;
  opts.layer_dir = batch_dir.path().string();
  opts.min_level = kFineLevel - 1;
  ASSERT_TRUE(mbs::buildMultiBandDepthOverviewPyramid(opts).sidecar_replaced);

  const gggs::GridIndex parent = gggs::parent(fine.front());
  const mbs::MultiBandParentResult one =
    mbs::buildMultiBandDepthOverviewParent(
    per_dir.path().string(), parent.level(), parent.row(), parent.column());
  ASSERT_TRUE(one.written);
  EXPECT_EQ(one.children_used, 4u);

  const mtrs::TiledRasterTile<double> from_batch = loadMultiBand(
    batch_dir.path() / "overviews" / mtrs::tileFilename(parent),
    kFineLevel - 1);
  const mtrs::TiledRasterTile<double> from_one = loadMultiBand(
    per_dir.path() / "overviews" / mtrs::tileFilename(parent), kFineLevel - 1);
  for (std::size_t band = 0; band < kMB; ++band) {
    for (uint16_t row = 0; row < from_one.edge; ++row) {
      for (uint16_t col = 0; col < from_one.edge; ++col) {
        const double a = from_batch.get(row, col, band);
        const double b = from_one.get(row, col, band);
        if (std::isnan(a)) {
          ASSERT_TRUE(std::isnan(b)) << "band " << band;
        } else {
          ASSERT_DOUBLE_EQ(a, b) << "band " << band;
        }
      }
    }
  }
}

TEST(PerParentOverview, WritesItsOwnGeometricErrorSidecar)
{
  // Per-TILE metadata, not a shared coverage.json: a parallel DAG writing one
  // manifest would be a write race with no lock that would not also serialise
  // the DAG back into the batch build it replaces.
  ScratchDir dir("parent_meta");
  const std::vector<gggs::GridIndex> fine = fineSiblings();
  for (const gggs::GridIndex & g : fine) {
    writeUniformNativeTile(dir.path(), g, -8.0, 0.4);
  }
  const gggs::GridIndex parent = gggs::parent(fine.front());
  const mbs::MultiBandParentResult r = mbs::buildMultiBandDepthOverviewParent(
    dir.path().string(), parent.level(), parent.row(), parent.column());
  ASSERT_TRUE(r.written);
  EXPECT_GT(r.geometric_error_m, 0.0);

  fs::path meta = dir.path() / "overviews" / mtrs::tileFilename(parent);
  meta.replace_extension(".json");
  ASSERT_TRUE(fs::exists(meta));
  std::ifstream in(meta);
  const std::string text(
    (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_NE(text.find("geometric_error_m"), std::string::npos) << text;
  EXPECT_NE(text.find("\"sigma_fold\": \"undecided\""), std::string::npos) << text;

  // And the per-parent path leaves the SAME schema record the batch builder
  // does, so the cross-schema guard reads a record rather than probing a tile.
  const std::string schema = readText(
    dir.path() / "overviews" / "overview_schema.json");
  EXPECT_NE(schema.find("depth-overview-multiband/1"), std::string::npos) << schema;
  EXPECT_NE(schema.find("\"sigma_fold\": \"undecided\""), std::string::npos) <<
    schema;
  ScratchDir batch_dir("parent_meta_batch");
  for (const gggs::GridIndex & g : fine) {
    writeUniformNativeTile(batch_dir.path(), g, -8.0, 0.4);
  }
  mbs::MultiBandOverviewOptions batch;
  batch.layer_dir = batch_dir.path().string();
  batch.min_level = kFineLevel - 1;
  ASSERT_TRUE(mbs::buildMultiBandDepthOverviewPyramid(batch).sidecar_replaced);
  EXPECT_EQ(
    schema, readText(batch_dir.path() / "overviews" / "overview_schema.json"));

  // A sidecar recorded under another sigma rule is refused, not mixed.
  EXPECT_THROW(
    mbs::buildMultiBandDepthOverviewParent(
      dir.path().string(), parent.level(), parent.row(), parent.column(),
      mbs::SigmaFold::kMaxChild),
    std::runtime_error);

  // No staging dir and no run lock: a second invocation over the same parent
  // must simply redo the tile.
  EXPECT_FALSE(fs::exists(dir.path() / "overviews.tmp"));
  EXPECT_TRUE(
    mbs::buildMultiBandDepthOverviewParent(
      dir.path().string(), parent.level(), parent.row(), parent.column())
    .written);
  // And it must leave no write-beside temporary behind.
  for (const auto & e : fs::directory_iterator(dir.path() / "overviews")) {
    const std::string name = e.path().filename().string();
    EXPECT_EQ(name.find(".tmp"), std::string::npos) << name;
    EXPECT_NE(name.front(), '.') << name;
  }
}

TEST(PerParentOverview, ErrorNestsOverADerivedChild)
{
  // Two steps up: the grandparent folds a DERIVED child, whose recorded error
  // comes back out of the per-tile sidecar. Saturation must hold across that
  // hand-off, or the D7 core's nesting condition breaks at the seam.
  ScratchDir dir("parent_nesting");
  const gggs::GridIndex seed = gggs::Level(kFineLevel).gridIndex(kLat, kLon);
  const gggs::GridIndex parent = gggs::parent(seed);
  const gggs::GridIndex grandparent = gggs::parent(parent);
  for (const gggs::GridIndex & g : gggs::children(parent)) {
    writeUniformNativeTile(dir.path(), g, -8.0, 0.4);
  }
  const mbs::MultiBandParentResult child_run =
    mbs::buildMultiBandDepthOverviewParent(
    dir.path().string(), parent.level(), parent.row(), parent.column());
  ASSERT_TRUE(child_run.written);
  const mbs::MultiBandParentResult gp_run =
    mbs::buildMultiBandDepthOverviewParent(
    dir.path().string(), grandparent.level(), grandparent.row(),
    grandparent.column());
  ASSERT_TRUE(gp_run.written);
  EXPECT_GE(gp_run.geometric_error_m, child_run.geometric_error_m);
}

TEST(PerParentOverview, NativeTileAtTheParentSuppressesTheWrite)
{
  // Native-wins, same rule as the batch builder: compiled data is never
  // overwritten and never merged into.
  ScratchDir dir("parent_native");
  const std::vector<gggs::GridIndex> fine = fineSiblings();
  for (const gggs::GridIndex & g : fine) {
    writeUniformNativeTile(dir.path(), g, -8.0, 0.4);
  }
  const gggs::GridIndex parent = gggs::parent(fine.front());
  writeUniformNativeTile(dir.path(), parent, -3.0, 0.2);

  const mbs::MultiBandParentResult r = mbs::buildMultiBandDepthOverviewParent(
    dir.path().string(), parent.level(), parent.row(), parent.column());
  EXPECT_FALSE(r.written);
  EXPECT_TRUE(r.suppressed_by_native);
  EXPECT_FALSE(fs::exists(dir.path() / "overviews" / mtrs::tileFilename(parent)));
}

TEST(PerParentOverview, NoChildYetIsNotAnError)
{
  // A per-parent DAG legitimately enumerates parents whose children do not
  // exist; throwing here would turn a sparse region into a failed run.
  ScratchDir dir("parent_empty");
  const gggs::GridIndex parent = gggs::parent(fineSiblings().front());
  const mbs::MultiBandParentResult r = mbs::buildMultiBandDepthOverviewParent(
    dir.path().string(), parent.level(), parent.row(), parent.column());
  EXPECT_FALSE(r.written);
  EXPECT_FALSE(r.suppressed_by_native);
  EXPECT_EQ(r.children_used, 0u);
}

TEST(PerParentOverview, RefusesAChildThatIsBothNativeAndDerived)
{
  // Disjoint by construction in both writers, so this is a corrupted layer, not
  // a precedence question — resolving it silently would make the pyramid depend
  // on which rule ran last.
  ScratchDir dir("parent_conflict");
  const std::vector<gggs::GridIndex> fine = fineSiblings();
  for (const gggs::GridIndex & g : fine) {
    writeUniformNativeTile(dir.path(), g, -8.0, 0.4);
  }
  // A 4-band tile, so it is the disjointness check that refuses and not the
  // cross-schema guard (which runs first).
  writeUniformMultiBandTile(dir.path() / "overviews", fine.front(), -8.0);
  const gggs::GridIndex parent = gggs::parent(fine.front());
  try {
    mbs::buildMultiBandDepthOverviewParent(
      dir.path().string(), parent.level(), parent.row(), parent.column());
    ADD_FAILURE() << "a child present both natively and as a derived overview "
      "must be refused, not resolved by precedence";
  } catch (const std::runtime_error & e) {
    EXPECT_NE(std::string(e.what()).find("disjoint"), std::string::npos) <<
      e.what();
  }
}

TEST(PerParentOverview, RejectsAnIndexThatNamesNoGrid)
{
  ScratchDir dir("parent_badindex");
  EXPECT_THROW(
    mbs::buildMultiBandDepthOverviewParent(
      dir.path().string(), 3, 1u << 30, 1u << 30),
    std::invalid_argument);
  // The apex has no child level to fold from.
  EXPECT_THROW(
    mbs::buildMultiBandDepthOverviewParent(dir.path().string(), -1, 0, 0),
    std::invalid_argument);
  EXPECT_THROW(
    mbs::buildMultiBandDepthOverviewParent("/nonexistent/layer/dir", 5, 0, 0),
    std::runtime_error);
}

TEST(PerParentOverview, ListsTheParentsADagShouldSchedule)
{
  // What a Snakemake DAG enumerates. Kept on this side of the fence because
  // the parent/child mapping is GGGS, whose column counts vary by latitude
  // band; a second implementation in the rules would be a second thing to get
  // wrong, with no tests on it.
  ScratchDir dir("parent_list");
  const std::vector<gggs::GridIndex> fine = fineSiblings();
  for (const gggs::GridIndex & g : fine) {
    writeUniformNativeTile(dir.path(), g, -8.0, 0.4);
  }
  const gggs::GridIndex parent = gggs::parent(fine.front());
  std::vector<gggs::GridIndex> parents =
    mbs::listMultiBandOverviewParents(dir.path().string(), parent.level());
  ASSERT_EQ(parents.size(), 1u) <<
    "four children name ONE parent; a DAG scheduled twice over one tile would "
    "race with itself over the destination";
  EXPECT_EQ(parents.front(), parent);

  // A parent the compile already covers is omitted: scheduling it would only
  // produce a suppressed no-op, once per invocation.
  writeUniformNativeTile(dir.path(), parent, -3.0, 0.2);
  EXPECT_TRUE(
    mbs::listMultiBandOverviewParents(
      dir.path().string(), parent.level()).empty());

  // A level with no children at all schedules nothing, rather than failing.
  EXPECT_TRUE(
    mbs::listMultiBandOverviewParents(dir.path().string(), 3).empty());
  EXPECT_THROW(
    mbs::listMultiBandOverviewParents(dir.path().string(), -1),
    std::invalid_argument);
  EXPECT_THROW(
    mbs::listMultiBandOverviewParents("/nonexistent/layer/dir", 5),
    std::runtime_error);
}

TEST(PerParentOverview, ListsADerivedTileAsAContributorToTheNextLevelUp)
{
  // The DAG climbs: once level N is built, level N-1's parents must appear.
  ScratchDir dir("parent_list_climb");
  const gggs::GridIndex seed = gggs::Level(kFineLevel).gridIndex(kLat, kLon);
  const gggs::GridIndex parent = gggs::parent(seed);
  const gggs::GridIndex grandparent = gggs::parent(parent);
  for (const gggs::GridIndex & g : gggs::children(parent)) {
    writeUniformNativeTile(dir.path(), g, -8.0, 0.4);
  }
  EXPECT_TRUE(
    mbs::listMultiBandOverviewParents(
      dir.path().string(), grandparent.level()).empty()) <<
    "nothing at the child level yet";
  ASSERT_TRUE(
    mbs::buildMultiBandDepthOverviewParent(
      dir.path().string(), parent.level(), parent.row(), parent.column())
    .written);
  const std::vector<gggs::GridIndex> next =
    mbs::listMultiBandOverviewParents(
    dir.path().string(), grandparent.level());
  ASSERT_EQ(next.size(), 1u);
  EXPECT_EQ(next.front(), grandparent);
}

// --- stale derived tiles, the DAG's inputs ----------------------------------

fs::path recordOf(const fs::path & tile)
{
  fs::path record = tile;
  record.replace_extension(".json");
  return record;
}

TEST(PerParentOverview, NativeWinsRemovesAStaleDerivedParent)
{
  // Regression: a native tile arriving where an earlier run had derived one
  // left the derived tile in place, and the next coarser fold then found the
  // index "both natively and derived" and refused the layer forever.
  ScratchDir dir("stale_native");
  const std::vector<gggs::GridIndex> fine = fineSiblings();
  for (const gggs::GridIndex & g : fine) {
    writeUniformNativeTile(dir.path(), g, -8.0, 0.4);
  }
  const gggs::GridIndex parent = gggs::parent(fine.front());
  const gggs::GridIndex grandparent = gggs::parent(parent);
  ASSERT_TRUE(
    mbs::buildMultiBandDepthOverviewParent(
      dir.path().string(), parent.level(), parent.row(), parent.column())
    .written);
  const fs::path derived = dir.path() / "overviews" / mtrs::tileFilename(parent);
  std::ofstream(fs::path(derived).concat(".fp")) << "{}";
  ASSERT_TRUE(fs::exists(derived));

  writeUniformNativeTile(dir.path(), parent, -3.0, 0.2);   // compiled data lands
  const mbs::MultiBandParentResult r = mbs::buildMultiBandDepthOverviewParent(
    dir.path().string(), parent.level(), parent.row(), parent.column());
  EXPECT_TRUE(r.suppressed_by_native);
  EXPECT_TRUE(r.removed_stale);
  EXPECT_FALSE(fs::exists(derived));
  EXPECT_FALSE(fs::exists(recordOf(derived)));
  EXPECT_FALSE(fs::exists(fs::path(derived).concat(".fp")));
  EXPECT_NO_THROW(
    mbs::buildMultiBandDepthOverviewParent(
      dir.path().string(), grandparent.level(), grandparent.row(),
      grandparent.column()));
}

TEST(PerParentOverview, AParentWhoseChildrenAreGoneIsRemoved)
{
  // Regression: a parent whose children vanished kept its tile and record,
  // so the coverage manifest and the derived index kept advertising it.
  ScratchDir dir("stale_orphan");
  const std::vector<gggs::GridIndex> fine = fineSiblings();
  for (const gggs::GridIndex & g : fine) {
    writeUniformNativeTile(dir.path(), g, -8.0, 0.4);
  }
  const gggs::GridIndex parent = gggs::parent(fine.front());
  ASSERT_TRUE(
    mbs::buildMultiBandDepthOverviewParent(
      dir.path().string(), parent.level(), parent.row(), parent.column())
    .written);
  for (const gggs::GridIndex & g : fine) {
    fs::remove(dir.path() / mtrs::tileFilename(g));
  }
  const mbs::MultiBandParentResult r = mbs::buildMultiBandDepthOverviewParent(
    dir.path().string(), parent.level(), parent.row(), parent.column());
  EXPECT_FALSE(r.written);
  EXPECT_TRUE(r.removed_stale);
  EXPECT_FALSE(fs::exists(dir.path() / "overviews" / mtrs::tileFilename(parent)));
}

TEST(PruneOverviewLevel, RemovesOnlyTheTilesThatDescribeNothing)
{
  // The DAG never schedules a parent with no children or with a native tile,
  // so without a prune nothing would remove such a tile.
  ScratchDir dir("prune");
  const gggs::GridIndex seed = gggs::Level(kFineLevel).gridIndex(kLat, kLon);
  const gggs::GridIndex keep = gggs::parent(seed);
  // Three parents at one level: one kept (children present), one covered by
  // a native tile, one whose children are gone.
  std::vector<gggs::GridIndex> parents;
  for (const gggs::GridIndex & sibling : gggs::children(gggs::parent(keep))) {
    parents.push_back(sibling);
  }
  ASSERT_GE(parents.size(), 3u);
  for (const gggs::GridIndex & p : parents) {
    for (const gggs::GridIndex & g : gggs::children(p)) {
      writeUniformNativeTile(dir.path(), g, -8.0, 0.4);
    }
    ASSERT_TRUE(
      mbs::buildMultiBandDepthOverviewParent(
        dir.path().string(), p.level(), p.row(), p.column()).written);
  }
  const gggs::GridIndex covered = parents[1];
  const gggs::GridIndex orphaned = parents[2];
  writeUniformNativeTile(dir.path(), covered, -3.0, 0.2);
  for (const gggs::GridIndex & g : gggs::children(orphaned)) {
    fs::remove(dir.path() / mtrs::tileFilename(g));
  }

  const std::vector<gggs::GridIndex> removed =
    mbs::pruneMultiBandOverviewLevel(dir.path().string(), keep.level());
  std::vector<gggs::GridIndex> expected{covered, orphaned};
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(removed, expected);
  const fs::path overviews = dir.path() / "overviews";
  EXPECT_TRUE(fs::exists(overviews / mtrs::tileFilename(parents[0])));
  EXPECT_FALSE(fs::exists(overviews / mtrs::tileFilename(covered)));
  EXPECT_FALSE(fs::exists(recordOf(overviews / mtrs::tileFilename(orphaned))));
  // Idempotent: a second prune finds nothing.
  EXPECT_TRUE(
    mbs::pruneMultiBandOverviewLevel(dir.path().string(), keep.level()).empty());
  EXPECT_THROW(
    mbs::pruneMultiBandOverviewLevel(dir.path().string(), 21),
    std::invalid_argument);
}

TEST(PruneOverviewLevel, RemoveLevelTakesEveryDerivedTileAndNoNativeOne)
{
  // A level the regenerate no longer builds (min_level raised, say) still has
  // children, so --prune keeps it; the manifest and the Items scan all of
  // overviews/, so without this it stayed published and went stale.
  ScratchDir dir("remove_level");
  const std::vector<gggs::GridIndex> fine = fineSiblings();
  for (const gggs::GridIndex & g : fine) {
    writeUniformNativeTile(dir.path(), g, -8.0, 0.4);
  }
  const gggs::GridIndex parent = gggs::parent(fine.front());
  ASSERT_TRUE(
    mbs::buildMultiBandDepthOverviewParent(
      dir.path().string(), parent.level(), parent.row(), parent.column())
    .written);
  const fs::path derived = dir.path() / "overviews" / mtrs::tileFilename(parent);
  std::ofstream(fs::path(derived).concat(".fp")) << "{}";
  // It describes something, so a prune keeps it ...
  EXPECT_TRUE(
    mbs::pruneMultiBandOverviewLevel(dir.path().string(), parent.level()).empty());
  // ... and removing the level takes it, its record and its sidecar.
  EXPECT_EQ(
    mbs::removeMultiBandOverviewLevel(dir.path().string(), parent.level()),
    std::vector<gggs::GridIndex>{parent});
  EXPECT_FALSE(fs::exists(derived));
  EXPECT_FALSE(fs::exists(recordOf(derived)));
  EXPECT_FALSE(fs::exists(fs::path(derived).concat(".fp")));
  // Native tiles, at the level or anywhere, are never touched.
  for (const gggs::GridIndex & g : fine) {
    EXPECT_TRUE(
      mbs::removeMultiBandOverviewLevel(dir.path().string(), g.level()).empty());
    EXPECT_TRUE(fs::exists(dir.path() / mtrs::tileFilename(g)));
  }
  EXPECT_THROW(
    mbs::removeMultiBandOverviewLevel(dir.path().string(), 21),
    std::invalid_argument);
}

TEST(PerParentOverview, ListingNamesEachParentsChildrenForTheDag)
{
  // The DAG's job inputs: native children by name, derived ones under
  // overviews/, so a changed or vanished child reruns exactly its parent.
  ScratchDir dir("list_inputs");
  const gggs::GridIndex seed = gggs::Level(kFineLevel).gridIndex(kLat, kLon);
  const gggs::GridIndex parent = gggs::parent(seed);
  const gggs::GridIndex grandparent = gggs::parent(parent);
  writeUniformNativeTile(dir.path(), seed, -8.0, 0.4);
  std::vector<mbs::MultiBandOverviewParent> level_one =
    mbs::listMultiBandOverviewParentInputs(dir.path().string(), parent.level());
  ASSERT_EQ(level_one.size(), 1u);
  EXPECT_EQ(level_one.front().parent, parent);
  EXPECT_EQ(level_one.front().children,
    std::vector<std::string>{mtrs::tileFilename(seed)});

  ASSERT_TRUE(
    mbs::buildMultiBandDepthOverviewParent(
      dir.path().string(), parent.level(), parent.row(), parent.column())
    .written);
  const std::vector<mbs::MultiBandOverviewParent> level_two =
    mbs::listMultiBandOverviewParentInputs(
    dir.path().string(), grandparent.level());
  ASSERT_EQ(level_two.size(), 1u);
  EXPECT_EQ(level_two.front().children,
    std::vector<std::string>{"overviews/" + mtrs::tileFilename(parent)});

  // The record names the same lineage, which is what the tile's Item is
  // built from.
  const std::string record = readText(
    recordOf(dir.path() / "overviews" / mtrs::tileFilename(parent)));
  EXPECT_NE(record.find("\"children\""), std::string::npos) << record;
  EXPECT_NE(record.find(mtrs::tileFilename(seed)), std::string::npos) << record;

  // A child present both ways is refused while planning, not mid-run.
  writeUniformNativeTile(dir.path(), parent, -3.0, 0.2);
  EXPECT_THROW(
    mbs::listMultiBandOverviewParentInputs(
      dir.path().string(), grandparent.level()),
    std::runtime_error);
}

TEST(LayerWriterLock, PerParentWritesAndABatchBuildExcludeEachOther)
{
  // Regression: the per-parent writer ignored the batch builder's run lock, so
  // a batch swap could retire tiles a per-parent run had just written, or a
  // per-parent run could write into a directory about to be retired.
  ScratchDir dir("writer_lock");
  const std::vector<gggs::GridIndex> fine = fineSiblings();
  for (const gggs::GridIndex & g : fine) {
    writeUniformNativeTile(dir.path(), g, -8.0, 0.4);
  }
  const gggs::GridIndex parent = gggs::parent(fine.front());
  const fs::path lock_path = dir.path() / "overviews.lock";

  {
    // A batch build holds the lock exclusively.
    const int fd = ::open(lock_path.c_str(), O_RDWR | O_CREAT, 0644);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::flock(fd, LOCK_EX | LOCK_NB), 0);
    try {
      mbs::buildMultiBandDepthOverviewParent(
        dir.path().string(), parent.level(), parent.row(), parent.column());
      ADD_FAILURE() << "a per-parent write beside a batch build must refuse";
    } catch (const std::runtime_error & e) {
      EXPECT_NE(std::string(e.what()).find("batch"), std::string::npos) <<
        e.what();
    }
    EXPECT_THROW(
      mbs::pruneMultiBandOverviewLevel(dir.path().string(), parent.level()),
      std::runtime_error);
    ::close(fd);
  }
  {
    // Per-parent writers hold it shared: two may run at once, a batch may not.
    const int fd = ::open(lock_path.c_str(), O_RDWR | O_CREAT, 0644);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::flock(fd, LOCK_SH | LOCK_NB), 0);
    EXPECT_TRUE(
      mbs::buildMultiBandDepthOverviewParent(
        dir.path().string(), parent.level(), parent.row(), parent.column())
      .written);
    mbs::MultiBandOverviewOptions batch;
    batch.layer_dir = dir.path().string();
    batch.min_level = kFineLevel - 1;
    EXPECT_THROW(
      mbs::buildMultiBandDepthOverviewPyramid(batch), std::runtime_error);
    batch.dry_run = true;   // a dry run writes nothing and takes no lock
    EXPECT_NO_THROW(mbs::buildMultiBandDepthOverviewPyramid(batch));
    ::close(fd);
  }
  {
    // The single-band batch builder swaps overviews/ wholesale as well, so it
    // takes the same exclusive lock: it refuses beside a per-parent writer.
    // Regression: it called the shared pyramid body with no lock at all.
    ScratchDir single_dir("writer_lock_single");
    for (const gggs::GridIndex & g : fine) {
      writeUniformNativeTile(single_dir.path(), g, -8.0, 0.4);
    }
    const fs::path single_lock = single_dir.path() / "overviews.lock";
    const int fd = ::open(single_lock.c_str(), O_RDWR | O_CREAT, 0644);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::flock(fd, LOCK_SH | LOCK_NB), 0);
    mbs::DepthOverviewOptions single;
    single.layer_dir = single_dir.path().string();
    single.min_level = kFineLevel - 1;
    EXPECT_THROW(mbs::buildDepthOverviewPyramid(single), std::runtime_error);
    EXPECT_FALSE(fs::exists(single_dir.path() / "overviews"));
    single.dry_run = true;
    EXPECT_NO_THROW(mbs::buildDepthOverviewPyramid(single));
    ::close(fd);
    single.dry_run = false;
    EXPECT_TRUE(mbs::buildDepthOverviewPyramid(single).sidecar_replaced);
  }
  // A crashed batch build's staging directory also stops a per-parent write.
  fs::create_directories(dir.path() / "overviews.tmp");
  EXPECT_THROW(
    mbs::buildMultiBandDepthOverviewParent(
      dir.path().string(), parent.level(), parent.row(), parent.column()),
    std::runtime_error);
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
