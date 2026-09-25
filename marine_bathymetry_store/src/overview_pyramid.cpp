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

// [uma-ADR-0010 D9 / uma-ADR-0011 / uma-ADR-0013] Batch overview-pyramid builder
// for a depth store layer — production path (level discovery, per-level fold,
// level loop, argument parsing). The `build_depth_overviews` CLI is a thin
// main() over these.
//
// Folds the layer's native tiles into coarser parent tiles, level by level, into
// the layer's `overviews/` sidecar (flat dir, `<level>_<row>_<col>.tif` — level
// rides in the filename, same as the native layer). Overviews are DERIVED +
// REGENERABLE: each run rebuilds the sidecar (idempotent; safe to re-run after
// every ingest).
//
// Fold policy (depth, ADR-0010 D9): SHALLOWEST-PRESERVING. Among the valid
// contributors that land in one coarse cell, the one with the MAXIMUM ellipsoidal
// height (band 0 — most positive / least negative, i.e. shoalest and most
// hazardous to navigation) is selected and its whole {depth, σ} pair is carried
// through. Never a mean: a coarse corridor query must plan around the rock, not
// average it away, and the σ must stay coherent with the depth it describes, so
// the pair travels together (the fold operates on whole cells, all bands at once
// — see overview_builder.hpp CellFoldPolicy). VALID means band 0 (depth) is not
// NaN — NaN is the store's per-band no-data sentinel (see bathymetry_tile.hpp).
//
// Layer scope (uma-ADR-0010 D9, as amended by #331): draft, processed AND
// reference get a generated pyramid; chart is exempt — its ENC scale ladder is a
// cartographer-curated, shoal-biased native pyramid. No upsampling: only parent
// levels are built (the min_level < discovered-finest check plus the
// fold-toward-apex loop enforce it).
//
// NATIVE-WINS, and the display inverts it. On disk a derived tile is written
// only where no native tile occupies that (level, index): nothing compiled is
// overwritten, and no merge policy is needed or implied. On screen a consumer
// composites every level <= its selection with finer over coarser, so a derived
// level 7 folded from harbour-band data draws OVER a native level 6 compiled at
// a coarser scale wherever both exist. Both halves are the intended behaviour
// (uma-ADR-0013 D3's ECDIS-consistent corollary) and must be read together.
//
// Safety (uma-ADR-0013 D8): nothing in the query path reads this sidecar or its
// manifest. shallowestReliable() keeps scanning native tiles to the finest
// available level for a region, so declining to merge finer data into a compiled
// coarse tile carries no operational risk.
//
// Memory: tiles are grouped by parent FROM GRID INDICES and loaded <=4 children
// at a time (a whole-level in-memory fold would grow without bound with store
// size; this path peaks around one parent tile's contributor buckets — see
// overview_builder.hpp — and that peak does not grow with store size). Each level
// is built from the level below it — the native tiles there plus the derived
// tiles just written there — not by re-reading the finest data.

#include "marine_bathymetry_store/overview_pyramid.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include "marine_autonomy/gggs.h"
#include "marine_autonomy/gggs/index_math.h"
#include "marine_bathymetry_store/bathy_cell.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"
#include "marine_bathymetry_store/tile_io.hpp"
#include "marine_tiled_raster_store/coverage_manifest.hpp"
#include "marine_tiled_raster_store/overview_builder.hpp"
#include "marine_tiled_raster_store/tile_io.hpp"

