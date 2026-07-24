// Tests for the pure datum resolution core (marine_vertical_datum::datum_config).
//
// Ported from mru_transform for issue #274 (ADR-0010 D6). Covers, in pure
// code: the precedence matrix
// (param-first / override-beats-VDatum / fallback-loses-to-VDatum /
// fallback-fills-gap / nothing→none, plus MHHW present vs absent), the
// point-in-polygon predicate (inside / outside / boundary / overlap), and
// config parsing (valid / missing / malformed / override default).

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "marine_vertical_datum/datum_config.hpp"

using marine_vertical_datum::DatumEntry;
using marine_vertical_datum::DatumSource;
using marine_vertical_datum::LatLon;
using marine_vertical_datum::VDatumResult;
using marine_vertical_datum::load_datum_config;
using marine_vertical_datum::point_in_ring;
using marine_vertical_datum::resolve_datum;

namespace
{

// A unit square ring [0,1] x [0,1] in (lat, lon).
std::vector<LatLon> unitSquare()
{
  return {{0.0, 0.0}, {0.0, 1.0}, {1.0, 1.0}, {1.0, 0.0}};
}

// One config entry covering a small area around (43, -71).
DatumEntry lakeEntry(bool override_vdatum, double z = -25.0)
{
  DatumEntry e;
  e.name = "lake";
  e.ring = {{42.0, -72.0}, {42.0, -70.0}, {44.0, -70.0}, {44.0, -72.0}};
  e.chart_datum_z = z;
  e.override_vdatum = override_vdatum;
  return e;
}

// Write text to a unique temp file; returns the path.
std::string writeTemp(const std::string & contents)
{
  char tmpl[] = "/tmp/datum_cfg_XXXXXX";
  const int fd = mkstemp(tmpl);
  EXPECT_GE(fd, 0) << "mkstemp failed";
  if (fd >= 0) {
    const ssize_t written = write(fd, contents.data(), contents.size());
    EXPECT_EQ(static_cast<size_t>(written), contents.size());
    close(fd);
  }
  return std::string(tmpl);
}

}  // namespace

// ---- point_in_ring -------------------------------------------------------

TEST(PointInRing, InsideAndOutside)
{
  const auto ring = unitSquare();
  EXPECT_TRUE(point_in_ring(0.5, 0.5, ring));
  EXPECT_FALSE(point_in_ring(2.0, 0.5, ring));
  EXPECT_FALSE(point_in_ring(0.5, -0.5, ring));
}

TEST(PointInRing, BoundaryCountsAsInside)
{
  const auto ring = unitSquare();
  EXPECT_TRUE(point_in_ring(0.0, 0.5, ring));   // on an edge
  EXPECT_TRUE(point_in_ring(0.0, 0.0, ring));   // on a vertex
  EXPECT_TRUE(point_in_ring(1.0, 1.0, ring));   // on a vertex
}

TEST(PointInRing, DegenerateRingIsNeverInside)
{
  std::vector<LatLon> two = {{0.0, 0.0}, {1.0, 1.0}};
  EXPECT_FALSE(point_in_ring(0.5, 0.5, two));
}

TEST(PointInRing, CollinearButOutsideSegmentIsNotInside)
{
  // Point on the line of the bottom edge (lat=0) but beyond the segment's
  // longitude range — must be rejected by the on-segment bounding-box check
  // and not counted as on-boundary.
  const auto ring = unitSquare();
  EXPECT_FALSE(point_in_ring(0.0, 2.0, ring));
  EXPECT_FALSE(point_in_ring(0.0, -1.0, ring));
}

TEST(PointInRing, VertexLatitudeDoesNotDoubleCount)
{
  // A horizontal ray at the latitude of the top vertices (lat=1.0) must not be
  // mis-counted by the half-open ray-cast convention. A point well to the left
  // at that latitude is outside; the top edge itself is boundary (inside).
  const auto ring = unitSquare();
  EXPECT_FALSE(point_in_ring(1.0, -1.0, ring));   // left of the ring, on top edge's lat
  EXPECT_TRUE(point_in_ring(1.0, 0.5, ring));     // on the top edge → inside
}

