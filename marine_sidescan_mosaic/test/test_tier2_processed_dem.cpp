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
/// @brief End-to-end subprocess test of the built `sidescan_tier2_processed`
///   binary's DEM orthorectification path (#297).
///
/// The package had no Tier-2 test and no committed `.sst1` fixture, so the
/// input is authored in-test through the library's own `writeTier1Header` /
/// `writeTier1Ping` — no binary fixture enters the repository. The bathy store
/// is written with `marine_tiled_raster_store::saveTile<double>` (the on-disk
/// contract `marine_bathymetry_store` writes), so the test takes no
/// `marine_bathymetry_store` dependency — ADR-0006 D9's decoupling.
///
/// Covered: DEM placement differs from flat in the direction the slope implies;
/// the flat code path is unchanged (a no-`--bathy-store` run and a
/// zero-coverage DEM run produce byte-identical tiles); an unusable store
/// hard-fails; the `--min-dem-coverage` gate exits 3 and writes nothing; and
/// `--accumulate` refuses to mix projection modes.

#include <gtest/gtest.h>

#include <sys/wait.h>
#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "geodesy/ecef.h"
#include "geographic_msgs/msg/geo_point.hpp"
#include "marine_autonomy/gggs.h"
#include "marine_sidescan_mosaic/bathy_dem.hpp"
#include "marine_sidescan_mosaic/tier1.hpp"
#include "marine_tiled_raster_store/tile_io.hpp"
#include "marine_tiled_raster_store/tiled_raster_tile.hpp"

namespace fs = std::filesystem;
namespace mtrs = marine_tiled_raster_store;
namespace msm = marine_sidescan_mosaic;

namespace
{

constexpr int kOutLevel = 13;                     // the tool's default output level
constexpr int kBathyLevel = 10;                   // ~0.9 m cells, as the real store has
constexpr double kLat = 43.07, kLon = -71.42;     // Lake Massabesic — non-polar
constexpr double kFarLat = 10.0, kFarLon = 10.0;  // a store that covers a different ocean
constexpr double kSensorHeight = 0.0;             // WGS84 ellipsoidal height of the sensor
constexpr double kNadirAltitude = 10.0;           // altimeter height above bottom (m)
constexpr double kSlope = 0.2;                    // bottom deepens 0.2 m per metre east
constexpr double kShelfEastM = 25.0;              // degenerate-geometry shelf (see writeBathyStore)
constexpr int kSampleCount = 400;
constexpr int kSample0 = 200;
constexpr double kSoundSpeed = 1500.0, kSampleRate = 10000.0;

const double kNaN = std::numeric_limits<double>::quiet_NaN();

double metresPerDegreeLon(double lat_deg)
{
  return 111320.0 * std::cos(lat_deg * M_PI / 180.0);
}

/// Synthetic bottom: ellipsoidal height at (@p lat, @p lon), deepening eastward
/// from the survey origin so the DEM correction pulls samples IN from their
/// flat-bottom placement (a direction the test can assert).
double bottomHeight(double origin_lat, double origin_lon, double lat, double lon)
{
  (void)lat;
  const double east_m = (lon - origin_lon) * metresPerDegreeLon(origin_lat);
  return -kNadirAltitude - kSlope * east_m;
}

/// body->ECEF quaternion for a level, north-heading vessel carrying a starboard
/// sidescan whose +Z (range) axis points abeam — so the beam azimuth is due east
/// and the along-track heading is due north.
void starboardBeamQuaternion(
  double lat_deg, double lon_deg, double & qx, double & qy, double & qz, double & qw)
{
  const double lat = lat_deg * M_PI / 180.0;
  const double lon = lon_deg * M_PI / 180.0;
  const double sla = std::sin(lat), cla = std::cos(lat);
  const double slo = std::sin(lon), clo = std::cos(lon);
  // ECEF->ENU, then ENU->NED.
  const double r_ecef_enu[9] = {
    -slo, clo, 0.0, -sla * clo, -sla * slo, cla, cla * clo, cla * slo, sla};
  const double r_enu_ned[9] = {0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, -1.0};
  double r_ecef_ned[9] = {};
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      double s = 0.0;
      for (int k = 0; k < 3; ++k) {
        s += r_enu_ned[i * 3 + k] * r_ecef_enu[k * 3 + j];
      }
      r_ecef_ned[i * 3 + j] = s;
    }
  }
  // Sensor body->NED at zero attitude: x=forward(N), y=up, z=abeam-starboard(E).
  const double r_body_ned[9] = {1, 0, 0, 0, 0, 1, 0, -1, 0};
  // body->ECEF = (ECEF->NED)^T * (body->NED)
  double m[9] = {};
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      double s = 0.0;
      for (int k = 0; k < 3; ++k) {
        s += r_ecef_ned[k * 3 + i] * r_body_ned[k * 3 + j];
      }
      m[i * 3 + j] = s;
    }
  }
  const double trace = m[0] + m[4] + m[8];
  if (trace > 0.0) {
    const double s = 0.5 / std::sqrt(trace + 1.0);
    qw = 0.25 / s;
    qx = (m[7] - m[5]) * s;
    qy = (m[2] - m[6]) * s;
    qz = (m[3] - m[1]) * s;
  } else if (m[0] > m[4] && m[0] > m[8]) {
    const double s = 2.0 * std::sqrt(1.0 + m[0] - m[4] - m[8]);
    qw = (m[7] - m[5]) / s;
    qx = 0.25 * s;
    qy = (m[1] + m[3]) / s;
    qz = (m[2] + m[6]) / s;
  } else if (m[4] > m[8]) {
    const double s = 2.0 * std::sqrt(1.0 + m[4] - m[0] - m[8]);
    qw = (m[2] - m[6]) / s;
    qx = (m[1] + m[3]) / s;
    qy = 0.25 * s;
    qz = (m[5] + m[7]) / s;
  } else {
    const double s = 2.0 * std::sqrt(1.0 + m[8] - m[0] - m[4]);
    qw = (m[3] - m[1]) / s;
    qx = (m[2] + m[6]) / s;
    qy = (m[5] + m[7]) / s;
    qz = 0.25 * s;
  }
}

