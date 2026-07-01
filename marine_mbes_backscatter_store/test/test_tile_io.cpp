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

#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "marine_mbes_backscatter_store/mbes_cell.hpp"
#include "marine_mbes_backscatter_store/mbes_store.hpp"
#include "marine_mbes_backscatter_store/registry.hpp"
#include "marine_mbes_backscatter_store/tile_io.hpp"

using marine_mbes_backscatter_store::MbesBackscatterStore;
using marine_mbes_backscatter_store::MbesCell;
using marine_mbes_backscatter_store::SourceLayer;
using marine_mbes_backscatter_store::StoreMetadata;

namespace fs = std::filesystem;

class TileIoTest : public ::testing::Test
{
protected:
  fs::path dir_;

  void SetUp() override
  {
    const std::string name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
    dir_ = fs::temp_directory_path() / ("marine_mbes_store_" + name);
    fs::remove_all(dir_);
  }

  void TearDown() override
  {
    fs::remove_all(dir_);
  }
};

TEST_F(TileIoTest, RoundTripPreservesCells)
{
  MbesBackscatterStore store(5);
  store.set(
    SourceLayer::Cube, store.cellIndex(43.0, -70.5), MbesCell{-18.5f, 0.375f, 0.75f});
  store.set(
    SourceLayer::Cube, store.cellIndex(44.0, -71.0), MbesCell{-12.5f, 0.125f, 0.25f});

  EXPECT_EQ(marine_mbes_backscatter_store::save(store, dir_.string()), 2u);

  MbesBackscatterStore reloaded(5);
  EXPECT_EQ(marine_mbes_backscatter_store::load(reloaded, dir_.string()), 2u);

  const auto a = reloaded.get(SourceLayer::Cube, reloaded.cellIndex(43.0, -70.5));
  ASSERT_TRUE(a.has_value());
  EXPECT_FLOAT_EQ(a->mean, -18.5f);
  EXPECT_FLOAT_EQ(a->standard_error, 0.375f);
  EXPECT_FLOAT_EQ(a->sample_sd, 0.75f);

  const auto b = reloaded.get(SourceLayer::Cube, reloaded.cellIndex(44.0, -71.0));
  ASSERT_TRUE(b.has_value());
  EXPECT_FLOAT_EQ(b->mean, -12.5f);
}

// --- Welford sufficient-statistics round-trip (#248 amendment A.1) ---

TEST_F(TileIoTest, WelfordRoundTripReconstructsNAndM2)
{
  // The 3-band value tile encodes {mean, standard_error, sample_sd}. With a
  // confidence scale applied to the standard error, the consumer divides the
  // scale out (SE = standard_error / scale = sample_sd / sqrt(n)) and
  // reconstructs the full Welford state:
  //   n  = (sample_sd / SE)^2
  //   M2 = sample_sd^2 * (n - 1)
  // Choose exact-in-float values: n=9, sample_sd=3 -> SE_actual = 3/3 = 1.
  const float scale = 2.0f;
  const int n_true = 9;
  const float sample_sd = 3.0f;
  const float se_actual = sample_sd / std::sqrt(static_cast<float>(n_true));  // 1.0
  const float stored_se = scale * se_actual;                                  // 2.0

  MbesBackscatterStore store(5);
  store.set(
    SourceLayer::Cube, store.cellIndex(43.0, -70.5),
    MbesCell{-18.5f, stored_se, sample_sd});
  EXPECT_EQ(marine_mbes_backscatter_store::save(store, dir_.string()), 1u);

  MbesBackscatterStore reloaded(5);
  EXPECT_EQ(marine_mbes_backscatter_store::load(reloaded, dir_.string()), 1u);
  const auto got = reloaded.get(SourceLayer::Cube, reloaded.cellIndex(43.0, -70.5));
  ASSERT_TRUE(got.has_value());

  // Divide the confidence scale out, then reconstruct.
  const double se = static_cast<double>(got->standard_error) / scale;
  const double n = std::pow(static_cast<double>(got->sample_sd) / se, 2.0);
  const double m2 = std::pow(static_cast<double>(got->sample_sd), 2.0) * (n - 1.0);

  EXPECT_NEAR(n, static_cast<double>(n_true), 1e-4);
  EXPECT_NEAR(m2, static_cast<double>(sample_sd) * sample_sd * (n_true - 1), 1e-4);
}