namespace marine_bathymetry_store
{

// Depth-band indices and the fold policy live in `detail` (declared in the header)
// so the fold's determinism is directly unit-testable — feeding the same
// contributors in two orders must yield one {depth, σ}. Everything else is
// file-local (anonymous namespace below).
namespace detail
{

using Cell = marine_tiled_raster_store::CellValues<double>;

// The on-disk depth value tile is 2-band Float64: depth (band 0, ellipsoidal
// height in metres) + uncertainty (band 1, σ in metres). NaN is the per-band
// no-data sentinel (bathymetry_tile.hpp).
constexpr std::size_t kDepthBand = 0;   // shoalest-selection + no-data band
constexpr std::size_t kSigmaBand = 1;   // uncertainty (σ): the equal-depth tie-break key

// Total order for shallowest-preserving selection: true when candidate @p c should
// displace the current best @p best. The order is lexicographic and therefore
// TOTAL:
//   1. depth (band 0) DESCENDING — the larger ellipsoidal height (shoaler, most
//      hazardous to navigation) wins; both depths are non-NaN here (validCell gate);
//   2. on an EXACT depth tie, a finite σ beats a NaN σ (prefer the pair that
//      carries a real uncertainty); then
//   3. still tied, the SMALLER σ wins — the more reliable of two equally-shoal
//      pairs (ADR-0010 D9 "shoalest-reliable").
// When depth AND σ are both equal (or both σ NaN), the two cells carry identical
// {depth, σ}, so keeping the first is result-identical. The fold is thus a function
// of the contributor SET, not its order — buckets fill in filesystem-iteration
// order (overview_builder.hpp), which is not guaranteed, so an order-sensitive
// tie-break would make the sidecar non-idempotent.
inline bool shoalerThenMoreReliable(const Cell & c, const Cell & best)
{
  if (c[kDepthBand] != best[kDepthBand]) {
    return c[kDepthBand] > best[kDepthBand];
  }
  const bool c_sigma_nan = std::isnan(c[kSigmaBand]);
  const bool best_sigma_nan = std::isnan(best[kSigmaBand]);
  if (c_sigma_nan != best_sigma_nan) {
    return best_sigma_nan;   // finite σ preferred over NaN σ
  }
  if (c_sigma_nan) {
    return false;   // both σ NaN: identical {depth, σ}, keep the first
  }
  return c[kSigmaBand] < best[kSigmaBand];   // both finite: smaller σ wins
}

// Shallowest-preserving fold (ADR-0010 D9). Among the valid contributors, select
// the shoalest — MAXIMUM ellipsoidal height (band 0) — and return its WHOLE
// {depth, σ} pair, so the coarse cell's uncertainty stays coherent with the depth
// it describes. Never a mean: a coarse corridor query must not average a rock away,
// and a mixed depth/σ pair would describe a cell that never existed. Equal-depth
// ties break deterministically by σ (see shoalerThenMoreReliable), so the result
// depends only on the contributor SET, never enumeration order — the sidecar stays
// idempotent. Called only with >=1 contributor (the engine gates on validCell
// first), each carrying a non-NaN depth.
Cell depthShallowestFold(const std::vector<Cell> & contributors)
{
  const Cell * best = &contributors.front();
  for (const Cell & c : contributors) {
    if (shoalerThenMoreReliable(c, *best)) {
      best = &c;
    }
  }
  return *best;   // the whole pair, depth AND its paired uncertainty
}


// --- Multi-band (rev-3) fold -------------------------------------------------
//
// docs/world_store_design.md §7, spine decision 2 (BAG VR RESAMPLED_GRID
// precedent): a folded level stores MIN, MEAN, COUNT and σ per parent cell
// rather than one folded value, so a view can choose the band — navigation
// reads MIN, others read MEAN, and COUNT is the lineage that says how much
// evidence the MEAN rests on. The σ band's RULE is deliberately open (§7); see
// SigmaFold in the header.

std::vector<double> promoteNativeDepthCell(const std::vector<double> & native)
{
  // A native cell is a one-cell summary of itself: min = mean = its depth, one
  // contributing native cell, its own σ. Promoting on READ is what lets one
  // fold serve the native step and every derived step above it.
  return {native[kDepthBand], native[kDepthBand], 1.0, native[kSigmaBand]};
}

namespace
{

// Weight of one contributor's lineage. A COUNT that is not a finite number >= 1
// means the contributing tile is malformed; substituting 1 keeps the parent's
// COUNT consistent with the fact that a MIN exists at all, which is what a
// consumer reads to decide whether the MEAN is evidence or noise. Silently
// propagating a NaN or a 0 would corrupt that reading for every level above.
double contributorCount(const std::vector<double> & cell)
{
  const double n = cell[kMultiCountBand];
  return (std::isfinite(n) && n >= 1.0) ? n : 1.0;
}

// Representative value of one contributor for the MEAN accumulation. A NaN MEAN
// on a cell that passed the valid gate (its MIN is finite) is likewise a
// malformed upstream tile; its MIN is the one value known to be real.
double contributorMean(const std::vector<double> & cell)
{
  const double m = cell[kMultiMeanBand];
  return std::isnan(m) ? cell[kMultiMinBand] : m;
}

// The σ band, per rule. Returns NaN for kUndecided and whenever no contributor
// carries a σ at all — "no uncertainty information" must read as nodata, never
// as zero uncertainty, which is the most dangerous number this band could hold.
double foldSigma(
  const std::vector<std::vector<double>> & contributors, SigmaFold rule,
  double pooled_mean, double total_count)
{
  const double nan = std::numeric_limits<double>::quiet_NaN();
  if (rule == SigmaFold::kUndecided) {
    return nan;
  }
  bool any_sigma = false;
  for (const std::vector<double> & c : contributors) {
    if (!std::isnan(c[kMultiSigmaBand])) {
      any_sigma = true;
      break;
    }
  }
  if (!any_sigma) {
    return nan;
  }
  switch (rule) {
    case SigmaFold::kMaxChild: {
        double best = nan;
        for (const std::vector<double> & c : contributors) {
          const double s = c[kMultiSigmaBand];
          if (!std::isnan(s) && (std::isnan(best) || s > best)) {best = s;}
        }
        return best;
      }
    case SigmaFold::kMeanChild: {
        double weighted = 0.0, weight = 0.0;
        for (const std::vector<double> & c : contributors) {
          const double s = c[kMultiSigmaBand];
          if (std::isnan(s)) {continue;}
          const double n = contributorCount(c);
          weighted += s * n;
          weight += n;
        }
        return weight > 0.0 ? weighted / weight : nan;
      }
    case SigmaFold::kPooled: {
        // Within-child variance plus the spread of the child means, weighted by
        // lineage: σ² = Σ n_i (σ_i² + (μ_i − μ)²) / Σ n_i. A contributor with no
        // σ still contributes its mean's distance from the pooled mean (its
        // within-variance counts as 0) — dropping it entirely would understate
        // the spread the band exists to report.
        double accum = 0.0;
        for (const std::vector<double> & c : contributors) {
          const double n = contributorCount(c);
          const double s = c[kMultiSigmaBand];
          const double within = std::isnan(s) ? 0.0 : s * s;
          const double d = contributorMean(c) - pooled_mean;
          accum += n * (within + d * d);
        }
        return total_count > 0.0 ? std::sqrt(accum / total_count) : nan;
      }
    case SigmaFold::kUndecided:
    default:
      return nan;
  }
}

}  // namespace

std::vector<double> depthMultiBandFold(
  const std::vector<std::vector<double>> & contributors, SigmaFold rule)
{
  // MIN: shoalest wins — the maximum ellipsoidal height, the same selection
  // depthShallowestFold makes on band 0, so the two pyramids agree bit for bit
  // on the navigation band. Only a number travels here, not a {depth, σ} pair,
  // so there is no tie to break: equal depths are the same value.
  double min_band = contributors.front()[kMultiMinBand];
  double weighted_mean = 0.0;
  double total_count = 0.0;
  for (const std::vector<double> & c : contributors) {
    if (c[kMultiMinBand] > min_band) {min_band = c[kMultiMinBand];}
    const double n = contributorCount(c);
    weighted_mean += contributorMean(c) * n;
    total_count += n;
  }
  const double mean_band =
    total_count > 0.0 ? weighted_mean / total_count :
    std::numeric_limits<double>::quiet_NaN();
  std::vector<double> out(kMultiBandCount);
  out[kMultiMinBand] = min_band;
  out[kMultiMeanBand] = mean_band;
  out[kMultiCountBand] = total_count;
  out[kMultiSigmaBand] = foldSigma(contributors, rule, mean_band, total_count);
  return out;
}

// Saturated conservative per-tile geometric error (uma-ADR-0013 D1/D2).
//
// D2 makes error nesting a PRODUCER obligation — a tile's error must be at least
// the maximum of its descendants' — and requires a producer that cannot compute
// a meaningful error to "record a conservative upper bound rather than omit the
// field". An "unknown" sentinel would be that same omission wearing a hat, so
// this records the bound: max(level GSD, max child ε).
//
// It is computable from this producer alone. GGGS's nominal cell size halves
// with each level, so a child whose own ε is unrecorded contributes at most its
// level's GSD — strictly smaller than the parent's — and saturation holds even
// across an edge where no native ε exists. Where nothing records a finer error
// the value degenerates to exactly the level's ground sample distance, which is
// the level-as-resolution behaviour consumers already fall back to, so its
// arrival changes nothing for them and its later refinement (once the other
// three D2 writers record real errors) is purely additive.
//
// nominal_cell_size is the equatorial cell size; away from the equator the true
// ground sample is smaller in longitude, so the equatorial figure is itself an
// upper bound — which is the direction a conservative error must err.
double saturatedGeometricError(
  int level, int child_level,
  const std::vector<std::optional<double>> & child_errors)
{
  const double child_gsd = gggs::levels[child_level].nominal_cell_size;
  double error = gggs::levels[level].nominal_cell_size;
  for (const std::optional<double> & child_error : child_errors) {
    const double value = child_error.value_or(child_gsd);
    if (value > error) {
      error = value;
    }
  }
  return error;
}

}  // namespace detail

namespace
{

namespace fs = std::filesystem;

/// Atomically exchange two directory entries.
///
/// Wraps renameat2(RENAME_EXCHANGE) through syscall() rather than the glibc
/// wrapper: the wrapper only appeared in glibc 2.28, and RENAME_EXCHANGE lives in
/// <linux/fs.h> on some toolchains and <stdio.h> (under _GNU_SOURCE) on others.
/// Going through SYS_renameat2 directly keeps this working across both without a
/// feature-test dance.
///
/// @return 0 on success, -1 with errno set otherwise. errno is ENOSYS on a kernel
///   or platform without the call, and EINVAL/EOPNOTSUPP on a filesystem that
///   cannot do it — the caller treats all three as "fall back", not "fail".
int exchangeDirEntries(const char * a, const char * b)
{
#if defined(__linux__) && defined(SYS_renameat2)
#ifndef RENAME_EXCHANGE
#define RENAME_EXCHANGE (1 << 1)
#endif
  return static_cast<int>(
    ::syscall(SYS_renameat2, AT_FDCWD, a, AT_FDCWD, b, RENAME_EXCHANGE));
#else
  (void)a;
  (void)b;
  errno = ENOSYS;
  return -1;
#endif
}

using marine_tiled_raster_store::TiledRasterTile;
using Cell = marine_tiled_raster_store::CellValues<double>;

// Full band count of the on-disk depth tile (depth + uncertainty).
constexpr std::size_t kBands = BathymetryTile::value_band_count;   // 2

// Contributor gate. NaN in the DEPTH band (band 0) is the no-data sentinel
// (bathymetry_tile.hpp initialises empty cells to {NaN, NaN}). A cell with a real
// depth participates even if its uncertainty happens to be NaN — the depth is the
// navigable quantity and the pair is carried as-is; gating on band 0 alone
// matches how the store itself distinguishes surveyed from unsurveyed cells.
bool validCell(const Cell & cell) {return !std::isnan(cell[detail::kDepthBand]);}


// Promote a native 2-band {depth, σ} tile to the multi-band schema, cell by
// cell (detail::promoteNativeDepthCell is the per-cell rule). Kept here rather
// than in the fold because it is an I/O-shaped concern: the fold must see one
// band count, and this is where the two schemas meet.
TiledRasterTile<double> promoteNativeTile(
  const TiledRasterTile<double> & native, std::size_t bands)
{
  const double nan = std::numeric_limits<double>::quiet_NaN();
  TiledRasterTile<double> out(native.index(), std::vector<double>(bands, nan));
  std::vector<double> cell(kBands);
  for (uint16_t row = 0; row < TiledRasterTile<double>::edge; ++row) {
    for (uint16_t col = 0; col < TiledRasterTile<double>::edge; ++col) {
      for (std::size_t b = 0; b < kBands; ++b) {
        cell[b] = native.get(row, col, b);
      }
      const std::vector<double> promoted = detail::promoteNativeDepthCell(cell);
      for (std::size_t b = 0; b < bands && b < promoted.size(); ++b) {
        out.set(row, col, b, promoted[b]);
      }
    }
  }
  return out;
}

// The multi-band sidecar's schema record, written beside its tiles by BOTH
// multi-band writers (the batch builder into its staging directory, the
// per-parent writer into overviews/ before its first tile).
constexpr const char * kOverviewSchemaFilename = "overview_schema.json";
constexpr const char * kOverviewSchemaName = "depth-overview-multiband/1";

// Read @p dir's schema record. nullopt when there is none; a record that is
// present but unreadable, or is not this schema's, throws naming the file —
// a guard that treated it as absent would fall back to probing a tile and
// could let the wrong writer in on a record it could not read.
std::optional<nlohmann::json> readOverviewSchema(const fs::path & dir)
{
  const fs::path path = dir / kOverviewSchemaFilename;
  std::error_code ec;
  const bool present = fs::exists(path, ec);
  if (ec) {
    throw std::runtime_error(
      "cannot check for " + path.string() + ": " + ec.message() +
      "; refusing to write overview tiles beside it");
  }
  if (!present) {
    return std::nullopt;
  }
  std::ifstream in(path);
  nlohmann::json doc;
  try {
    if (!in) {
      throw std::runtime_error("cannot open it");
    }
    doc = nlohmann::json::parse(in);
  } catch (const std::exception & e) {
    throw std::runtime_error(
      "cannot read the overview schema record " + path.string() + " (" +
      e.what() + "); refusing to write overview tiles beside it — repair or "
      "remove it (the writer that owns the directory rewrites it)");
  }
  const auto bands = doc.is_object() ? doc.find("bands") : doc.end();
  if (!doc.is_object() || !doc.contains("schema") ||
    doc["schema"] != kOverviewSchemaName || bands == doc.end() ||
    !bands->is_array() || bands->empty())
  {
    throw std::runtime_error(
      path.string() + " is not a " + std::string(kOverviewSchemaName) +
      " record; refusing to write overview tiles beside it");
  }
  return doc;
}

// Band count of the sidecar in @p dir, or nullopt when it holds neither a
// schema record nor any tile. The schema record wins when present; otherwise
// ONE tile is probed (a sidecar with mixed band counts is not a state either
// writer can produce, and the cross-schema guard only needs to know which
// schema is in residence).
//
// FAILS CLOSED: a probe tile that cannot be read throws, naming it. Returning
// "unknown" there silently skipped the guard — exactly the state in which a
// mis-pointed writer could not be told apart from the right one.
std::optional<int> sidecarBandCount(const fs::path & dir)
{
  if (!fs::is_directory(dir)) {
    return std::nullopt;
  }
  if (const std::optional<nlohmann::json> schema = readOverviewSchema(dir)) {
    return static_cast<int>((*schema)["bands"].size());
  }
  std::size_t skipped = 0;
  const std::vector<gggs::GridIndex> grids =
    marine_tiled_raster_store::gridsInDir(dir.string(), std::nullopt, skipped);
  if (grids.empty()) {
    return std::nullopt;
  }
  const fs::path probe = dir / marine_tiled_raster_store::tileFilename(grids.front());
  try {
    return marine_tiled_raster_store::tileRasterCount(probe.string());
  } catch (const std::exception & e) {
    throw std::runtime_error(
      "cannot tell which band schema " + dir.string() + " holds: it has no " +
      kOverviewSchemaFilename + " and its tile " + probe.string() +
      " cannot be read (" + e.what() + "); refusing to write into it — "
      "repair or remove that tile");
  }
}

// Refuse to replace a sidecar written under the OTHER tile schema.
//
// Consumers read the sidecar BY BAND INDEX, so a single-band pyramid replaced
// in place by a multi-band one (or the reverse) is the one mistake nothing
// downstream can detect: every read succeeds and every number means something
// else. The two writers target different trees by design, so reaching this is
// always a mis-pointed path — say so, and touch nothing.
void refuseCrossSchemaSidecar(
  const fs::path & overviews, std::size_t writing_bands, const char * schema_name)
{
  const std::optional<int> existing = sidecarBandCount(overviews);
  if (existing.has_value() &&
    *existing != static_cast<int>(writing_bands))
  {
    throw std::runtime_error(
      "refusing to replace " + overviews.string() + ": it holds " +
      std::to_string(*existing) + "-band tiles and this is the " + schema_name +
      " writer (" + std::to_string(writing_bands) + " bands). Consumers read "
      "these tiles by band index, so swapping the schema under them would be "
      "silently wrong — check the layer path");
  }
}

// fsync one path. A file's failure throws — its content is what the rename
// is about to publish. A directory's is best effort: some filesystems refuse
// fsync on a directory, and the rename is still atomic there; only its
// durability is the filesystem's call.
void fsyncPath(const fs::path & path, bool directory)
{
  const int fd = ::open(path.c_str(), directory ? (O_RDONLY | O_DIRECTORY) : O_RDONLY);
  if (fd < 0) {
    if (directory) {
      return;
    }
    throw std::runtime_error(
      "cannot open " + path.string() + " to sync it: " + std::strerror(errno));
  }
  const int rc = ::fsync(fd);
  const int err = errno;
  ::close(fd);
  if (rc != 0 && !directory) {
    throw std::runtime_error(
      "cannot sync " + path.string() + ": " + std::strerror(err));
  }
}

// A temporary beside @p path that no other writer uses: pid, a per-process
// counter, and 64 random bits. A fixed `<name>.tmp` is shared by every writer
// of `<name>`, so two concurrent runs could publish each other's half-written
// file; the counter covers two writes of one name from one process. The pid
// alone is NOT unique across writers sharing storage — two containers (or two
// hosts on one network filesystem) each have their own pid namespace, and
// GDAL's Create truncates whatever it finds — so a random component makes a
// collision as unlikely as Python's atomic_io temporaries make it.
fs::path privateTemporary(const fs::path & path, const std::string & suffix)
{
  static std::atomic<unsigned long> counter{0};   // NOLINT(runtime/int)
  static thread_local std::mt19937_64 random_bits{[] {
      std::random_device device;
      return (static_cast<std::uint64_t>(device()) << 32) ^ device();
    }()};
  std::ostringstream token;
  token << std::hex << std::setw(16) << std::setfill('0') << random_bits();
  return path.parent_path() /
         ("." + path.filename().string() + "." +
         std::to_string(static_cast<std::int64_t>(::getpid())) + "." +
         std::to_string(counter.fetch_add(1)) + "." + token.str() + suffix);
}

// Publish @p path whole and durably: write a private temporary, sync it,
// rename it over @p path, sync the directory. rename(2) alone is atomic but
// not durable — after a crash a renamed file whose data never reached disk
// can be present and empty.
void publishText(const fs::path & path, const std::string & text)
{
  const fs::path tmp = privateTemporary(path, ".tmp");
  try {
    {
      std::ofstream out(tmp);
      out << text;
      out.flush();
      if (!out) {
        throw std::runtime_error("cannot write " + tmp.string());
      }
    }
    fsyncPath(tmp, false);
    fs::rename(tmp, path);
  } catch (...) {
    std::error_code ec;
    fs::remove(tmp, ec);
    throw;
  }
  fsyncPath(path.parent_path(), true);
}

// The rev-3 layer's writer lock: `<layer>/overviews.lock`, flock(2)ed.
//
// The batch 4-band builder replaces overviews/ WHOLESALE (stage, then swap);
// the per-parent writer and the prune write into it tile by tile. Interleaved,
// the swap retires every tile a per-parent run wrote meanwhile, or a
// per-parent run writes into a directory about to be retired. So the batch
// builder holds the lock EXCLUSIVELY for its whole run, and each per-parent
// write or prune holds it SHARED — many of those may run at once, which is
// the point of the per-parent mode, but never beside a batch build. Both
// refuse rather than wait: a DAG job that blocked on a batch build would hold
// a worker for the batch's whole duration and then fold stale inputs.
//
// flock is released by the kernel when the process exits, so a crashed run
// leaves no stale lock (unlike the batch builder's overviews.tmp/ staging
// directory, which is checked separately as the crash-debris signal).
class LayerWriterLock
{
public:
  LayerWriterLock(const fs::path & layer_dir, bool exclusive)
  : path_(layer_dir / "overviews.lock")
  {
    fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd_ < 0) {
      throw std::runtime_error(
        "cannot open the layer writer lock " + path_.string() + ": " +
        std::strerror(errno));
    }
    if (::flock(fd_, (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB) != 0) {
      const int err = errno;
      ::close(fd_);
      fd_ = -1;
      if (err == EWOULDBLOCK) {
        throw std::runtime_error(
          std::string("refusing to write ") + layer_dir.string() + ": " +
                (exclusive ?
                "per-parent overview writes are in progress over this layer" :
                "a batch overview build is in progress over this layer") +
          " (" + path_.string() + " is held); retry when it finishes");
      }
      throw std::runtime_error(
        "cannot lock " + path_.string() + ": " + std::strerror(err));
    }
  }
  ~LayerWriterLock()
  {
    if (fd_ >= 0) {
      ::close(fd_);   // releases the flock
    }
  }
  LayerWriterLock(const LayerWriterLock &) = delete;
  LayerWriterLock & operator=(const LayerWriterLock &) = delete;

private:
  fs::path path_;
  int fd_ = -1;
};

// A batch build's staging directory left behind by a crash: the per-parent
// writer must not fold into overviews/ while it is there, because the next
// batch run's recovery (or an operator clearing it) is about to decide what
// overviews/ holds.
void refuseBatchDebris(const fs::path & layer_dir)
{
  const fs::path staging = layer_dir / "overviews.tmp";
  if (fs::exists(staging)) {
    throw std::runtime_error(
      "refusing to write " + layer_dir.string() + ": " + staging.string() +
      " exists — a batch overview build is running, or crashed and left it "
      "behind (remove it to retry)");
  }
}

// The per-tile record's filename beside a tile: `<level>_<row>_<col>.json`.
fs::path tileRecordPath(const fs::path & tile_path)
{
  fs::path record = tile_path;
  record.replace_extension(".json");
  return record;
}

// Remove one derived tile and everything that describes it: its per-tile
// record and its content-fingerprint sidecar (`<tile>.fp`, written by
// marine_world_store's refresh pre-step). The tile goes first, so an
// interruption leaves at worst a record with no tile — which every reader
// already ignores — never a tile with no record claiming it is fine.
// Returns whether the tile existed.
bool removeDerivedTile(const fs::path & overviews, const gggs::GridIndex & grid)
{
  const fs::path tile = overviews / marine_tiled_raster_store::tileFilename(grid);
  std::error_code ec;
  const bool existed = fs::remove(tile, ec);
  if (ec) {
    throw std::runtime_error("cannot remove stale " + tile.string() + ": " + ec.message());
  }
  for (const fs::path & companion :
    {tileRecordPath(tile), fs::path(tile).concat(".fp")})
  {
    fs::remove(companion, ec);
    if (ec) {
      throw std::runtime_error(
        "cannot remove stale " + companion.string() + ": " + ec.message());
    }
  }
  return existed;
}

// The band schema and the σ rule, recorded beside the tiles.
//
// A reader must never have to INFER which of the four bands carries meaning.
// While §7's σ rule is open the fourth band is nodata, and `sigma_fold` says so
// by name — so the later decision produces a different recorded value and
// therefore a different fingerprint, which is the whole point of writing it
// down rather than leaving the band blank.
void writeOverviewSchema(const fs::path & dir, SigmaFold rule)
{
  nlohmann::json doc{
    {"schema", kOverviewSchemaName},
    {"bands", nlohmann::json::array({"min", "mean", "count", "sigma"})},
    {"sigma_fold", sigmaFoldName(rule)},
    {"sigma_band_written", rule != SigmaFold::kUndecided}};
  publishText(dir / kOverviewSchemaFilename, doc.dump(2) + "\n");
}

// The per-parent writer's half of the schema record: refuse a sidecar whose
// record names a DIFFERENT σ rule (its tiles would be mixed-rule, and the
// record would then describe only some of them — change the rule by rebuilding
// the layer's overviews), and otherwise make sure the record exists, so both
// multi-band writers leave the same record and the cross-schema guard reads it
// rather than probing a tile. Concurrent per-parent writers publish identical
// bytes through a private temporary and a rename, so the race is harmless.
void refuseOtherSigmaRule(const fs::path & overviews, SigmaFold rule)
{
  const std::optional<nlohmann::json> schema = readOverviewSchema(overviews);
  if (!schema.has_value()) {
    return;
  }
  const auto recorded = schema->find("sigma_fold");
  const std::string name =
    (recorded != schema->end() && recorded->is_string()) ?
    recorded->get<std::string>() : std::string("(none)");
  if (name != sigmaFoldName(rule)) {
    throw std::runtime_error(
      "refusing to write into " + overviews.string() + ": its " +
      kOverviewSchemaFilename + " records sigma_fold " + name +
      " and this writer folds under " + sigmaFoldName(rule) +
      "; one sidecar holds one rule — rebuild the layer's overviews to change it");
  }
}

void ensureOverviewSchema(const fs::path & overviews, SigmaFold rule)
{
  if (!readOverviewSchema(overviews).has_value()) {
    writeOverviewSchema(overviews, rule);
  }
}

// One derived tile's own record: its geometric error and the schema it was
// written under. Per-TILE rather than a shared manifest because the per-parent
// writer runs many at once over one directory under a Snakemake DAG, and a
// shared coverage.json would be a write race with no lock that would not also
// serialise the DAG back into the batch build it replaces. The records are
// assembled into coverage.json once, after the DAG (mws_assemble_coverage).
//
// `children` names the contributors (relative to the layer: `<name>` native,
// `overviews/<name>` derived), which is the lineage marine_world_store needs
// to build the tile's STAC Item — its observation interval and its inputs are
// the union of its children's.
void writeTileMeta(
  const fs::path & tile_path, double geometric_error_m, SigmaFold rule,
  const std::vector<std::string> & children)
{
  nlohmann::json doc{
    {"schema", "depth-overview-tile/1"},
    {"geometric_error_m", geometric_error_m},
    {"bands", nlohmann::json::array({"min", "mean", "count", "sigma"})},
    {"sigma_fold", sigmaFoldName(rule)},
    {"sigma_band_written", rule != SigmaFold::kUndecided},
    {"children_used", children.size()},
    {"children", children}};
  publishText(tileRecordPath(tile_path), doc.dump(2) + "\n");
}

// One level's tile counts: how many child tiles were read, how many parents were
// written, and how many parents were left to a native tile. The IN count is the
// diagnostic one for a partial store — an operator seeing "40 in" for a
// 1000-tile layer knows immediately what is wrong.
struct LevelCounts
{
  std::size_t in = 0;
  std::size_t out = 0;
  std::size_t suppressed_by_native = 0;
};

// Where one contributor tile lives. Children at a given level come from two
// places at once in a mixed-level layer: the native tiles in the layer dir, and
// the derived tiles this run just wrote into staging. The two sets are disjoint
// by construction (a derived tile is never written where a native one exists),
// so a flat list with each tile's directory is enough — no precedence rule is
// needed here, because there is never a collision to resolve.
struct SourceTile
{
  gggs::GridIndex grid;
  const fs::path * dir;
  /// True for a tile in the layer's own directory (a compiled native tile),
  /// false for one this run derived into staging. Under the multi-band schema
  /// the two differ in BAND COUNT — a native tile is the 2-band {depth, σ} pair
  /// and is promoted on read — so the distinction has to travel with the tile
  /// rather than being re-derived from its path by the reader.
  bool native = true;
};

// Build one coarser level. @p children are the contributor tiles at
// @p child_level; parents are written to @p out_dir. A parent whose
// `(level, index)` is already occupied by a NATIVE tile is skipped and counted —
// native data always wins on disk. Each written parent is added to
// @p derived with its saturated geometric error (uma-ADR-0013 D1/D2), read back
// from @p derived for children that were themselves derived.
LevelCounts buildLevel(
  const std::vector<SourceTile> & children, const fs::path & out_dir,
  uint8_t child_level,
  const marine_tiled_raster_store::CoverageManifest & native,
  marine_tiled_raster_store::CoverageManifest & derived,
  std::size_t bands,
  const marine_tiled_raster_store::CellFoldPolicy<double> & fold)
{
  LevelCounts counts;
  std::map<gggs::GridIndex, std::vector<SourceTile>> by_parent;
  for (const SourceTile & child : children) {
    ++counts.in;
    const gggs::GridIndex parent_grid = gggs::parent(child.grid);
    if (parent_grid.valid()) {
      by_parent[parent_grid].push_back(child);
    }
  }

  const double nan = std::numeric_limits<double>::quiet_NaN();
  const std::vector<std::optional<double>> nodata(
    bands, std::optional<double>(nan));
  for (const auto & group : by_parent) {
    // NATIVE-WINS. Compiled data is never overwritten and never merged into, so
    // the "what should a fold of harbour data into an approach-band tile mean?"
    // question is not answered here — it is removed.
    //
    // Suppression is WHOLE-TILE, and under a mixed-level producer (uma#369's
    // depth-adaptive `processed`) that has a KNOWN consequence for the coarse
    // display tier. `reference`'s mixed levels come from disjoint S-102
    // footprints, but depth bands within one contiguous survey share parent
    // indices, and the depth ladder GUARANTEES it: a 217 m level-12 tile deep on
    // one half and shallow on the other yields native 12 beside native 13/14
    // under one parent. Where that happens the shallow band's fold is dropped at
    // that level and every level coarser.
    //
    // This is NOT a writer obligation — "never emit two native levels over the
    // same ground" is unsatisfiable for a depth-adaptive writer, and stating it
    // as one (as an earlier draft of the ADR-0010 D9 amendment did) would be
    // telling cube_bathymetry#143 not to be depth-adaptive across a tile
    // boundary. Safety is unaffected: navigation reads the region-aware NATIVE
    // query and never an LOD level (uma-ADR-0013 D8). What is affected is the
    // coarse display tier — a design input for the coarse-tier work, recorded in
    // the ADR-0010 D9 amendment. The pyramid itself needs no change.
    if (native.contains(group.first)) {
      ++counts.suppressed_by_native;
      continue;
    }
    std::vector<TiledRasterTile<double>> child_tiles;
    child_tiles.reserve(group.second.size());
    std::vector<const TiledRasterTile<double> *> child_ptrs;
    std::vector<std::optional<double>> child_errors;
    child_errors.reserve(group.second.size());
    for (const SourceTile & child : group.second) {
      const fs::path path =
        *child.dir / marine_tiled_raster_store::tileFilename(child.grid);
      // A native child is always the 2-band {depth, σ} tile the compile wrote.
      // Under the multi-band schema it is PROMOTED to {depth, depth, 1, σ} here,
      // so buildParentTile sees one band count and one fold policy across the
      // whole pyramid instead of a special case for its bottom step.
      marine_tiled_raster_store::TiledRasterTile<double> loaded =
        marine_tiled_raster_store::loadTile<double>(
        path.string(), gggs::Level(child_level),
        child.native ? kBands : bands);
      child_tiles.push_back(
        (child.native && bands != kBands) ?
        promoteNativeTile(loaded, bands) : std::move(loaded));
      child_ptrs.push_back(&child_tiles.back());
      // nullopt for a native child: no producer records an error for those yet,
      // and saturatedGeometricError substitutes the child level's GSD.
      child_errors.push_back(derived.geometricError(child.grid));
    }
    const TiledRasterTile<double> parent_tile =
      marine_tiled_raster_store::buildParentTile<double>(
      group.first, child_ptrs,
      std::vector<double>(bands, nan),
      validCell, fold);
    marine_tiled_raster_store::saveTile<double>(
      parent_tile,
      (out_dir / marine_tiled_raster_store::tileFilename(group.first)).string(),
      nodata);
    derived.add(
      group.first,
      detail::saturatedGeometricError(
        group.first.level(), child_level, child_errors));
    ++counts.out;
  }
  return counts;
}

// Strict integer parse: rejects an empty, non-numeric or trailing-garbage value
// that std::atoi would silently read as 0 (`--min-level abc` must be a usage
// error, not a silent "build to the apex").
bool parseInt(const char * text, int & out)
{
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  try {
    std::size_t used = 0;
    const int value = std::stoi(text, &used);
    if (text[used] != '\0') {
      return false;
    }
    out = value;
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

}  // namespace

DepthArgStatus parseDepthOverviewArgs(
  int argc, char ** argv, DepthOverviewOptions & out)
{
  out = DepthOverviewOptions{};
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--min-level" && i + 1 < argc) {
      if (!parseInt(argv[++i], out.min_level)) {
        return DepthArgStatus::kError;
      }
    } else if (arg == "--dry-run") {
      out.dry_run = true;
    } else if (arg == "--help" || arg == "-h") {
      return DepthArgStatus::kHelp;
    } else if (out.layer_dir.empty() && !arg.empty() && arg[0] != '-') {
      out.layer_dir = arg;
    } else {
      return DepthArgStatus::kError;
    }
  }
  // The no-upsample guard (min_level strictly below the layer's finest native
  // level) can no longer be checked here: the finest level is DISCOVERED from the
  // layer, so the check moved into buildDepthOverviewPyramid, where it throws
  // std::invalid_argument. Parsing validates only what argv alone can decide.
  if (out.layer_dir.empty() || out.min_level < 0 || out.min_level > 20) {
    return DepthArgStatus::kError;
  }
  return DepthArgStatus::kOk;
}

namespace
{

// The pyramid build, shared by the single-band (`draft/processed/reference`)
// and multi-band (rev-3) writers.
//
// Level discovery, the native-wins rule, the guards, the staging run lock and
// the atomic swap are IDENTICAL between the two — only the output tile's band
// count and fold policy differ, plus the schema record the multi-band writer
// leaves beside its tiles. Sharing one body is what keeps that true: two copies
// of a swap this intricate would drift, and the half that drifted would be the
// one with no golden-fixture pin on it.
//
// @param bands Output tile band count (2 = single-band, 4 = multi-band).
// @param sigma_rule Set for the multi-band schema — also selects writing
//   `overview_schema.json` into the staging directory. Unset = single-band.
// @param fn_name The public entry point's name, for exception messages.
DepthOverviewBuildResult buildPyramidCore(
  const std::string & layer_dir_s, int min_level, bool dry_run,
  std::size_t bands,
  const marine_tiled_raster_store::CellFoldPolicy<double> & fold,
  std::optional<SigmaFold> sigma_rule, const char * fn_name,
  std::ostream * progress)
{
  if (min_level < 0 || min_level > 20) {
    throw std::invalid_argument(
      std::string(fn_name) + ": min_level out of bounds");
  }
  const fs::path layer_dir(layer_dir_s);
  if (!fs::is_directory(layer_dir)) {
    throw std::runtime_error("not a directory: " + layer_dir_s);
  }
  // Cross-schema guard, BEFORE anything is scanned or staged: a mis-pointed
  // path must cost nothing and destroy nothing.
  refuseCrossSchemaSidecar(
    layer_dir / "overviews", bands,
    sigma_rule.has_value() ? "multi-band" : "single-band");

  // Level discovery (uma-ADR-0013 D3). One all-level scan yields the layer's
  // native coverage, which is both the guard below and the fold's input: a
  // mixed-level layer cannot be pyramided without knowing which regions hold data
  // at which level.
  //
  // The manifest is held IN MEMORY only and never written to <layer>/. This
  // builder does not own the native tiles, so a native coverage.json it wrote
  // would go stale on the next s102_import with nothing able to notice — the
  // scan fallback fires on a manifest's ABSENCE, not on its staleness. Only the
  // derived manifest, which this builder does own, is persisted (into
  // overviews.tmp/, so it rides uma-ADR-0011's rename-aside).
  std::size_t guard_skipped = 0;
  const marine_tiled_raster_store::CoverageManifest native =
    marine_tiled_raster_store::scanCoverage(layer_dir.string(), guard_skipped);

  // Guard: never wipe the sidecar for an empty or mis-pointed layer. Require at
  // least one usable native tile at SOME level before touching overviews/ — a
  // path typo must not destroy a previously-good build. This generalises the old
  // "no fine tiles at the declared level" refusal; there is no declared level to
  // mistype any more.
  if (native.empty()) {
    // Distinguish "nothing there" (a path typo) from "tiles are there but none
    // could be reconstructed" — the same message for both sends the operator
    // hunting a typo that does not exist.
    throw std::runtime_error(
      "no usable native tiles under " + layer_dir_s +
            (guard_skipped > 0 ?
            " (" + std::to_string(guard_skipped) + " tile name(s) were present "
            "but failed grid reconstruction — see the warnings above; not a path typo)" :
            " (no tile of the form <level>_<row>_<col>.tif — check the path)") +
      "; refusing to replace overviews/");
  }

  const std::vector<uint8_t> native_levels = native.levels();
  const int finest = static_cast<int>(native_levels.back());
  // No-upsample invariant, now against the DISCOVERED finest level: the coarsest
  // level built must be strictly coarser than the layer's finest native data, so
  // the build only ever produces coarser tiles.
  if (min_level >= finest) {
    throw std::invalid_argument(
      std::string(fn_name) + ": min_level " + std::to_string(min_level) +
      " is not below the layer's finest native level " + std::to_string(finest) +
      " (that would ask for an upsample)");
  }

  // The scan is filename-only. Another store's layer whose tiles happen to be
  // named <level>_<row>_<col>.tif would pass it and be rebuilt with the depth
  // shallowest-preserving policy (and its band semantics). Probe ONE TILE PER
  // DISCOVERED LEVEL — a mixed-level layer can hold a wrong-shape band at a level
  // the finest-level probe never opens.
  for (const uint8_t level : native_levels) {
    const std::vector<gggs::GridIndex> at_level = native.gridsAt(level);
    const std::string probe_path =
      (layer_dir /
      marine_tiled_raster_store::tileFilename(at_level.front())).string();
    const int probe_bands = marine_tiled_raster_store::tileRasterCount(probe_path);
    if (probe_bands != static_cast<int>(kBands)) {
      throw std::runtime_error(
        "not a depth layer: " + probe_path + " has " +
        std::to_string(probe_bands) + " band(s), expected " +
        std::to_string(kBands) + " (depth, uncertainty); refusing to replace "
        "overviews/ with a depth-policy pyramid");
    }
  }

  DepthOverviewBuildResult result;
  result.tiles_skipped = guard_skipped;
  for (const uint8_t level : native_levels) {
    result.native_levels.push_back(static_cast<int>(level));
  }

  // --dry-run stops here: the guards above are exactly the checks that catch a
  // mistyped path or wrong layer, and nothing below this point is reached without
  // writing. Report the discovered coverage — the report that replaces
  // --fine-level's mis-pointed-path guard — and touch nothing.
  if (dry_run) {
    if (progress != nullptr) {
      *progress << "dry run: " << native.size() << " usable native tile(s) under " <<
        layer_dir_s << " (" << guard_skipped << " unreconstructable)\n";
      for (const uint8_t level : native_levels) {
        *progress << "  native level " << static_cast<unsigned>(level) << ": " <<
          native.countAt(level) << " tile(s)\n";
      }
      *progress << "  would build levels " << (finest - 1) << "..." <<
        min_level << " and replace " <<
        (layer_dir / "overviews").string() << "\n";
    }
    return result;
  }

  // Refuse the swap unless every native tile name reconstructed: a pyramid
  // missing a tile's coverage must never displace a previously-complete sidecar.
  //
  // This is decided from the guard scan alone, so it is settled BEFORE the run
  // lock is claimed and before a single tile is folded. Doing it after the fold
  // (as this originally did) burned a full staging copy and hours of I/O on a
  // layer that was already known unbuildable, and held the per-layer lock for the
  // whole futile run.
  if (result.tiles_skipped > 0) {
    return result;
  }

  const fs::path overviews = layer_dir / "overviews";
  const fs::path staging = layer_dir / "overviews.tmp";
  const fs::path retired = layer_dir / "overviews.old";

  // Crash-safe regeneration: build into a staging sibling and swap it over the
  // live sidecar only after every level succeeds, so an interrupted or throwing
  // run leaves the previous overviews/ intact rather than a truncated one a
  // consumer would read as complete.
  //
  // The staging directory doubles as the run lock: create_directory fails when it
  // already exists, so a second concurrent run over the same layer refuses instead
  // of interleaving its tiles with the first run's. A crashed run leaves the
  // directory behind as debris — the message says how to clear it.
  std::error_code create_ec;
  if (!fs::create_directory(staging, create_ec)) {
    throw std::runtime_error(
      "cannot claim staging directory " + staging.string() +
            (create_ec ?
            ": " + create_ec.message() :
            " (it already exists — another build is running over this layer, or a "
            "crashed run left it behind; remove it to retry)"));
  }

  // The derived coverage this run produces (uma-ADR-0013 D3), accumulated as the
  // levels are built. It is both the record written into the sidecar and the
  // lookup for a derived child's geometric error on the next level down.
  marine_tiled_raster_store::CoverageManifest derived;
  try {
    // Fold from just under the finest native level toward the apex. Contributors
    // at each child level are the NATIVE tiles there plus the DERIVED tiles this
    // run just wrote there — disjoint by construction, so no precedence rule is
    // needed between them.
    for (int level = finest - 1; level >= min_level; --level) {
      const uint8_t child_level = static_cast<uint8_t>(level + 1);
      std::vector<SourceTile> children;
      for (const gggs::GridIndex & grid : native.gridsAt(child_level)) {
        children.push_back(SourceTile{grid, &layer_dir, true});
      }
      for (const gggs::GridIndex & grid : derived.gridsAt(child_level)) {
        children.push_back(SourceTile{grid, &staging, false});
      }

      const LevelCounts counts =
        buildLevel(
        children, staging, child_level, native, derived, bands, fold);
      const std::size_t native_here = native.countAt(static_cast<uint8_t>(level));
      if (progress != nullptr) {
        // child_level is uint8_t: without the cast it streams as a character.
        *progress << "level " << static_cast<unsigned>(child_level) << " -> " <<
          level << ": " <<
          counts.in << " tile(s) in, " << counts.out << " overview tile(s) out, " <<
          counts.suppressed_by_native << " left to native, " << native_here <<
          " native tile(s) already at level " << level << "\n";
      }
      result.tiles_suppressed_by_native += counts.suppressed_by_native;
      // Invariant: a level can never end up with no coverage at all. `children`
      // is non-empty at every iteration (the first reads the finest native level,
      // which the native.empty() guard proved non-empty; each later one reads a
      // level that itself just produced a derived tile or holds a native one), and
      // gggs::parent stays valid down to level 0, so by_parent is non-empty. Each
      // parent group is then either written (counts.out++) or suppressed, and
      // suppression requires a native tile AT THIS level (native_here >= 1).
      // Hence counts.out == 0 implies native_here >= 1.
      //
      // This used to be a runtime refusal with its own result flag and CLI exit
      // code. It cannot fire — see uma#331's review — so shipping it as a live
      // safety guard misrepresented what the builder actually protects against.
      // A broken tile chain surfaces through tiles_skipped instead.
      assert(counts.out > 0 || native_here > 0);
      (void)native_here;   // only read by the assert in NDEBUG builds
      if (counts.out > 0) {
        result.tiles_written += counts.out;
        result.derived_by_level[level] = counts.out;
        result.coarsest_level = level;
      }
    }

    // The derived manifest is written INTO STAGING, before the swap, so it rides
    // the rename-aside and is crash-consistent with the sidecar it describes
    // (uma-ADR-0011 §2). Reaching here means the build was not refused — the only
    // refusal, tiles_skipped, returned before staging was ever created.
    marine_tiled_raster_store::saveCoverageManifest(
      derived,
      (staging / marine_tiled_raster_store::coverageManifestFilename()).string(),
      "derived");
    // The multi-band schema record rides the same staging directory, so it is
    // swapped in atomically with the tiles it describes — a sidecar that says
    // which band is which must never be newer or older than those bands.
    if (sigma_rule.has_value()) {
      writeOverviewSchema(staging, *sigma_rule);
    }
  } catch (...) {
    // Best-effort: cleanup must not throw here, or it would replace the original
    // exception with its own.
    std::error_code ec;
    fs::remove_all(staging, ec);   // never leave a partial staging dir behind
    throw;
  }

  // Swap staging over the live sidecar.
  //
  // PREFERRED PATH — renameat2(RENAME_EXCHANGE): atomically exchanges the two
  // directory entries, so <layer>/overviews/ resolves to a complete sidecar at
  // EVERY instant, and a concurrent reader either sees the old pyramid or the new
  // one, never a missing directory. This matters because consumers treat an
  // absent overviews/ as "this layer has no overviews" rather than retrying:
  // CAMP's GggsTileLayer skips the directory outright when it does not exist, so
  // a layer opened during a non-atomic swap would render with no overview tiles
  // at all until the operator reloaded it.
  //
  // FALLBACK — rename-aside, for filesystems without RENAME_EXCHANGE (it needs
  // Linux >= 3.15 and per-filesystem support; NFS and some overlay/FUSE mounts
  // return EINVAL/ENOSYS/EOPNOTSUPP). overviews/ -> overviews.old/, staging ->
  // overviews/, then drop overviews.old/. That path is crash-SAFE but NOT atomic:
  // the previous sidecar's contents are never destroyed before the new one is in
  // place, but the PATH overviews/ is briefly absent between the two renames.
  const bool had_previous = fs::exists(overviews);
  bool retired_moved = false;
  bool exchanged = false;
  if (had_previous) {
    // Both entries must exist for an exchange; staging always does here.
    if (exchangeDirEntries(staging.c_str(), overviews.c_str()) == 0) {
      // staging now holds the PREVIOUS sidecar; retire it under the usual name so
      // the cleanup below drops it.
      exchanged = true;
      std::error_code ec;
      fs::remove_all(retired, ec);
      fs::rename(staging, retired, ec);
      if (ec) {
        // The swap itself succeeded — the live sidecar is correct. Only the
        // leftover previous sidecar could not be moved aside; say where it is.
        std::cerr << "warning: swap succeeded but could not retire the previous "
          "sidecar from " << staging.string() << ": " << ec.message() <<
          "; remove it by hand before the next run" << std::endl;
      }
    } else {
      const int saved = errno;
      // Distinguish "this filesystem cannot exchange directory entries" (fall
      // back to rename-aside) from a real failure such as EACCES or EIO, which
      // the fallback would only hit again.
      const bool unsupported = saved == EINVAL || saved == ENOSYS ||
        saved == EOPNOTSUPP || saved == EPERM;
      if (!unsupported) {
        std::error_code ec;
        fs::remove_all(staging, ec);   // clear the run lock before reporting
        throw fs::filesystem_error(
          "atomic sidecar swap failed", staging, overviews,
          std::error_code(saved, std::generic_category()));
      }
    }
  }

  if (!exchanged) {
    try {
      // Retire the previous sidecar (overviews/ -> overviews.old/) and swap the
      // new one in under one guard: a throw from the retire step must clear the
      // staging lock too, or overviews.tmp/ blocks the next run.
      if (had_previous) {
        fs::remove_all(retired);
        fs::rename(overviews, retired);
        retired_moved = true;
      }
      fs::rename(staging, overviews);
    } catch (...) {
      // Clear the staging debris (best-effort, non-throwing — a throwing cleanup
      // would mask the original failure and skip the restore below), then
      // restore. Restore only if the retire rename actually completed —
      // otherwise overviews/ was never moved and is still in place.
      std::error_code ec;
      fs::remove_all(staging, ec);
      if (retired_moved) {
        // Non-throwing, unlike the rest of this block: a throw here would replace
        // the original swap diagnostic with a restore error, and the operator
        // would never learn WHY the swap failed. If the restore does fail, the
        // previous sidecar is sitting at overviews.old/ and nothing else would
        // say so.
        std::error_code restore_ec;
        fs::rename(retired, overviews, restore_ec);
        if (restore_ec) {
          std::cerr << "warning: could not restore the previous sidecar: " <<
            restore_ec.message() << "; it is intact at " << retired.string() <<
            " — rename it back to " << overviews.string() << " by hand" <<
            std::endl;
        }
      }
      throw;
    }
  }
  // Best-effort: the swap already succeeded — a throwing cleanup here would report
  // the build as failed with the new sidecar live, prompting a needless re-run.
  // Warn so the leftover overviews.old/ is still visible.
  std::error_code ec;
  fs::remove_all(retired, ec);
  if (ec) {
    std::cerr << "warning: could not remove retired sidecar " <<
      retired.string() << ": " << ec.message() << std::endl;
  }
  result.sidecar_replaced = true;
  return result;
}

}  // namespace

DepthOverviewBuildResult buildDepthOverviewPyramid(
  const DepthOverviewOptions & opts, std::ostream * progress)
{
  // The same exclusive writer lock the multi-band batch builder takes: this
  // builder swaps overviews/ wholesale too, so a per-parent write or prune
  // pointed at the same layer (which the cross-schema guard would refuse, but
  // only once it looks) must not interleave with the swap. A dry run writes
  // nothing and takes no lock.
  std::optional<LayerWriterLock> lock;
  if (!opts.dry_run && fs::is_directory(opts.layer_dir)) {
    lock.emplace(fs::path(opts.layer_dir), true);
  }
  return buildPyramidCore(
    opts.layer_dir, opts.min_level, opts.dry_run, kBands,
    detail::depthShallowestFold, std::nullopt, "buildDepthOverviewPyramid",
    progress);
}

std::string sigmaFoldName(SigmaFold rule)
{
  switch (rule) {
    case SigmaFold::kPooled: return "pooled";
    case SigmaFold::kMaxChild: return "max_child";
    case SigmaFold::kMeanChild: return "mean_child";
    case SigmaFold::kUndecided: return "undecided";
  }
  // Unreachable for a valid enumerator; a cast-in value is recorded as unknown
  // rather than silently spelled "undecided", which is a claim about §7.
  return "unknown";
}

DepthOverviewBuildResult buildMultiBandDepthOverviewPyramid(
  const MultiBandOverviewOptions & opts, std::ostream * progress)
{
  const SigmaFold rule = opts.sigma_fold;
  // A dry run writes nothing, so it needs no writer lock (and must not create
  // the lock file in a layer it only inspects).
  std::optional<LayerWriterLock> lock;
  if (!opts.dry_run && fs::is_directory(opts.layer_dir)) {
    lock.emplace(fs::path(opts.layer_dir), true);
  }
  return buildPyramidCore(
    opts.layer_dir, opts.min_level, opts.dry_run, detail::kMultiBandCount,
    [rule](const std::vector<std::vector<double>> & contributors) {
      return detail::depthMultiBandFold(contributors, rule);
    },
    rule, "buildMultiBandDepthOverviewPyramid", progress);
}


namespace
{

// A derived tile's recorded geometric error, from its own per-tile sidecar.
// nullopt when there is no sidecar or it is unreadable — the same answer the
// batch builder gives for a native child, and saturatedGeometricError
// substitutes the child level's GSD, which is a conservative upper bound.
std::optional<double> tileMetaGeometricError(const fs::path & tile_path)
{
  fs::path meta = tile_path;
  meta.replace_extension(".json");
  std::ifstream in(meta);
  if (!in) {
    return std::nullopt;
  }
  try {
    const nlohmann::json doc = nlohmann::json::parse(in);
    const auto field = doc.find("geometric_error_m");
    if (field != doc.end() && field->is_number()) {
      return field->get<double>();
    }
  } catch (const std::exception &) {
    // Advisory metadata (uma-ADR-0013 D8): unreadable is no worse than absent.
  }
  return std::nullopt;
}

}  // namespace

std::vector<gggs::GridIndex> listMultiBandOverviewParents(
  const std::string & layer_dir_s, int parent_level)
{
  std::vector<gggs::GridIndex> parents;
  for (const MultiBandOverviewParent & entry :
    listMultiBandOverviewParentInputs(layer_dir_s, parent_level))
  {
    parents.push_back(entry.parent);
  }
  return parents;
}

std::vector<MultiBandOverviewParent> listMultiBandOverviewParentInputs(
  const std::string & layer_dir_s, int parent_level)
{
  const fs::path layer_dir(layer_dir_s);
  if (!fs::is_directory(layer_dir)) {
    throw std::runtime_error("not a directory: " + layer_dir_s);
  }
  if (parent_level < 0 ||
    static_cast<std::size_t>(parent_level) + 1 >= gggs::levels.size())
  {
    throw std::invalid_argument(
      "listMultiBandOverviewParents: level " + std::to_string(parent_level) +
      " has no child level to fold from");
  }
  const uint8_t child_level = static_cast<uint8_t>(parent_level + 1);
  const fs::path overviews = layer_dir / "overviews";

  // std::set, not a vector: GridIndex orders by level/row/column, so the
  // deduplication and the GGGS ordering the caller is promised fall out
  // together. Four children name one parent, and a native and a derived level
  // can both feed one — a DAG scheduled twice over the same tile would race
  // with itself over the destination.
  std::set<gggs::GridIndex> parents;
  std::size_t skipped = 0;
  for (const fs::path & dir : {layer_dir, overviews}) {
    for (const gggs::GridIndex & child :
      marine_tiled_raster_store::gridsInDir(dir.string(), child_level, skipped))
    {
      const gggs::GridIndex parent = gggs::parent(child);
      // Native-wins: scheduling a parent the compile already covers would only
      // produce a suppressed no-op, once per invocation.
      if (parent.valid() &&
        !fs::exists(layer_dir / marine_tiled_raster_store::tileFilename(parent)))
      {
        parents.insert(parent);
      }
    }
  }
  std::vector<MultiBandOverviewParent> out;
  out.reserve(parents.size());
  for (const gggs::GridIndex & parent : parents) {
    MultiBandOverviewParent entry{parent, {}};
    for (const gggs::GridIndex & child : gggs::children(parent)) {
      const std::string name = marine_tiled_raster_store::tileFilename(child);
      const bool has_native = fs::exists(layer_dir / name);
      const bool has_derived = fs::exists(overviews / name);
      if (has_native && has_derived) {
        throw std::runtime_error(
          "tile " + name + " exists both natively in " + layer_dir_s +
          " and as a derived overview in " + overviews.string() +
          "; native and derived coverage must be disjoint — refusing to guess");
      }
      if (has_native) {
        entry.children.push_back(name);
      } else if (has_derived) {
        entry.children.push_back("overviews/" + name);
      }
    }
    out.push_back(std::move(entry));
  }
  return out;
}

namespace
{

// The shared body of pruneMultiBandOverviewLevel (@p everything false: only the
// tiles that describe nothing) and removeMultiBandOverviewLevel (true: every
// derived tile at the level).
std::vector<gggs::GridIndex> removeDerivedTilesAtLevel(
  const std::string & layer_dir_s, int level, bool everything,
  const char * caller)
{
  const fs::path layer_dir(layer_dir_s);
  if (!fs::is_directory(layer_dir)) {
    throw std::runtime_error("not a directory: " + layer_dir_s);
  }
  if (level < 0 || static_cast<std::size_t>(level) >= gggs::levels.size()) {
    throw std::invalid_argument(
      std::string(caller) + ": " + std::to_string(level) +
      " is not a GGGS level");
  }
  const LayerWriterLock lock(layer_dir, false);
  refuseBatchDebris(layer_dir);
  const fs::path overviews = layer_dir / "overviews";
  // Before anything is removed: a prune pointed at a single-band sidecar would
  // otherwise delete its tiles by the multi-band rules.
  refuseCrossSchemaSidecar(overviews, detail::kMultiBandCount, "multi-band");
  const bool has_child_level =
    static_cast<std::size_t>(level) + 1 < gggs::levels.size();
  std::vector<gggs::GridIndex> removed;
  std::size_t skipped = 0;
  for (const gggs::GridIndex & grid :
    marine_tiled_raster_store::gridsInDir(
      overviews.string(), static_cast<uint8_t>(level), skipped))
  {
    const bool native_here = !everything &&
      fs::exists(layer_dir / marine_tiled_raster_store::tileFilename(grid));
    bool any_child = false;
    if (!everything && !native_here && has_child_level) {
      for (const gggs::GridIndex & child : gggs::children(grid)) {
        const std::string name = marine_tiled_raster_store::tileFilename(child);
        if (fs::exists(layer_dir / name) || fs::exists(overviews / name)) {
          any_child = true;
          break;
        }
      }
    }
    if ((native_here || !any_child) && removeDerivedTile(overviews, grid)) {
      removed.push_back(grid);
    }
  }
  std::sort(removed.begin(), removed.end());
  return removed;
}

}  // namespace

std::vector<gggs::GridIndex> pruneMultiBandOverviewLevel(
  const std::string & layer_dir_s, int level)
{
  return removeDerivedTilesAtLevel(
    layer_dir_s, level, false, "pruneMultiBandOverviewLevel");
}

std::vector<gggs::GridIndex> removeMultiBandOverviewLevel(
  const std::string & layer_dir_s, int level)
{
  return removeDerivedTilesAtLevel(
    layer_dir_s, level, true, "removeMultiBandOverviewLevel");
}

MultiBandParentResult buildMultiBandDepthOverviewParent(
  const std::string & layer_dir_s, int level, uint32_t row, uint32_t col,
  SigmaFold rule)
{
  const fs::path layer_dir(layer_dir_s);
  if (!fs::is_directory(layer_dir)) {
    throw std::runtime_error("not a directory: " + layer_dir_s);
  }
  if (level < 0 || static_cast<std::size_t>(level) + 1 >= gggs::levels.size()) {
    throw std::invalid_argument(
      "buildMultiBandDepthOverviewParent: level " + std::to_string(level) +
      " has no child level to fold from");
  }
  const LayerWriterLock lock(layer_dir, false);
  refuseBatchDebris(layer_dir);
  const fs::path overviews = layer_dir / "overviews";
  // Cross-schema guard FIRST — before the native-wins and no-children paths
  // remove a derived tile, and before any child is loaded under the 4-band
  // assumption. A single-band sidecar reached here is a mis-pointed path, and
  // it must leave this call exactly as it arrived.
  refuseCrossSchemaSidecar(overviews, detail::kMultiBandCount, "multi-band");
  refuseOtherSigmaRule(overviews, rule);
  // gridFromTileName round-trips its answer through tileFilename and compares
  // it with this string, so the label must be the FILENAME, extension included
  // — a bare "<level>_<row>_<col>" never matches and every index reads as
  // "names no grid".
  const std::string label = std::to_string(level) + "_" + std::to_string(row) +
    "_" + std::to_string(col) + ".tif";
  const gggs::GridIndex parent = marine_tiled_raster_store::gridFromTileName(
    static_cast<uint8_t>(level), row, col, label);
  if (!parent.valid()) {
    throw std::invalid_argument(
      "buildMultiBandDepthOverviewParent: " + label +
      " does not name a grid at level " + std::to_string(level));
  }

  MultiBandParentResult result;
  result.geometric_error_m = std::numeric_limits<double>::quiet_NaN();

  // NATIVE-WINS, same rule as the batch builder: compiled data is never
  // overwritten and never merged into. Checked FIRST, so a Snakemake rule over
  // a native-covered parent costs one stat() rather than four tile loads.
  if (fs::exists(layer_dir / marine_tiled_raster_store::tileFilename(parent))) {
    result.suppressed_by_native = true;
    // A derived tile an earlier run left here now shadows nothing but the
    // truth: remove it, or the next coarser fold finds this index both
    // natively and derived and refuses the layer forever.
    result.removed_stale = removeDerivedTile(overviews, parent);
    return result;
  }

  const uint8_t child_level = static_cast<uint8_t>(level + 1);
  std::vector<marine_tiled_raster_store::TiledRasterTile<double>> child_tiles;
  std::vector<const marine_tiled_raster_store::TiledRasterTile<double> *> child_ptrs;
  std::vector<std::optional<double>> child_errors;
  std::vector<std::string> child_names;
  for (const gggs::GridIndex & child : gggs::children(parent)) {
    const std::string name = marine_tiled_raster_store::tileFilename(child);
    const fs::path native_path = layer_dir / name;
    const fs::path derived_path = overviews / name;
    const bool has_native = fs::exists(native_path);
    const bool has_derived = fs::exists(derived_path);
    if (has_native && has_derived) {
      // Disjoint by construction in both writers, so this is a corrupted layer,
      // not a precedence question. Resolving it silently would make the pyramid
      // depend on which rule happened to run last.
      throw std::runtime_error(
        "tile " + name + " exists both natively in " + layer_dir_s +
        " and as a derived overview in " + overviews.string() +
        "; native and derived coverage must be disjoint — refusing to guess");
    }
    if (!has_native && !has_derived) {
      continue;
    }
    const fs::path path = has_native ? native_path : derived_path;
    marine_tiled_raster_store::TiledRasterTile<double> loaded =
      marine_tiled_raster_store::loadTile<double>(
      path.string(), gggs::Level(child_level),
      has_native ? BathymetryTile::value_band_count : detail::kMultiBandCount);
    child_tiles.push_back(
      has_native ?
      promoteNativeTile(loaded, detail::kMultiBandCount) : std::move(loaded));
    child_errors.push_back(
      has_native ? std::nullopt : tileMetaGeometricError(derived_path));
    child_names.push_back(has_native ? name : "overviews/" + name);
  }
  if (child_tiles.empty()) {
    // Nothing to fold. Not an error: a per-parent DAG legitimately enumerates
    // parents whose children have not been built (or do not exist) yet, and a
    // throw here would turn a sparse region into a failed run. A derived tile
    // left here by an earlier run describes children that are gone; remove it
    // rather than keep advertising that coverage.
    result.removed_stale = removeDerivedTile(overviews, parent);
    return result;
  }
  child_ptrs.reserve(child_tiles.size());
  for (const auto & tile : child_tiles) {
    child_ptrs.push_back(&tile);
  }
  result.children_used = child_tiles.size();

  const double nan = std::numeric_limits<double>::quiet_NaN();
  const marine_tiled_raster_store::TiledRasterTile<double> parent_tile =
    marine_tiled_raster_store::buildParentTile<double>(
    parent, child_ptrs,
    std::vector<double>(detail::kMultiBandCount, nan), validCell,
    [rule](const std::vector<std::vector<double>> & contributors) {
      return detail::depthMultiBandFold(contributors, rule);
    });

  std::error_code ec;
  fs::create_directories(overviews, ec);
  if (ec && !fs::is_directory(overviews)) {
    throw std::runtime_error(
      "cannot create " + overviews.string() + ": " + ec.message());
  }
  // The schema record first, so no tile this writer publishes ever sits in a
  // directory that does not say which band is which.
  ensureOverviewSchema(overviews, rule);

  // Tile-level atomicity: write beside the destination, then rename over it.
  // rename(2) within one directory is atomic, so a reader sees the previous
  // tile or the new one and never a half-written raster. No overviews.tmp/
  // staging and no run lock — one tile has no partial-pyramid hazard, and a
  // per-parent DAG runs many of these at once over this directory.
  const fs::path final_path =
    overviews / marine_tiled_raster_store::tileFilename(parent);
  // `.tif` last: the private temporary must still read as a GeoTIFF to GDAL.
  const fs::path tmp_path = privateTemporary(final_path, ".tmp.tif");
  try {
    marine_tiled_raster_store::saveTile<double>(
      parent_tile, tmp_path.string(),
      std::vector<std::optional<double>>(
        detail::kMultiBandCount, std::optional<double>(nan)));
    fsyncPath(tmp_path, false);
    fs::rename(tmp_path, final_path);
  } catch (...) {
    std::error_code cleanup_ec;
    fs::remove(tmp_path, cleanup_ec);
    throw;
  }
  fsyncPath(overviews, true);

  result.geometric_error_m = detail::saturatedGeometricError(
    level, static_cast<int>(child_level), child_errors);
  result.written = true;
  writeTileMeta(final_path, result.geometric_error_m, rule, child_names);
  return result;
}

}  // namespace marine_bathymetry_store