/// Author a Tier-1 archive: @p n_pings starboard pings on a northbound track
/// through (@p lat, @p lon), point-deposited (no beamwidth) for determinism.
void writeTier1Fixture(const fs::path & path, double lat, double lon, int n_pings)
{
  std::ofstream out(path, std::ios::binary);
  ASSERT_TRUE(out.good()) << "cannot write " << path;
  msm::writeTier1Header(out);
  for (int i = 0; i < n_pings; ++i) {
    const double ping_lat = lat + static_cast<double>(i) * 0.5 / 111320.0;
    geographic_msgs::msg::GeoPoint position;
    position.latitude = ping_lat;
    position.longitude = lon;
    position.altitude = kSensorHeight;
    geodesy::ECEFPoint ecef;
    geodesy::fromMsg(position, ecef);

    msm::Tier1Ping ping;
    ping.stamp_ns = 1000000000LL * i;
    ping.channel = msm::Tier1Channel::Starboard;
    ping.tx = ecef.x;
    ping.ty = ecef.y;
    ping.tz = ecef.z;
    starboardBeamQuaternion(ping_lat, lon, ping.qx, ping.qy, ping.qz, ping.qw);
    ping.sound_speed = kSoundSpeed;
    ping.sample_rate = kSampleRate;
    ping.sample0 = kSample0;
    ping.nadir_altitude_m = static_cast<float>(kNadirAltitude);
    ping.tx_beamwidth_rad = 0.0F;   // point-deposit: one cell per sample
    ping.samples.resize(kSampleCount);
    for (int j = 0; j < kSampleCount; ++j) {
      ping.samples[j] = 1000.0F + static_cast<float>(j);
    }
    msm::writeTier1Ping(out, ping);
  }
}

/// Write a bathy `survey/` layer covering the ~±110 m neighbourhood of
/// (@p lat, @p lon) with the eastward-deepening plane of @ref bottomHeight.
///
/// @param shelf_above_sensor when true, every cell more than @ref kShelfEastM east
///   of the origin is given a height ABOVE the sensor — DEM data is present, but
///   the geometry is degenerate there, so those samples fall back to flat
///   placement. Covered-but-degenerate is the case the coverage gate must count.
void writeBathyStore(
  const fs::path & root, double lat, double lon, bool shelf_above_sensor = false)
{
  const gggs::Level level(kBathyLevel);
  const fs::path layer_dir = root / "survey";
  fs::create_directories(layer_dir);

  // The swath is ~45 m; sample a lattice over ±0.001 deg (~±110 m) so the grids
  // straddling the origin are all written whatever side of an edge it lands on.
  std::set<std::pair<uint32_t, uint32_t>> seen;
  std::vector<gggs::GridIndex> grids;
  for (int i = -2; i <= 2; ++i) {
    for (int j = -2; j <= 2; ++j) {
      const auto grid = level.gridIndex(lat + i * 0.0005, lon + j * 0.0005);
      if (seen.insert({grid.row(), grid.column()}).second) {
        grids.push_back(grid);
      }
    }
  }

  for (const auto & grid : grids) {
    mtrs::TiledRasterTile<double> tile(grid, msm::BathyDem::kBands, kNaN);
    const double lat_span = grid.latitudinalSpan() / gggs::cell_rows_per_grid;
    const double lon_span = grid.longitudinalSpan() / gggs::cell_columns_per_grid;
    for (uint16_t r = 0; r < tile.edge; ++r) {
      const double cell_lat = grid.southLatitude() + (r + 0.5) * lat_span;
      for (uint16_t c = 0; c < tile.edge; ++c) {
        const double cell_lon = grid.westLongitude() + (c + 0.5) * lon_span;
        const double east_m = (cell_lon - lon) * metresPerDegreeLon(lat);
        const double height = (shelf_above_sensor && east_m > kShelfEastM) ?
          kSensorHeight + 1.0 : bottomHeight(lat, lon, cell_lat, cell_lon);
        tile.set(r, c, 0, height);
        tile.set(r, c, 1, 0.25);
      }
    }
    mtrs::saveTile<double>(
      tile, (layer_dir / mtrs::tileFilename(grid)).string(), {kNaN, kNaN});
  }
}

/// The easternmost longitude of any painted (non-zero intensity) cell in a
/// processed store — the far edge of the swath, which the DEM correction moves.
double maxPaintedLongitude(const fs::path & dir)
{
  std::map<gggs::GridIndex, mtrs::TiledRasterTile<std::uint16_t>> tiles;
  mtrs::loadTiles<std::uint16_t>(tiles, dir.string(), gggs::Level(kOutLevel), 3);
  double max_lon = -std::numeric_limits<double>::infinity();
  for (const auto & kv : tiles) {
    const auto & grid = kv.first;
    const double lon_span = grid.longitudinalSpan() / gggs::cell_columns_per_grid;
    for (uint16_t r = 0; r < kv.second.edge; ++r) {
      for (uint16_t c = 0; c < kv.second.edge; ++c) {
        if (kv.second.get(r, c, 0) != 0) {
          max_lon = std::max(max_lon, grid.westLongitude() + (c + 0.5) * lon_span);
        }
      }
    }
  }
  return max_lon;
}

