// test_vdatum_query.cpp — the VDatumQueryFn seam and the production
// factory's grids-free failure paths. No PROJ grids are needed on CI: the
// seam tests inject stubs, and the factory tests only exercise setup
// validation (which fails before any grid is read).

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "marine_vertical_datum/datum_config.hpp"
#include "marine_vertical_datum/vdatum_query.hpp"

namespace mvd = marine_vertical_datum;
namespace fs = std::filesystem;

namespace
{

// A square fallback polygon around the origin.
std::vector<mvd::DatumEntry> fallback_square()
{
  mvd::DatumEntry entry;
  entry.name = "fallback";
  entry.chart_datum_z = -10.0;
  entry.override_vdatum = false;
  entry.ring = {
    {-1.0, -1.0}, {-1.0, 1.0}, {1.0, 1.0}, {1.0, -1.0},
  };
  return {entry};
}

}  // namespace

// An injected stub result reaches resolve_datum as the VDatum step and wins
// over a covering fallback polygon (precedence step 3 before step 4).
TEST(VDatumQuerySeam, StubResultBeatsFallbackPolygon)
{
  mvd::VDatumQueryFn stub = [](double, double) {
      mvd::VDatumResult r;
      r.mllw_z = -48.88;
      r.mhhw_z = -47.5;
      return std::optional<mvd::VDatumResult>(r);
    };

  const auto result = mvd::resolve_datum(
    0.0, 0.0, std::nullopt, std::nullopt, stub(0.0, 0.0), fallback_square());

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->source, mvd::DatumSource::VDATUM);
  EXPECT_DOUBLE_EQ(result->chart_datum_z, -48.88);
  ASSERT_TRUE(result->mhhw_z.has_value());
  EXPECT_DOUBLE_EQ(*result->mhhw_z, -47.5);
}

// A stub nullopt (no coverage) falls through to the fallback polygon —
// the gap-fill path the polygon config exists for.
TEST(VDatumQuerySeam, StubNulloptFallsThroughToPolygon)
{
  mvd::VDatumQueryFn stub = [](double, double) {
      return std::optional<mvd::VDatumResult>();
    };

  const auto result = mvd::resolve_datum(
    0.0, 0.0, std::nullopt, std::nullopt, stub(0.0, 0.0), fallback_square());

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->source, mvd::DatumSource::POLYGON_CONFIG);
  EXPECT_EQ(result->name, "fallback");
  EXPECT_DOUBLE_EQ(result->chart_datum_z, -10.0);
}

// Empty config fields fail setup with a diagnostic and an empty callable.
TEST(MakeVDatumQuery, EmptyConfigFailsWithDiagnostic)
{
  std::vector<std::string> messages;
  const auto query = mvd::make_vdatum_query(
    mvd::VDatumConfig{}, [&messages](const std::string & m) {
      messages.push_back(m);
    });

  EXPECT_FALSE(static_cast<bool>(query));
  ASSERT_FALSE(messages.empty());
  EXPECT_NE(messages.front().find("geoid_grid"), std::string::npos);
}

// A nonexistent grid directory fails setup (scan error), not crash.
TEST(MakeVDatumQuery, MissingGridDirFailsWithDiagnostic)
{
  std::vector<std::string> messages;
  const auto query = mvd::make_vdatum_query(
    mvd::VDatumConfig{"geoid.tif", "/nonexistent/vdatum/dir"},
    [&messages](const std::string & m) {messages.push_back(m);});

  EXPECT_FALSE(static_cast<bool>(query));
  ASSERT_FALSE(messages.empty());
  EXPECT_NE(messages.front().find("scanning"), std::string::npos);
}

// An existing directory with no *_mllw.gtx grids fails setup with the
// no-grids diagnostic. Unique per-run directory (mkdtemp) so concurrent
// test runs on one host (multi-worktree CI) can't collide.
TEST(MakeVDatumQuery, EmptyGridDirFailsWithDiagnostic)
{
  std::string tmpl =
    (fs::temp_directory_path() / "mvd_test_empty_grid_dir.XXXXXX").string();
  ASSERT_NE(mkdtemp(tmpl.data()), nullptr) << "mkdtemp failed";
  const fs::path dir(tmpl);

  std::vector<std::string> messages;
  const auto query = mvd::make_vdatum_query(
    mvd::VDatumConfig{"geoid.tif", dir.string()},
    [&messages](const std::string & m) {messages.push_back(m);});

  EXPECT_FALSE(static_cast<bool>(query));
  ASSERT_FALSE(messages.empty());
  EXPECT_NE(messages.front().find("_mllw"), std::string::npos);

  fs::remove_all(dir);
}

// The default (empty) DiagFn is safe: failures stay silent, no crash.
TEST(MakeVDatumQuery, DefaultDiagIsSafe)
{
  const auto query = mvd::make_vdatum_query(mvd::VDatumConfig{});
  EXPECT_FALSE(static_cast<bool>(query));
}