TEST_F(TileIoTest, WelfordN1SentinelRoundTrips)
{
  // n=1 sentinel: sample_sd == 0 with a finite mean encodes n=1, M2=0, distinct
  // from no-data (NaN mean).
  MbesBackscatterStore store(5);
  store.set(
    SourceLayer::Cube, store.cellIndex(43.0, -70.5), MbesCell{-12.0f, 0.0f, 0.0f});
  EXPECT_EQ(marine_mbes_backscatter_store::save(store, dir_.string()), 1u);

  MbesBackscatterStore reloaded(5);
  EXPECT_EQ(marine_mbes_backscatter_store::load(reloaded, dir_.string()), 1u);
  const auto got = reloaded.get(SourceLayer::Cube, reloaded.cellIndex(43.0, -70.5));
  ASSERT_TRUE(got.has_value());

  // Finite mean + zero sample_sd -> real data, n=1, M2=0.
  EXPECT_TRUE(got->hasData());
  EXPECT_FALSE(std::isnan(got->mean));
  EXPECT_FLOAT_EQ(got->mean, -12.0f);
  EXPECT_FLOAT_EQ(got->sample_sd, 0.0f);   // n=1 detector: sample_sd == 0
  // M2 = sample_sd^2 * (n-1) = 0.
  const double m2 = static_cast<double>(got->sample_sd) * got->sample_sd * (1.0 - 1.0);
  EXPECT_DOUBLE_EQ(m2, 0.0);

  // Distinct from a no-data cell (NaN mean), which is not data.
  const MbesCell no_data{};
  EXPECT_FALSE(no_data.hasData());
  EXPECT_TRUE(std::isnan(no_data.mean));
}

TEST_F(TileIoTest, ConfidenceScaleDividesOutToIntegerN)
{
  // Writing standard_error as scale * SE_actual and dividing scale out on reload
  // must recover an integer n = (sample_sd / SE_actual)^2.
  const float scale = 4.0f;
  const int n_true = 16;
  const float sample_sd = 8.0f;
  const float se_actual = sample_sd / std::sqrt(static_cast<float>(n_true));  // 2.0
  const float stored_se = scale * se_actual;                                  // 8.0

  MbesBackscatterStore store(5);
  store.set(
    SourceLayer::Cube, store.cellIndex(43.0, -70.5),
    MbesCell{-5.0f, stored_se, sample_sd});
  marine_mbes_backscatter_store::save(store, dir_.string());

  MbesBackscatterStore reloaded(5);
  marine_mbes_backscatter_store::load(reloaded, dir_.string());
  const auto got = reloaded.get(SourceLayer::Cube, reloaded.cellIndex(43.0, -70.5));
  ASSERT_TRUE(got.has_value());

  const double se = static_cast<double>(got->standard_error) / scale;
  const double n = std::pow(static_cast<double>(got->sample_sd) / se, 2.0);
  EXPECT_NEAR(n, std::round(n), 1e-4);          // integer count
  EXPECT_EQ(static_cast<int>(std::round(n)), n_true);
}

TEST_F(TileIoTest, LoadedTilesAreCleanAndDontResave)
{
  MbesBackscatterStore store(5);
  store.set(SourceLayer::Cube, store.cellIndex(43.0, -70.5), MbesCell{-30.0f, 0.5f, 1.0f});

  EXPECT_EQ(marine_mbes_backscatter_store::save(store, dir_.string()), 1u);
  EXPECT_EQ(marine_mbes_backscatter_store::save(store, dir_.string()), 0u);  // incremental

  MbesBackscatterStore reloaded(5);
  marine_mbes_backscatter_store::load(reloaded, dir_.string());
  EXPECT_EQ(marine_mbes_backscatter_store::save(reloaded, dir_.string()), 0u);
}

TEST_F(TileIoTest, SaveWritesCubeSubdirectory)
{
  MbesBackscatterStore store(5);
  store.set(SourceLayer::Cube, store.cellIndex(43.0, -70.5), MbesCell{-10.0f, 0.1f, 0.2f});
  marine_mbes_backscatter_store::save(store, dir_.string());
  EXPECT_TRUE(fs::is_directory(dir_ / "cube"));
}

TEST_F(TileIoTest, LoadRejectsTilesFromAnotherLevel)
{
  MbesBackscatterStore store(5);
  store.set(SourceLayer::Cube, store.cellIndex(43.0, -70.5), MbesCell{-30.0f, 0.5f, 1.0f});
  marine_mbes_backscatter_store::save(store, dir_.string());

  MbesBackscatterStore wrong_level(6);
  EXPECT_THROW(
    marine_mbes_backscatter_store::load(wrong_level, dir_.string()), std::runtime_error);
}

// --- Coarse StoreMetadata (registry.json) round-trip (#248) ---

TEST_F(TileIoTest, StoreMetadataRoundTrip)
{
  MbesBackscatterStore store(5);
  store.set(SourceLayer::Cube, store.cellIndex(43.0, -70.5), MbesCell{-20.0f, 0.5f, 1.0f});
  StoreMetadata md{"bizzyboat", "kongsberg-m3", "massabesic-2026", "2026-06-30", ""};

  marine_mbes_backscatter_store::save(store, dir_.string(), &md);
  EXPECT_TRUE(fs::is_regular_file(dir_ / "registry.json"));
  EXPECT_FALSE(fs::exists(dir_ / "registry.json.tmp"));   // atomic publish

  StoreMetadata reloaded;
  marine_mbes_backscatter_store::load(store, dir_.string(), &reloaded);
  EXPECT_EQ(reloaded.platform, "bizzyboat");
  EXPECT_EQ(reloaded.sensor, "kongsberg-m3");
  EXPECT_EQ(reloaded.survey, "massabesic-2026");
  EXPECT_EQ(reloaded.date, "2026-06-30");
  EXPECT_EQ(reloaded.calibration_ref, "");   // empty until a calibration exists
}