/// Per-cell quality band (band 1) of a processed store, keyed by absolute cell
/// so two stores can be compared cell for cell.
std::map<std::tuple<uint32_t, uint32_t, uint16_t, uint16_t>, std::uint16_t> qualityByCell(
  const fs::path & dir)
{
  std::map<gggs::GridIndex, mtrs::TiledRasterTile<std::uint16_t>> tiles;
  mtrs::loadTiles<std::uint16_t>(tiles, dir.string(), gggs::Level(kOutLevel), 3);
  std::map<std::tuple<uint32_t, uint32_t, uint16_t, uint16_t>, std::uint16_t> quality;
  for (const auto & kv : tiles) {
    for (uint16_t r = 0; r < kv.second.edge; ++r) {
      for (uint16_t c = 0; c < kv.second.edge; ++c) {
        if (kv.second.get(r, c, 0) != 0) {
          quality[{kv.first.row(), kv.first.column(), r, c}] = kv.second.get(r, c, 1);
        }
      }
    }
  }
  return quality;
}

std::set<std::string> tileNames(const fs::path & dir)
{
  std::set<std::string> names;
  if (!fs::is_directory(dir)) {
    return names;
  }
  for (const auto & entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() == ".tif") {
      names.insert(entry.path().filename().string());
    }
  }
  return names;
}

