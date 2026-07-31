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

#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "s102/fetch.hpp"

namespace fs = std::filesystem;
using marine_bathymetry_store::s102::sha256File;
using marine_bathymetry_store::s102::TileCache;
using marine_bathymetry_store::s102::TileRecord;

namespace
{

class S102FetchTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    root_ = fs::temp_directory_path() / "s102_fetch_test";
    fs::remove_all(root_);
    fs::create_directories(root_ / "remote");
    cache_dir_ = (root_ / "cache").string();
  }
  void TearDown() override {fs::remove_all(root_);}

  /// Create a "remote" payload and a matching catalog record.
  TileRecord makeRemoteTile(
    const std::string & name, const std::string & content,
    const std::string & issuance)
  {
    const fs::path payload = root_ / "remote" / name;
    std::ofstream(payload) << content;
    TileRecord record;
    record.tile_id = name;
    record.url = "file://" + payload.string();
    record.sha256 = sha256File(payload.string());
    record.issuance = issuance;
    record.resolution_m = 4.0;
    return record;
  }

  fs::path root_;
  std::string cache_dir_;
};

}  // namespace

TEST(S102Sha256, known_vector)
{
  // FIPS 180-2 test vector: SHA256("abc").
  const fs::path path = fs::temp_directory_path() / "s102_sha_abc";
  std::ofstream(path) << "abc";
  EXPECT_EQ(
    sha256File(path.string()),
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  fs::remove(path);
  EXPECT_THROW(sha256File("/nonexistent/file"), std::runtime_error);
}

TEST_F(S102FetchTest, fetch_verify_and_cache_hit)
{
  TileCache cache(cache_dir_);
  const auto record = makeRemoteTile("TILE_A.h5", "payload-a", "2026-01-01");

  const auto first = cache.fetch(record);
  EXPECT_TRUE(first.downloaded);
  EXPECT_TRUE(fs::exists(first.local_path));
  EXPECT_EQ(sha256File(first.local_path), record.sha256);

  // Same issuance → verified cache hit, no download.
  const auto second = cache.fetch(record);
  EXPECT_FALSE(second.downloaded);
  EXPECT_EQ(second.local_path, first.local_path);

  // The registry sidecar is tiles.json (NOT registry.json — that name
  // belongs to the store's own StoreMetadata sidecar).
  EXPECT_TRUE(fs::exists(fs::path(cache_dir_) / "tiles.json"));
  EXPECT_FALSE(fs::exists(fs::path(cache_dir_) / "registry.json"));
}

TEST_F(S102FetchTest, corrupted_cache_is_refetched)
{
  TileCache cache(cache_dir_);
  const auto record = makeRemoteTile("TILE_B.h5", "payload-b", "2026-01-01");
  const auto first = cache.fetch(record);

  // Corrupt the cached payload behind the registry's back.
  std::ofstream(first.local_path, std::ios::app) << "garbage";
  const auto again = cache.fetch(record);
  EXPECT_TRUE(again.downloaded);  // hash mismatch on access → refetched
  EXPECT_EQ(sha256File(again.local_path), record.sha256);
}

TEST_F(S102FetchTest, digest_mismatch_hard_fails)
{
  TileCache cache(cache_dir_);
  auto record = makeRemoteTile("TILE_C.h5", "payload-c", "2026-01-01");
  record.sha256 = std::string(64, '0');  // catalog says a different digest
  EXPECT_THROW(cache.fetch(record), std::runtime_error);
  // The failed payload must not land in the cache.
  EXPECT_FALSE(fs::exists(fs::path(cache_dir_) / "tiles" / "TILE_C.h5"));
}

TEST_F(S102FetchTest, new_issuance_refetches)
{
  TileCache cache(cache_dir_);
  auto record = makeRemoteTile("TILE_D.h5", "payload-d1", "2026-01-01");
  EXPECT_TRUE(cache.fetch(record).downloaded);

  // Upstream reissues the tile: new content, new digest, new issuance.
  auto updated = makeRemoteTile("TILE_D.h5", "payload-d2", "2026-02-02");
  const auto result = cache.fetch(updated);
  EXPECT_TRUE(result.downloaded);
  EXPECT_EQ(sha256File(result.local_path), updated.sha256);
}

TEST_F(S102FetchTest, offline_miss_and_hit)
{
  TileCache cache(cache_dir_);
  const auto record = makeRemoteTile("TILE_E.h5", "payload-e", "2026-01-01");

  // Not cached yet → offline is a hard error naming the tile.
  EXPECT_THROW(cache.fetch(record, true), std::runtime_error);

  cache.fetch(record);
  const auto offline = cache.fetch(record, true);
  EXPECT_FALSE(offline.downloaded);
}

TEST_F(S102FetchTest, corrupt_registry_is_surfaced)
{
  {
    TileCache cache(cache_dir_);
    cache.fetch(makeRemoteTile("TILE_F.h5", "payload-f", "2026-01-01"));
  }
  std::ofstream(fs::path(cache_dir_) / "tiles.json") << "{not json";
  EXPECT_THROW(TileCache{cache_dir_}, std::runtime_error);
}
