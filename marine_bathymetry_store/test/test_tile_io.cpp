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

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/tile_io.hpp"

using marine_bathymetry_store::BathyCell;
using marine_bathymetry_store::BathymetryStore;
using marine_bathymetry_store::SourceLayer;

namespace fs = std::filesystem;

class TileIoTest : public ::testing::Test
{
protected:
  fs::path dir_;

  void SetUp() override
  {
    const std::string name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
    dir_ = fs::temp_directory_path() / ("marine_bathy_store_" + name);
    fs::remove_all(dir_);
  }

  void TearDown() override
  {
    fs::remove_all(dir_);
  }
};

TEST_F(TileIoTest, RoundTripPreservesCells)
{
  BathymetryStore store(5);
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-30.123, 0.456, 1.78e9});
  store.set(SourceLayer::Processed, store.cellIndex(44.0, -71.0), BathyCell{-12.5, 0.2, 1.79e9});

  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 2u);

  BathymetryStore reloaded(5);
  EXPECT_EQ(marine_bathymetry_store::load(reloaded, dir_.string()), 2u);

  const auto draft = reloaded.get(SourceLayer::Draft, reloaded.cellIndex(43.0, -70.5));
  ASSERT_TRUE(draft.has_value());
  EXPECT_DOUBLE_EQ(draft->depth, -30.123);
  EXPECT_DOUBLE_EQ(draft->uncertainty, 0.456);
  // Float64 timestamp band: absolute Unix seconds survive exactly.
  // (A Float32 band would lose ~128 s here — this is the precision guard.)
  EXPECT_DOUBLE_EQ(draft->timestamp, 1.78e9);

  const auto processed = reloaded.get(SourceLayer::Processed, reloaded.cellIndex(44.0, -71.0));
  ASSERT_TRUE(processed.has_value());
  EXPECT_DOUBLE_EQ(processed->depth, -12.5);
  EXPECT_DOUBLE_EQ(processed->timestamp, 1.79e9);
}

TEST_F(TileIoTest, LoadedTilesAreCleanAndDontResave)
{
  BathymetryStore store(5);
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 1.0});

  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 1u);
  // No changes since the last save -> nothing re-written (incremental save).
  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 0u);

  // Reloaded tiles are clean, so saving the reloaded store writes nothing.
  BathymetryStore reloaded(5);
  marine_bathymetry_store::load(reloaded, dir_.string());
  EXPECT_EQ(marine_bathymetry_store::save(reloaded, dir_.string()), 0u);
}

TEST_F(TileIoTest, WritingAfterSaveRedirties)
{
  BathymetryStore store(5);
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 1.0});
  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 1u);

  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-31.0, 0.4, 2.0});
  EXPECT_EQ(marine_bathymetry_store::save(store, dir_.string()), 1u);
}

TEST_F(TileIoTest, LayersWriteToSeparateSubdirectories)
{
  BathymetryStore store(5);
  store.set(SourceLayer::Processed, store.cellIndex(43.0, -70.5), BathyCell{-10.0, 0.1, 1.0});
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-12.0, 0.5, 2.0});
  marine_bathymetry_store::save(store, dir_.string());

  EXPECT_TRUE(fs::is_directory(dir_ / "processed"));
  EXPECT_TRUE(fs::is_directory(dir_ / "draft"));
}

TEST_F(TileIoTest, LoadRejectsTilesFromAnotherLevel)
{
  BathymetryStore store(5);
  store.set(SourceLayer::Draft, store.cellIndex(43.0, -70.5), BathyCell{-30.0, 0.5, 1.0});
  marine_bathymetry_store::save(store, dir_.string());

  BathymetryStore wrong_level(6);
  EXPECT_THROW(marine_bathymetry_store::load(wrong_level, dir_.string()), std::runtime_error);
}
