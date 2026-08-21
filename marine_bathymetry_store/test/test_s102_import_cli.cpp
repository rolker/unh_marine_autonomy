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

/// @file
/// @brief Subprocess test of the built `s102_import` binary — `--cache`
/// resolution (#288).
///
/// The pipeline stages have their own offline library tests (test_s102_*.cpp);
/// this suite runs the actual binary (path injected via the `S102_IMPORT_BINARY`
/// compile definition) to pin the `--cache` argument contract that #288 changed:
/// omitting `--cache` now falls back to the canonical import cache
/// `~/data/world/s100/s102` (ADR-0010 D3 as amended by #288) instead of being a
/// usage error, while an explicit `--cache` still overrides. Every case runs
/// `--offline` so nothing touches the network: the offline-with-no-cached-catalog
/// failure fails loud and names the *resolved* cache path, which is exactly the
/// observable we assert on to tell "default" from "override" apart.

#include <gtest/gtest.h>

#include <sys/wait.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

namespace
{

/// A minimal, valid, offline invocation missing only the `--cache` flag: a
/// finite in-order area, a scratch store, a constant datum, and `--offline`.
/// Every required arg other than `--cache` is present, so whatever fails is the
/// cache/offline path — not arg validation.
std::string baseArgs(const fs::path & store)
{
  return "--area -70.75,42.90,-70.55,43.05"
         " --store \"" + store.string() + "\""
         " --datum constant:0"
         " --offline";
}

}  // namespace

class S102ImportCliTest : public ::testing::Test
{
protected:
  fs::path dir_;
  fs::path home_;
  fs::path store_;

  void SetUp() override
  {
    const std::string name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
    dir_ = fs::temp_directory_path() / ("marine_bathy_s102_cli_" + name);
    fs::remove_all(dir_);
    fs::create_directories(dir_);
    // A sandboxed $HOME so the default cache resolves under the temp tree and
    // never writes to the real ~/data/world.
    home_ = dir_ / "home";
    fs::create_directories(home_);
    store_ = dir_ / "store";
  }

  void TearDown() override
  {
    fs::remove_all(dir_);
  }

  /// Run the binary with @p args (space-joined) under @p env (a shell env
  /// prefix, e.g. `HOME="..."` or `env -u HOME`), stdout+stderr redirected to a
  /// per-call log under dir_. Returns the process exit status, or -1 if the
  /// process did not exit normally.
  int run(const std::string & env, const std::string & args)
  {
    const std::string cmd =
      env + " " + std::string(S102_IMPORT_BINARY) + " " + args +
      " > \"" + log().string() + "\" 2>&1";
    const int rc = std::system(cmd.c_str());
    if (rc == -1 || !WIFEXITED(rc)) {
      return -1;
    }
    return WEXITSTATUS(rc);
  }

  fs::path log() const {return dir_ / "cli.log";}

  std::string readLog() const
  {
    std::ifstream in(log());
    return std::string(
      (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }
};

TEST_F(S102ImportCliTest, OmittedCacheResolvesCanonicalDefaultUnderHome)
{
  // No --cache: the tool falls back to $HOME/data/world/s100/s102 (canonical,
  // ADR-0010 D3 as amended by #288). --offline with an empty cache then fails
  // loud naming the resolved catalog path, which proves where the default
  // landed. run() maps an abnormal exit to -1, so EXPECT_EQ(..., 1) keeps a
  // crash from masquerading as the offline usage error.
  EXPECT_EQ(run("HOME=\"" + home_.string() + "\"", baseArgs(store_)), 1);
  const std::string out = readLog();
  const std::string expected =
    (home_ / "data/world/s100/s102/catalog.gpkg").string();
  EXPECT_NE(out.find(expected), std::string::npos)
    << "expected the default cache to resolve to " << expected << ", got: " << out;
}

TEST_F(S102ImportCliTest, ExplicitCacheOverridesDefault)
{
  // An explicit --cache wins over the canonical default: the offline failure
  // must name the override path, and must NOT mention the ~/data/world default.
  const fs::path override_cache = dir_ / "explicit_cache";
  EXPECT_EQ(
    run(
      "HOME=\"" + home_.string() + "\"",
      baseArgs(store_) + " --cache \"" + override_cache.string() + "\""),
    1);
  const std::string out = readLog();
  EXPECT_NE(out.find((override_cache / "catalog.gpkg").string()), std::string::npos)
    << "expected the explicit --cache to be honored, got: " << out;
  EXPECT_EQ(out.find("data/world/s100/s102"), std::string::npos)
    << "explicit --cache must not fall back to the default, got: " << out;
}

TEST_F(S102ImportCliTest, OmittedCacheWithUnsetHomeFailsLoud)
{
  // The default is anchored on $HOME; with HOME unset and no --cache the tool
  // must fail loud rather than write a relative "data/world/..." under the CWD.
  EXPECT_EQ(run("env -u HOME", baseArgs(store_)), 1);
  const std::string out = readLog();
  EXPECT_NE(out.find("HOME is unset"), std::string::npos)
    << "expected a fail-loud unset-HOME diagnostic, got: " << out;
}

TEST_F(S102ImportCliTest, StillRequiredArgsRemainRequired)
{
  // #288 made only --cache optional; --area/--store/--datum stay required.
  // Omitting --store (still required) is a usage error (exit 1), proving the
  // required-arg set was not loosened beyond --cache.
  EXPECT_EQ(
    run(
      "HOME=\"" + home_.string() + "\"",
      "--area -70.75,42.90,-70.55,43.05 --datum constant:0 --offline"),
    1);
}