TEST(PointInRing, ConcaveRingExcludesTheNotch)
{
  // An L-shaped (concave) ring: the notch must read as outside even though it
  // is within the bounding box.
  std::vector<LatLon> ell = {
    {0.0, 0.0}, {0.0, 2.0}, {1.0, 2.0}, {1.0, 1.0}, {2.0, 1.0}, {2.0, 0.0}};
  EXPECT_TRUE(point_in_ring(0.5, 0.5, ell));    // in the solid corner
  EXPECT_FALSE(point_in_ring(1.5, 1.5, ell));   // in the removed notch
}

// ---- resolve_datum precedence -------------------------------------------

TEST(ResolveDatum, ParamWinsOutright)
{
  std::vector<DatumEntry> entries = {lakeEntry(/*override=*/true, -10.0)};
  VDatumResult vd{-3.0, std::nullopt};
  // Point is inside the override polygon AND has VDatum, yet param wins.
  auto r = resolve_datum(43.0, -71.0, /*lake=*/-7.5, /*lake_mhhw=*/-7.0, vd, entries);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->source, DatumSource::PARAM);
  EXPECT_DOUBLE_EQ(r->chart_datum_z, -7.5);
  ASSERT_TRUE(r->mhhw_z.has_value());
  EXPECT_DOUBLE_EQ(*r->mhhw_z, -7.0);
}

TEST(ResolveDatum, OverrideEntryBeatsVDatum)
{
  std::vector<DatumEntry> entries = {lakeEntry(/*override=*/true, -10.0)};
  VDatumResult vd{-3.0, std::nullopt};
  auto r = resolve_datum(43.0, -71.0, std::nullopt, std::nullopt, vd, entries);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->source, DatumSource::POLYGON_CONFIG);
  EXPECT_EQ(r->name, "lake");
  EXPECT_DOUBLE_EQ(r->chart_datum_z, -10.0);
}

TEST(ResolveDatum, FallbackEntryLosesToVDatum)
{
  std::vector<DatumEntry> entries = {lakeEntry(/*override=*/false, -10.0)};
  VDatumResult vd{-3.0, -2.5};
  auto r = resolve_datum(43.0, -71.0, std::nullopt, std::nullopt, vd, entries);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->source, DatumSource::VDATUM);
  EXPECT_DOUBLE_EQ(r->chart_datum_z, -3.0);
  ASSERT_TRUE(r->mhhw_z.has_value());
  EXPECT_DOUBLE_EQ(*r->mhhw_z, -2.5);
}

TEST(ResolveDatum, FallbackEntryFillsVDatumGap)
{
  std::vector<DatumEntry> entries = {lakeEntry(/*override=*/false, -10.0)};
  // No VDatum coverage here.
  auto r = resolve_datum(43.0, -71.0, std::nullopt, std::nullopt, std::nullopt, entries);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->source, DatumSource::POLYGON_CONFIG);
  EXPECT_DOUBLE_EQ(r->chart_datum_z, -10.0);
  EXPECT_FALSE(r->mhhw_z.has_value());
}

TEST(ResolveDatum, NothingMatchesYieldsNone)
{
  std::vector<DatumEntry> entries = {lakeEntry(/*override=*/false)};
  // Point outside the polygon, no VDatum, no param.
  auto r = resolve_datum(10.0, 10.0, std::nullopt, std::nullopt, std::nullopt, entries);
  EXPECT_FALSE(r.has_value());
}

TEST(ResolveDatum, EmptyEverythingYieldsNone)
{
  auto r = resolve_datum(43.0, -71.0, std::nullopt, std::nullopt, std::nullopt, {});
  EXPECT_FALSE(r.has_value());
}

TEST(ResolveDatum, FirstMatchWinsOnOverlap)
{
  DatumEntry a = lakeEntry(/*override=*/false, -10.0);
  a.name = "first";
  DatumEntry b = lakeEntry(/*override=*/false, -20.0);
  b.name = "second";
  std::vector<DatumEntry> entries = {a, b};
  auto r = resolve_datum(43.0, -71.0, std::nullopt, std::nullopt, std::nullopt, entries);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->name, "first");
}

// ---- load_datum_config ---------------------------------------------------