std::string readFile(const fs::path & path)
{
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

class Tier2ProcessedDemTest : public ::testing::Test
{
protected:
  fs::path dir_;
  fs::path tier1_;
  fs::path store_;      ///< bathy store overlapping the survey
  fs::path far_store_;  ///< a valid store that covers somewhere else entirely

  void SetUp() override
  {
    const std::string name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
    dir_ = fs::temp_directory_path() / ("msm_tier2_dem_" + name);
    fs::remove_all(dir_);
    fs::create_directories(dir_);
    tier1_ = dir_ / "fixture.sst1";
    writeTier1Fixture(tier1_, kLat, kLon, 4);
    store_ = dir_ / "bathy";
    writeBathyStore(store_, kLat, kLon);
    far_store_ = dir_ / "bathy_far";
    writeBathyStore(far_store_, kFarLat, kFarLon);
  }

  void TearDown() override
  {
    std::error_code ec;
    fs::remove_all(dir_, ec);
  }

  /// Run the tool with @p args appended to `<tier1> <out_dir>`; stdout+stderr go
  /// to `<out_dir_name>.log` under dir_ (readable via @ref log).
  int run(const fs::path & out_dir, const std::string & args = "")
  {
    last_log_ = dir_ / (out_dir.filename().string() + ".log");
    const std::string cmd = std::string(SIDESCAN_TIER2_PROCESSED_BINARY) +
      " \"" + tier1_.string() + "\" \"" + out_dir.string() + "\" " + args +
      " > \"" + last_log_.string() + "\" 2>&1";
    const int rc = std::system(cmd.c_str());
    if (rc == -1 || !WIFEXITED(rc)) {
      return -1;
    }
    return WEXITSTATUS(rc);
  }

  std::string log() const {return readFile(last_log_);}

  fs::path last_log_;
};

// A DEM run must move samples, and move them the way the bottom slopes: an
// eastward-deepening bottom shortens the ground range, pulling the far edge of
// the swath IN from where the flat-bottom assumption put it.
TEST_F(Tier2ProcessedDemTest, DemPlacementDiffersFromFlatInTheSlopeDirection)
{
  const fs::path flat_out = dir_ / "flat";
  const fs::path dem_out = dir_ / "dem";
  ASSERT_EQ(run(flat_out), 0) << log();
  ASSERT_EQ(run(dem_out, "--bathy-store \"" + store_.string() + "\""), 0) << log();

  const std::string dem_log = log();
  EXPECT_NE(dem_log.find("projection=dem"), std::string::npos) << dem_log;

  const double flat_lon = maxPaintedLongitude(flat_out);
  const double dem_lon = maxPaintedLongitude(dem_out);
  ASSERT_GT(flat_lon, -1e30);
  ASSERT_GT(dem_lon, -1e30);
  const double shift_m = (flat_lon - dem_lon) * metresPerDegreeLon(kLat);
  // Analytically ~2.7 m at the far edge for this slope; assert metres, not cells.
  EXPECT_GT(shift_m, 1.0) << "flat_lon=" << flat_lon << " dem_lon=" << dem_lon;

  // The corrected (vertical, horizontal) pair also reaches grazingQuality at the
  // TOOL's call site, not just in the geometry unit test: some cell painted by
  // both runs must carry a different quality score.
  const auto flat_quality = qualityByCell(flat_out);
  const auto dem_quality = qualityByCell(dem_out);
  std::size_t shared = 0, differing = 0;
  for (const auto & kv : flat_quality) {
    const auto found = dem_quality.find(kv.first);
    if (found != dem_quality.end()) {
      ++shared;
      differing += (found->second != kv.second) ? 1 : 0;
    }
  }
  ASSERT_GT(shared, 0u) << "the two runs painted no cell in common";
  EXPECT_GT(differing, 0u) << "quality is identical in every shared cell — the "
                              "DEM-corrected pair is not reaching grazingQuality";

  // The DEM was actually consulted and mostly succeeded.
  EXPECT_NE(dem_log.find("dem hit="), std::string::npos) << dem_log;
  EXPECT_EQ(dem_log.find("dem hit=0 "), std::string::npos) << dem_log;
  // Same datum on both sides of the subtraction ⇒ the cross-check is ~zero, so
  // the 1.0 m warning must not fire.
  EXPECT_EQ(dem_log.find("warning: mean datum discrepancy"), std::string::npos) << dem_log;
}

// Backward compatibility, proven between two live runs (the package commits no
// golden tiles): omitting --bathy-store and running a DEM that never has data
// must produce the same store, byte for byte.
TEST_F(Tier2ProcessedDemTest, FlatPathUnchangedByTheDemCodePath)
{
  const fs::path flat_out = dir_ / "flat";
  const fs::path nocov_out = dir_ / "nocov";
  ASSERT_EQ(run(flat_out), 0) << log();
  const std::string flat_log = log();
  ASSERT_EQ(
    run(
      nocov_out,
      "--bathy-store \"" + far_store_.string() + "\" --min-dem-coverage 0"), 0) << log();
  const std::string nocov_log = log();

  const auto flat_names = tileNames(flat_out);
  ASSERT_FALSE(flat_names.empty());
  EXPECT_EQ(flat_names, tileNames(nocov_out));
  for (const auto & name : flat_names) {
    EXPECT_EQ(readFile(flat_out / name), readFile(nocov_out / name)) << "tile " << name;
  }

  // The flat run reports no DEM counters at all; the zero-coverage run reports
  // them and says so loudly rather than looking like a normal DEM build.
  EXPECT_EQ(flat_log.find("dem hit="), std::string::npos) << flat_log;
  EXPECT_NE(flat_log.find("projection=flat"), std::string::npos) << flat_log;
  EXPECT_NE(nocov_log.find("dem hit=0 "), std::string::npos) << nocov_log;
  EXPECT_NE(nocov_log.find("warning: DEM coverage"), std::string::npos) << nocov_log;
}

// A --bathy-store that cannot be used is a hard failure at startup, never a
// silent whole-run fallback to flat placement.
TEST_F(Tier2ProcessedDemTest, UnusableBathyStoreFailsFast)
{
  const fs::path out = dir_ / "bad_store";
  EXPECT_NE(run(out, "--bathy-store \"" + (dir_ / "nonexistent").string() + "\""), 0);
  EXPECT_TRUE(tileNames(out).empty());

  // A real directory whose requested layer is absent fails the same way.
  const fs::path empty_store = dir_ / "empty_store";
  fs::create_directories(empty_store);
  const fs::path out2 = dir_ / "bad_layers";
  EXPECT_NE(run(out2, "--bathy-store \"" + empty_store.string() + "\""), 0);
  EXPECT_TRUE(tileNames(out2).empty());
}

// A value-taking flag left without its value (a shell slip that ate the path)
// must fail loudly: the operator asked for orthorectification, and a silent
// fall-through to the default would write a full flat-bottom store, exit 0, and
// mark it "flat" in the sidecar.
TEST_F(Tier2ProcessedDemTest, ValueFlagWithoutItsValueIsAnArgumentError)
{
  const fs::path out = dir_ / "truncated";
  EXPECT_EQ(run(out, "--bathy-store"), 2) << log();
  EXPECT_NE(log().find("requires a value"), std::string::npos) << log();
  EXPECT_TRUE(tileNames(out).empty());
  EXPECT_FALSE(fs::exists(out / "registry.json"));

  // The same slip one slot along: the next argument is another flag, which must
  // not be recorded as the value (a campaign named "--platform" in registry.json).
  const fs::path out2 = dir_ / "flag_as_value";
  EXPECT_EQ(run(out2, "--campaign --platform bizzyboat"), 2) << log();
  EXPECT_NE(log().find("another flag"), std::string::npos) << log();
  EXPECT_TRUE(tileNames(out2).empty());

  // An empty layer list is an ARGUMENT refusal (exit 2), not a store failure.
  const fs::path out3 = dir_ / "no_layers";
  EXPECT_EQ(
    run(out3, "--bathy-store \"" + store_.string() + "\" --bathy-layers \" , \""), 2) << log();
  EXPECT_NE(log().find("--bathy-layers names no layer"), std::string::npos) << log();
  EXPECT_TRUE(tileNames(out3).empty());
}

// The same dropped-argument class in the two slots the flag-value check does not
// cover: a POSITIONAL that is really a flag, and a numeric value with trailing
// garbage. Both used to succeed at exit 0 and be recorded in the store.
TEST_F(Tier2ProcessedDemTest, FlagShapedPositionalsAndTrailingGarbageAreArgumentErrors)
{
  // `sidescan_tier2_processed in.sst1 --accumulate` created a directory literally
  // named "--accumulate" *and* turned accumulate on.
  const fs::path positional_log = dir_ / "flag_positional.log";
  const std::string cmd = "cd \"" + dir_.string() + "\" && " +
    std::string(SIDESCAN_TIER2_PROCESSED_BINARY) + " \"" + tier1_.string() +
    "\" --accumulate > \"" + positional_log.string() + "\" 2>&1";
  const int rc = std::system(cmd.c_str());
  ASSERT_TRUE(rc != -1 && WIFEXITED(rc));
  const std::string captured = readFile(positional_log);
  EXPECT_EQ(WEXITSTATUS(rc), 2) << captured;
  EXPECT_NE(captured.find("which is a flag, not a path"), std::string::npos) << captured;
  EXPECT_FALSE(fs::exists(dir_ / "--accumulate"));

  // Trailing garbage on a numeric value is refused, not silently truncated.
  const fs::path out = dir_ / "trailing_garbage";
  EXPECT_EQ(run(out, "--min-dem-coverage 0.9,"), 2) << log();
  EXPECT_NE(log().find("expected a number"), std::string::npos) << log();
  EXPECT_EQ(run(out, "--level 13x"), 2) << log();
  EXPECT_NE(log().find("expected an integer"), std::string::npos) << log();
  EXPECT_TRUE(tileNames(out).empty());
}

// The coverage gate: a valid store that does not overlap the survey exits 3 and
// writes NOTHING, and --min-dem-coverage 0 is the documented opt-in.
TEST_F(Tier2ProcessedDemTest, CoverageGateAbortsBeforeAnyWrite)
{
  const fs::path gated = dir_ / "gated";
  EXPECT_EQ(run(gated, "--bathy-store \"" + far_store_.string() + "\""), 3) << log();
  const std::string gate_log = log();
  EXPECT_NE(gate_log.find("error: DEM coverage"), std::string::npos) << gate_log;
  EXPECT_NE(gate_log.find("survey bounds"), std::string::npos) << gate_log;
  EXPECT_TRUE(tileNames(gated).empty());
  EXPECT_FALSE(fs::exists(gated / "registry.json"));
  EXPECT_FALSE(fs::exists(gated / "projection.json"));

  const fs::path allowed = dir_ / "allowed";
  EXPECT_EQ(
    run(
      allowed,
      "--bathy-store \"" + far_store_.string() + "\" --min-dem-coverage 0"), 0) << log();
  EXPECT_FALSE(tileNames(allowed).empty());
  // An opt-in partial run is still a "dem" store, so the sidecar must carry the
  // coverage it actually achieved — otherwise a 0 %- and a 99 %-corrected store are
  // indistinguishable downstream.
  const std::string sidecar = readFile(allowed / "projection.json");
  // The trailing comma matters: "dem_coverage": 0 alone also matches 0.87, which
  // would prove only that the key is present and starts with a zero.
  EXPECT_NE(sidecar.find("\"dem_coverage\": 0,"), std::string::npos) << sidecar;

  // A flat run has no coverage figure to record.
  const fs::path flat = dir_ / "flat_coverage";
  ASSERT_EQ(run(flat), 0) << log();
  EXPECT_NE(
    readFile(flat / "projection.json").find("\"dem_coverage\": null,"), std::string::npos)
    << readFile(flat / "projection.json");
}

// A sidecar that is present but unreadable/truncated leaves the store's mode
// UNKNOWN. Unknown must not read as "flat": a DEM store whose sidecar was cut
// short would otherwise silently accept a flat --accumulate.
TEST_F(Tier2ProcessedDemTest, UnparseableSidecarIsNotTreatedAsFlat)
{
  const fs::path store_out = dir_ / "dem_store";
  ASSERT_EQ(run(store_out, "--bathy-store \"" + store_.string() + "\""), 0) << log();

  // Truncate the sidecar before the projection_mode line.
  {
    std::ofstream truncate(store_out / "projection.json", std::ios::trunc);
    truncate << "{\n";
  }
  const auto before = readFile(store_out / *tileNames(store_out).begin());
  EXPECT_EQ(run(store_out, "--accumulate"), 2) << log();
  EXPECT_NE(log().find("UNKNOWN"), std::string::npos) << log();
  EXPECT_EQ(readFile(store_out / *tileNames(store_out).begin()), before);
}

// A tile the startup scan listed but that cannot be read is a corrupt store, and
// the run aborts having written nothing. The handler is scoped to the DEM call
// sites, so the message names the real fault instead of blaming the DEM for any
// exception the ping loop might throw.
TEST_F(Tier2ProcessedDemTest, CorruptTileAbortsTheRunWithoutWriting)
{
  const fs::path corrupt_store = dir_ / "bathy_corrupt";
  writeBathyStore(corrupt_store, kLat, kLon);
  const fs::path layer = corrupt_store / "survey";
  ASSERT_TRUE(fs::is_directory(layer));
  for (const auto & entry : fs::directory_iterator(layer)) {
    if (entry.path().extension() == ".tif") {
      std::ofstream clobber(entry.path(), std::ios::binary | std::ios::trunc);
      clobber << "not a GeoTIFF";
    }
  }

  const fs::path out = dir_ / "corrupt_out";
  EXPECT_EQ(run(out, "--bathy-store \"" + corrupt_store.string() + "\""), 1) << log();
  const std::string fail_log = log();
  EXPECT_NE(fail_log.find("bathy DEM lookup failed"), std::string::npos) << fail_log;
  EXPECT_NE(fail_log.find("corrupt or truncated"), std::string::npos) << fail_log;
  EXPECT_TRUE(tileNames(out).empty());
  EXPECT_FALSE(fs::exists(out / "registry.json"));
  EXPECT_FALSE(fs::exists(out / "projection.json"));

  // The mirror assertion: a NON-DEM fault on a DEM run must not be reported as a
  // DEM lookup failure. The handlers are scoped to the DEM call sites precisely so
  // an output-path problem keeps its own diagnosis.
  const fs::path blocked = dir_ / "out_is_a_file";
  {
    std::ofstream occupied(blocked);
    occupied << "not a directory";
  }
  EXPECT_NE(run(blocked, "--bathy-store \"" + store_.string() + "\""), 0) << log();
  const std::string blocked_log = log();
  EXPECT_EQ(blocked_log.find("bathy DEM lookup failed"), std::string::npos) << blocked_log;
  EXPECT_EQ(blocked_log.find("corrupt or truncated"), std::string::npos) << blocked_log;
}

// Degenerate and non-converged samples are placed FLAT, so they count against
// coverage. A store that covers the survey but yields degenerate geometry over
// most of the swath must not report coverage 1.0 and sail through the gate.
TEST_F(Tier2ProcessedDemTest, DegenerateSamplesCountAgainstTheCoverageGate)
{
  const fs::path shelf_store = dir_ / "bathy_shelf";
  writeBathyStore(shelf_store, kLat, kLon, /*shelf_above_sensor=*/ true);

  const fs::path gated = dir_ / "shelf_gated";
  EXPECT_EQ(run(gated, "--bathy-store \"" + shelf_store.string() + "\""), 3) << log();
  const std::string gate_log = log();
  EXPECT_NE(gate_log.find("error: DEM coverage"), std::string::npos) << gate_log;
  EXPECT_EQ(gate_log.find("degenerate=0 "), std::string::npos) << gate_log;
  EXPECT_TRUE(tileNames(gated).empty());
  EXPECT_FALSE(fs::exists(gated / "registry.json"));

  // The operator can still opt in explicitly.
  const fs::path allowed = dir_ / "shelf_allowed";
  EXPECT_EQ(
    run(
      allowed,
      "--bathy-store \"" + shelf_store.string() + "\" --min-dem-coverage 0"), 0) << log();
  EXPECT_FALSE(tileNames(allowed).empty());
}

// The sidecar is written BEFORE the first tile, so a store can never exist without
// its mode record (a DEM store with no projection.json reads as a pre-#297 FLAT
// build). Proven by making the sidecar write fail: nothing else may be on disk.
TEST_F(Tier2ProcessedDemTest, SidecarIsWrittenBeforeAnyTile)
{
  // The technique is a read-only directory, which CAP_DAC_OVERRIDE defeats: root
  // writes into it regardless, the sidecar write succeeds, and the test fails on a
  // tool that is behaving correctly. `ci_local.sh` runs its container `--user root`
  // — i.e. under precisely the full-scope attestation ADR-0018 makes the merge gate
  // — so skip rather than fail there (#297 round-3 review). This is the only test
  // in the package that denies access by permissions.
  if (::geteuid() == 0) {
    GTEST_SKIP() << "running as root: CAP_DAC_OVERRIDE defeats the read-only output "
                 << "directory this test needs to make the sidecar write fail";
  }
  const fs::path out = dir_ / "no_sidecar_write";
  fs::create_directories(out);
  fs::permissions(
    out, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);

  const int rc = run(out, "--bathy-store \"" + store_.string() + "\"");
  const std::string fail_log = log();
  fs::permissions(out, fs::perms::owner_all, fs::perm_options::replace);

  EXPECT_EQ(rc, 1) << fail_log;
  EXPECT_NE(fail_log.find("projection.json"), std::string::npos) << fail_log;
  EXPECT_TRUE(tileNames(out).empty()) << fail_log;
  EXPECT_FALSE(fs::exists(out / "registry.json")) << fail_log;
}

// A build interrupted between its tiles and its registry leaves tiles with no
// registry.json. Both --accumulate guards used to key on registry.json alone, so
// such a store was folded into unguarded and then re-stamped with a pure mode.
TEST_F(Tier2ProcessedDemTest, StoreWithTilesButNoRegistryIsStillGuarded)
{
  // (a) the projection-mode guard must still see a DEM store's mode.
  const fs::path dem_store = dir_ / "dem_no_registry";
  ASSERT_EQ(run(dem_store, "--bathy-store \"" + store_.string() + "\""), 0) << log();
  ASSERT_FALSE(tileNames(dem_store).empty());
  ASSERT_TRUE(fs::remove(dem_store / "registry.json"));
  const auto before = readFile(dem_store / *tileNames(dem_store).begin());

  EXPECT_EQ(run(dem_store, "--accumulate"), 2) << log();
  EXPECT_NE(log().find("projection_mode 'dem'"), std::string::npos) << log();
  EXPECT_EQ(readFile(dem_store / *tileNames(dem_store).begin()), before);
  EXPECT_FALSE(fs::exists(dem_store / "registry.json"));

  // (b) with the mode matching, the source-id guard has no registry to check
  // against, so the fold is refused rather than run unverified.
  const fs::path flat_store = dir_ / "flat_no_registry";
  ASSERT_EQ(run(flat_store), 0) << log();
  ASSERT_TRUE(fs::remove(flat_store / "registry.json"));
  EXPECT_EQ(run(flat_store, "--accumulate"), 2) << log();
  EXPECT_NE(log().find("no registry.json"), std::string::npos) << log();
}

// Without --accumulate, saveTiles rewrites only the tiles this run touches — so a
// re-run into a populated directory leaves the previous build's other tiles beside
// this one's while the sidecar records a single pure mode. Refuse it — and the
// refusal is PLAIN: the tool has no store deleter (the round-3 `--overwrite` flag
// was removed rather than hardened; stores are data-of-record and clearing one is
// the operator's own explicit act). The message must name the files to remove.
TEST_F(Tier2ProcessedDemTest, NonAccumulateReRunIntoAPopulatedDirIsRefused)
{
  const fs::path out = dir_ / "populated";
  ASSERT_EQ(run(out), 0) << log();
  const auto flat_names = tileNames(out);
  ASSERT_FALSE(flat_names.empty());
  const auto before = readFile(out / *flat_names.begin());

  // A DEM re-run into it is refused, and nothing on disk changes.
  EXPECT_EQ(run(out, "--bathy-store \"" + store_.string() + "\""), 2) << log();
  const std::string refusal = log();
  EXPECT_NE(refusal.find("--accumulate"), std::string::npos) << refusal;
  // The two ways forward, and the paths the operator would delete by hand.
  EXPECT_NE(refusal.find("fresh out_dir"), std::string::npos) << refusal;
  EXPECT_NE(refusal.find("delete the previous build yourself"), std::string::npos) << refusal;
  EXPECT_NE(refusal.find((out / "registry.json").string()), std::string::npos) << refusal;
  EXPECT_NE(refusal.find((out / "projection.json").string()), std::string::npos) << refusal;
  EXPECT_EQ(readFile(out / *flat_names.begin()), before);
  EXPECT_NE(readFile(out / "projection.json").find("\"flat\""), std::string::npos);

  // There is no in-tool deleter to reach for: --overwrite is not a flag, and an
  // unknown flag must not become a way through the refusal.
  EXPECT_EQ(run(out, "--overwrite --bathy-store \"" + store_.string() + "\""), 2) << log();
  EXPECT_EQ(readFile(out / *flat_names.begin()), before);
  EXPECT_TRUE(fs::exists(out / "registry.json"));

  // The documented way through, done by hand, then leaves a fresh DEM build.
  for (const auto & name : flat_names) {
    ASSERT_TRUE(fs::remove(out / name));
  }
  ASSERT_TRUE(fs::remove(out / "registry.json"));
  ASSERT_TRUE(fs::remove(out / "projection.json"));
  EXPECT_EQ(run(out, "--bathy-store \"" + store_.string() + "\""), 0) << log();
  EXPECT_NE(readFile(out / "projection.json").find("\"dem\""), std::string::npos);

  const fs::path fresh = dir_ / "fresh_dem";
  ASSERT_EQ(run(fresh, "--bathy-store \"" + store_.string() + "\""), 0) << log();
  EXPECT_EQ(tileNames(out), tileNames(fresh));
}

// Two 'dem' runs pass the mode guard even when they used DIFFERENT bathy stores —
// a different surface places samples differently, and per-cell provenance cannot
// tell the two apart. The sidecar records the store, so say so.
TEST_F(Tier2ProcessedDemTest, AccumulateWarnsWhenTheBathyStoreChanges)
{
  const fs::path out = dir_ / "changed_store";
  ASSERT_EQ(run(out, "--bathy-store \"" + store_.string() + "\""), 0) << log();
  EXPECT_EQ(log().find("was DEM-orthorectified against"), std::string::npos) << log();

  // Same store again: no warning.
  ASSERT_EQ(
    run(out, "--accumulate --bathy-store \"" + store_.string() + "\""), 0) << log();
  EXPECT_EQ(log().find("was DEM-orthorectified against"), std::string::npos) << log();

  // A different store: warned, and the run still proceeds (an updated store is a
  // legitimate workflow — only the operator knows whether the surfaces agree).
  ASSERT_EQ(
    run(
      out, "--accumulate --min-dem-coverage 0 --bathy-store \"" +
      far_store_.string() + "\""), 0) << log();
  EXPECT_NE(log().find("was DEM-orthorectified against"), std::string::npos) << log();
}

// The datum cross-check must be REPORTED on a run the coverage gate then kills:
// a store in the wrong vertical frame is exactly what drives coverage down, so a
// check that only prints on successful runs is silent when it matters most.
TEST_F(Tier2ProcessedDemTest, DatumCrossCheckIsReportedBeforeTheCoverageGateExits)
{
  const fs::path shelf_store = dir_ / "bathy_shelf";
  writeBathyStore(shelf_store, kLat, kLon, /*shelf_above_sensor=*/ true);

  const fs::path gated = dir_ / "datum_gated";
  EXPECT_EQ(run(gated, "--bathy-store \"" + shelf_store.string() + "\""), 3) << log();
  const std::string gate_log = log();
  const auto datum_at = gate_log.find("datum cross-check (nadir altimeter");
  const auto gate_at = gate_log.find("error: DEM coverage");
  ASSERT_NE(datum_at, std::string::npos) << gate_log;
  ASSERT_NE(gate_at, std::string::npos) << gate_log;
  EXPECT_LT(datum_at, gate_at) << gate_log;
  EXPECT_TRUE(tileNames(gated).empty());
}

// --accumulate must refuse to composite DEM-corrected samples into a store built
// flat (and vice versa): per-cell provenance cannot tell the two apart.
TEST_F(Tier2ProcessedDemTest, AccumulateRefusesToMixProjectionModes)
{
  const fs::path store_out = dir_ / "store_out";
  ASSERT_EQ(run(store_out), 0) << log();
  ASSERT_TRUE(fs::exists(store_out / "projection.json"));
  EXPECT_NE(readFile(store_out / "projection.json").find("\"flat\""), std::string::npos);

  // A flat --accumulate onto a flat store is still fine.
  EXPECT_EQ(run(store_out, "--accumulate"), 0) << log();

  // A DEM --accumulate onto it is refused, and nothing is rewritten.
  const auto before = readFile(store_out / *tileNames(store_out).begin());
  EXPECT_EQ(
    run(store_out, "--accumulate --bathy-store \"" + store_.string() + "\""), 2) << log();
  const std::string refusal = log();
  EXPECT_NE(refusal.find("error: --accumulate"), std::string::npos) << refusal;
  EXPECT_NE(refusal.find("projection_mode 'flat'"), std::string::npos) << refusal;
  EXPECT_EQ(readFile(store_out / *tileNames(store_out).begin()), before);
  EXPECT_NE(readFile(store_out / "projection.json").find("\"flat\""), std::string::npos);

  // ...unless the operator explicitly accepts the mix — and then the store is
  // marked `mixed` FOREVER. Recording it as pure "dem" would launder the
  // provenance: every later --accumulate would sail through the guard.
  EXPECT_EQ(
    run(
      store_out,
      "--accumulate --allow-mixed-projection --bathy-store \"" + store_.string() + "\""),
    0) << log();
  const std::string mixed_sidecar = readFile(store_out / "projection.json");
  EXPECT_NE(mixed_sidecar.find("\"mixed\""), std::string::npos) << mixed_sidecar;
  EXPECT_EQ(mixed_sidecar.find("\"dem\""), std::string::npos) << mixed_sidecar;

  // Sticky: a plain DEM --accumulate into the now-mixed store is still refused,
  // and accepting the mix again leaves it `mixed` rather than promoting it.
  EXPECT_EQ(
    run(store_out, "--accumulate --bathy-store \"" + store_.string() + "\""), 2) << log();
  EXPECT_EQ(
    run(
      store_out,
      "--accumulate --allow-mixed-projection --bathy-store \"" + store_.string() + "\""),
    0) << log();
  EXPECT_NE(
    readFile(store_out / "projection.json").find("\"mixed\""), std::string::npos);

  // The mirror case: a flat --accumulate onto a DEM-built store is refused too.
  const fs::path dem_store = dir_ / "dem_store";
  ASSERT_EQ(run(dem_store, "--bathy-store \"" + store_.string() + "\""), 0) << log();
  EXPECT_NE(readFile(dem_store / "projection.json").find("\"dem\""), std::string::npos);
  EXPECT_EQ(run(dem_store, "--accumulate"), 2) << log();

  // A first --accumulate into a fresh directory has no store to mix with.
  const fs::path fresh = dir_ / "fresh";
  EXPECT_EQ(
    run(fresh, "--accumulate --bathy-store \"" + store_.string() + "\""), 0) << log();
}

// The sidecar must not LAUNDER the provenance the prior run recorded. Two ways it
// used to: an --accumulate re-stamped dem_coverage with only its own figure (so a
// 0.99 bag folded into a near-zero store read 0.99), and a flat run promoted to
// `mixed` wrote bathy_store: "" — erasing, permanently and silently, the record the
// different-store cross-check depends on.
TEST_F(Tier2ProcessedDemTest, AccumulateDoesNotLaunderTheRecordedProvenance)
{
  // (a) coverage. A deliberately partial run first (coverage 0 against the far
  // store), then a well-covered fold on top.
  const fs::path out = dir_ / "coverage_history";
  ASSERT_EQ(
    run(out, "--bathy-store \"" + far_store_.string() + "\" --min-dem-coverage 0"), 0) << log();
  const std::string first = readFile(out / "projection.json");
  EXPECT_NE(first.find("\"dem_coverage\": 0,"), std::string::npos) << first;
  EXPECT_NE(first.find("\"dem_coverage_history\": [0]"), std::string::npos) << first;

  ASSERT_EQ(
    run(out, "--accumulate --bathy-store \"" + store_.string() + "\""), 0) << log();
  const std::string folded = readFile(out / "projection.json");
  // Every run's figure survives, and the single-number field is the WORST of them —
  // never the rosier latest.
  EXPECT_NE(folded.find("\"dem_coverage\": 0,"), std::string::npos) << folded;
  EXPECT_NE(folded.find("\"dem_coverage_history\": [0, "), std::string::npos) << folded;
  EXPECT_EQ(folded.find("\"dem_coverage_history\": [0]"), std::string::npos) << folded;

  // (b) the bathy store record. A flat --accumulate accepted as a deliberate mix
  // must leave the DEM half's store named.
  const fs::path mixed = dir_ / "mixed_keeps_store";
  ASSERT_EQ(run(mixed, "--bathy-store \"" + store_.string() + "\""), 0) << log();
  ASSERT_NE(
    readFile(mixed / "projection.json").find(store_.string()), std::string::npos);
  ASSERT_EQ(run(mixed, "--accumulate --allow-mixed-projection"), 0) << log();
  const std::string promoted = readFile(mixed / "projection.json");
  EXPECT_NE(promoted.find("\"mixed\""), std::string::npos) << promoted;
  EXPECT_NE(promoted.find(store_.string()), std::string::npos) << promoted;
  EXPECT_EQ(promoted.find("\"bathy_store\": \"\""), std::string::npos) << promoted;

  // ...and the cross-check that record feeds still works afterwards: a DEM
  // --accumulate against a DIFFERENT store into the now-mixed store still warns.
  ASSERT_EQ(
    run(
      mixed, "--accumulate --allow-mixed-projection --min-dem-coverage 0 --bathy-store \"" +
      far_store_.string() + "\""), 0) << log();
  EXPECT_NE(log().find("was DEM-orthorectified against"), std::string::npos) << log();
}

// A sidecar value must never be readable as a KEY. The reader parses the object
// structurally, so a bathy-store path that itself contains a quoted
// "projection_mode" cannot spoof the store's mode — and a hand-repaired
// single-line sidecar still yields all of its fields, not just the first.
TEST_F(Tier2ProcessedDemTest, SidecarValuesCannotSpoofSidecarKeys)
{
  const fs::path out = dir_ / "spoofed";
  ASSERT_EQ(run(out, "--bathy-store \"" + store_.string() + "\""), 0) << log();

  // Hand-rewrite the sidecar onto ONE line, with a spoofed mode inside a value.
  {
    std::ofstream repaired(out / "projection.json", std::ios::trunc);
    repaired << "{ \"version\": 2, \"projection_mode\": \"dem\", \"bathy_store\": "
             << "\"/tmp/\\\"projection_mode\\\": \\\"flat\\\"/store\", "
             << "\"bathy_layers\": \"survey,reference\", \"dem_coverage\": null, "
             << "\"dem_coverage_history\": [null] }\n";
  }
  // The mode is still 'dem' (not the spoofed 'flat'), so a flat --accumulate is
  // refused — and the store field parsed off the same single line is reported.
  EXPECT_EQ(run(out, "--accumulate"), 2) << log();
  EXPECT_NE(log().find("projection_mode 'dem'"), std::string::npos) << log();

  // The single line's bathy_store also reached the different-store cross-check.
  EXPECT_EQ(
    run(out, "--accumulate --bathy-store \"" + store_.string() + "\""), 0) << log();
  EXPECT_NE(log().find("was DEM-orthorectified against"), std::string::npos) << log();
}