TEST(LoadDatumConfig, ValidFileParses)
{
  const std::string yaml =
    "datum_polygons:\n"
    "  - name: \"Lake A\"\n"
    "    chart_datum_z: -25.0\n"
    "    mhhw_z: -24.5\n"
    "    override: true\n"
    "    ring:\n"
    "      - [43.0, -71.4]\n"
    "      - [43.0, -71.3]\n"
    "      - [42.9, -71.3]\n"
    "      - [42.9, -71.4]\n";
  const std::string path = writeTemp(yaml);
  auto entries = load_datum_config(path);
  std::remove(path.c_str());

  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].name, "Lake A");
  EXPECT_DOUBLE_EQ(entries[0].chart_datum_z, -25.0);
  ASSERT_TRUE(entries[0].mhhw_z.has_value());
  EXPECT_DOUBLE_EQ(*entries[0].mhhw_z, -24.5);
  EXPECT_TRUE(entries[0].override_vdatum);
  EXPECT_EQ(entries[0].ring.size(), 4u);
}

TEST(LoadDatumConfig, OverrideAndMhhwDefault)
{
  const std::string yaml =
    "datum_polygons:\n"
    "  - name: \"Lake B\"\n"
    "    chart_datum_z: -5.0\n"
    "    ring:\n"
    "      - [1.0, 1.0]\n"
    "      - [1.0, 2.0]\n"
    "      - [2.0, 2.0]\n";
  const std::string path = writeTemp(yaml);
  auto entries = load_datum_config(path);
  std::remove(path.c_str());

  ASSERT_EQ(entries.size(), 1u);
  EXPECT_FALSE(entries[0].override_vdatum);   // default false
  EXPECT_FALSE(entries[0].mhhw_z.has_value());  // optional, omitted
}

TEST(LoadDatumConfig, MissingFileThrows)
{
  EXPECT_THROW(
    load_datum_config("/nonexistent/path/datum.yaml"), std::runtime_error);
}

TEST(LoadDatumConfig, MissingChartDatumZThrows)
{
  const std::string yaml =
    "datum_polygons:\n"
    "  - name: \"No Z\"\n"
    "    ring:\n"
    "      - [1.0, 1.0]\n"
    "      - [1.0, 2.0]\n"
    "      - [2.0, 2.0]\n";
  const std::string path = writeTemp(yaml);
  EXPECT_THROW(load_datum_config(path), std::runtime_error);
  std::remove(path.c_str());
}

TEST(LoadDatumConfig, TooFewRingPointsThrows)
{
  const std::string yaml =
    "datum_polygons:\n"
    "  - name: \"Sliver\"\n"
    "    chart_datum_z: -5.0\n"
    "    ring:\n"
    "      - [1.0, 1.0]\n"
    "      - [1.0, 2.0]\n";
  const std::string path = writeTemp(yaml);
  EXPECT_THROW(load_datum_config(path), std::runtime_error);
  std::remove(path.c_str());
}

TEST(LoadDatumConfig, MissingTopLevelKeyThrows)
{
  const std::string path = writeTemp("some_other_key: 1\n");
  EXPECT_THROW(load_datum_config(path), std::runtime_error);
  std::remove(path.c_str());
}

TEST(LoadDatumConfig, MalformedRingVertexThrowsRuntimeError)
{
  // A ring vertex that is not a 2-element [lat, lon] sequence.
  const std::string yaml =
    "datum_polygons:\n"
    "  - name: \"Bad Vertex\"\n"
    "    chart_datum_z: -5.0\n"
    "    ring:\n"
    "      - [1.0, 1.0]\n"
    "      - [1.0]\n"
    "      - [2.0, 2.0]\n";
  const std::string path = writeTemp(yaml);
  EXPECT_THROW(load_datum_config(path), std::runtime_error);
  std::remove(path.c_str());
}

TEST(LoadDatumConfig, NonNumericChartDatumZThrowsRuntimeError)
{
  // A yaml-cpp type-conversion failure must surface as std::runtime_error,
  // per the function's documented contract (not a raw YAML::Exception).
  const std::string yaml =
    "datum_polygons:\n"
    "  - name: \"Bad Z\"\n"
    "    chart_datum_z: \"not-a-number\"\n"
    "    ring:\n"
    "      - [1.0, 1.0]\n"
    "      - [1.0, 2.0]\n"
    "      - [2.0, 2.0]\n";
  const std::string path = writeTemp(yaml);
  EXPECT_THROW(load_datum_config(path), std::runtime_error);
  std::remove(path.c_str());
}
