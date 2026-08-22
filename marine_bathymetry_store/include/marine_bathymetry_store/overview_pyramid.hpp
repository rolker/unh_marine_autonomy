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

#ifndef MARINE_BATHYMETRY_STORE__OVERVIEW_PYRAMID_HPP_
#define MARINE_BATHYMETRY_STORE__OVERVIEW_PYRAMID_HPP_

#include <cstddef>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <vector>

/// @file
/// @brief Testable production path for the depth overview-pyramid builder
///        (`uma-ADR-0010` D9 / `uma-ADR-0011`, on the #188 fold engine, with
///        `uma-ADR-0013` D1/D2/D3 metadata).
///
/// The `build_depth_overviews` CLI is a thin `main()` over the two entry points
/// here: `parseDepthOverviewArgs` (argv -> options, no side effects) and
/// `buildDepthOverviewPyramid` (the disk fold). Extracting them makes the level
/// discovery, per-level fold, level loop and argument parsing unit- and
/// integration-testable without spawning a process (the generic cross-tile
/// engine lives in `marine_tiled_raster_store::overview_builder.hpp`; only the
/// depth **fold policy** — shallowest-preserving, `uma-ADR-0010` D9 — is here).
///
/// **Layer scope (`uma-ADR-0010` D9, as amended by #331): `draft`, `processed`
/// AND `reference`.** `chart` is still exempt — its ENC scale ladder is a
/// cartographer-curated, shoal-biased native pyramid. `reference` is no longer
/// "as imported": `s102_import` maps each dataset's native resolution through
/// `gggs::Level::fromCellSize()`, so one import populates several levels over
/// disjoint ground, and below each region's coarsest native level the layer
/// renders nothing at all.
///
/// **Native-wins is a STORAGE rule; the display inverts it.** On disk, a derived
/// tile is written only at a `(level, index)` that holds no native tile —
/// nothing compiled is ever overwritten or merged into. On screen, a consumer
/// composites every level ≤ its selection with finer over coarser, so a derived
/// level 7 folded from 3.6 m harbour data draws *over* a native level 6
/// compiled at 14.5 m wherever both exist. That inversion is intended and
/// ECDIS-consistent (`uma-ADR-0013` D3's corollary), and the two halves must be
/// read together: reading the storage rule alone predicts the wrong picture.
///
/// **No upsampling**: only parent levels are built. `min_level` must be strictly
/// below the layer's finest DISCOVERED native level, and the loop folds strictly
/// toward the apex.

namespace marine_bathymetry_store
{

/// @brief Parsed options for one depth overview-pyramid build.
struct DepthOverviewOptions
{
  std::string layer_dir;   ///< the layer directory (`draft/`, `processed/`, `reference/`)
  int min_level = 0;       ///< coarsest level to build (0 = apex)
  /// Run the guards and report what would be built — including the discovered
  /// per-level native coverage — writing nothing. The check before pointing a
  /// destructive rebuild at a mistyped path.
  bool dry_run = false;
};

/// @brief Outcome of `parseDepthOverviewArgs` — distinguishes a clean parse, an
///        explicit help request, and a usage error so the caller can pick the
///        exit code (0 for help, non-zero for error).
enum class DepthArgStatus
{
  kOk,       ///< @p out is populated and valid
  kHelp,     ///< `--help`/`-h` requested; print usage, exit 0
  kError,    ///< malformed arguments; print usage, exit non-zero
};

/// @brief Parse @p argv into @p out. Pure (no I/O); validates the level range.
///
/// Returns `kError` — never crashes — on an unknown flag, a flag missing its
/// value, a non-numeric or trailing-garbage level value, an out-of-range level,
/// or an empty-string argument. Returns `kHelp` on `--help`/`-h`.
///
/// @note There is no `--fine-level`: the builder DISCOVERS the layer's native
///   levels, and asserting a single one is meaningless for a mixed-level layer.
///   The mis-pointed-path guard it used to carry is now the generalised "no
///   usable native tiles under &lt;dir&gt;" refusal, and `--dry-run` reports the
///   discovered levels. (`marine_sidescan_mosaic`'s `build_sidescan_overviews`
///   keeps its own `--fine-level`: that store is genuinely single-level, so the
///   flag still asserts something true there. The divergence is deliberate.)
DepthArgStatus parseDepthOverviewArgs(
  int argc, char ** argv, DepthOverviewOptions & out);

/// @brief Aggregate result of a pyramid build (for logging and exit codes).
struct DepthOverviewBuildResult
{
  std::size_t tiles_written = 0;   ///< total overview tiles written
  /// Input tiles skipped for a grid-reconstruction mismatch. **Any skip refuses
  /// the swap** — the pyramid is missing that tile's coverage, so it must not
  /// displace a previously-complete sidecar. The caller should exit non-zero.
  ///
  /// Under level discovery this surface is wider than it was under a single
  /// declared level: an unreadable tile name at ANY level now refuses the swap.
  /// That is the point — its coverage would be missing from every level built
  /// beneath it.
  std::size_t tiles_skipped = 0;
  /// Coarsest level PRODUCED. Stays -1 when the fold wrote no derived tile at
  /// any level — legal for a mixed-level layer whose coarser levels are already
  /// native all the way down to @c min_level.
  int coarsest_level = -1;
  /// Derived tiles written per level (level -> count). Levels covered entirely
  /// by native tiles are absent, not zero-valued.
  std::map<int, std::size_t> derived_by_level;
  /// Parents not written because a NATIVE tile already occupies that
  /// `(level, index)`. Native data always wins on disk.
  std::size_t tiles_suppressed_by_native = 0;
  /// The native levels discovered in the layer, ascending (coarsest first).
  std::vector<int> native_levels;
  /// Whether the freshly-built pyramid actually replaced `overviews/`. False when
  /// the build was refused (@c tiles_skipped > 0), in which case the previous
  /// sidecar is untouched and the staging dir is cleaned up.
  bool sidecar_replaced = false;
};

/// @brief Rebuild `<layer_dir>/overviews/` from the depth layer's native tiles.
///
/// The rebuild is **wholesale**: on success the previous sidecar is discarded,
/// never merged (overviews are a derived, regenerable cache — `uma-ADR-0011`).
/// With @c DepthOverviewOptions::dry_run the guards run and nothing is written.
///
/// **Mixed-level, native-wins.** The layer's native levels are discovered by a
/// single all-level scan (`marine_tiled_raster_store::scanCoverage`). Let
/// `finest` be the finest of them. For each level `L` from `finest - 1` down to
/// @c min_level, the contributors at `L + 1` are the native tiles there **union**
/// the derived tiles just written there — disjoint by construction, since a
/// derived tile is never written where a native one exists. Each parent is
/// folded from at most four children, and **skipped entirely when a native tile
/// already occupies it**. The single-level case falls out as a degenerate
/// instance: one native level, no collisions at any coarser level, so the result
/// is exactly what the pre-#331 builder produced (pinned by a golden-fixture
/// regression test).
///
/// The fold policy is depth **shallowest-preserving** (`uma-ADR-0010` D9): among
/// the valid contributors that land in one coarse cell, the one with the
/// **maximum** ellipsoidal height (most positive / least negative — shoalest,
/// most hazardous to navigation) is selected and its whole `{depth, σ}` pair is
/// carried through — never a mean, and the pair always travels together. A cell
/// whose depth (band 0) is NaN is the no-data sentinel and does not contribute.
///
/// **Coverage manifest + geometric error** (`uma-ADR-0013` D1/D2/D3): the
/// derived coverage is written as `overviews.tmp/coverage.json` **before** the
/// swap, so it rides `uma-ADR-0011`'s rename-aside and is crash-consistent with
/// the sidecar it describes. Each derived tile records a saturated conservative
/// geometric error, `max(level GSD, max child ε)`. The layer's NATIVE coverage is
/// deliberately **not** persisted here: this builder does not own the native
/// tiles, so a file it wrote would go stale on the next import with nothing able
/// to detect it (the scan fallback fires on absence, not staleness).
///
/// **Safety (`uma-ADR-0013` D8)**: no query path consults the sidecar or the
/// manifest. `shallowestReliable()` keeps reading the native tiles to the finest
/// available level for a region, so native-wins costs nothing operationally —
/// it is a display-and-storage decision only.
///
/// **Crash-safe, and atomic where the filesystem allows it.** The pyramid is
/// built into a sibling `overviews.tmp/` and swapped in only on success, so an
/// interrupted or failing run never leaves a truncated sidecar that reads as
/// complete. The swap prefers `renameat2(RENAME_EXCHANGE)`, which exchanges the
/// two directory entries atomically: `overviews/` resolves to a complete sidecar
/// at every instant, so a concurrent reader sees the old pyramid or the new one
/// and never a missing directory. That matters because consumers treat an absent
/// `overviews/` as "no overviews" rather than retrying — CAMP's `GggsTileLayer`
/// skips the directory outright when it does not exist.
///
/// On a filesystem without `RENAME_EXCHANGE` (it needs Linux ≥ 3.15 and
/// per-filesystem support; NFS and some overlay/FUSE mounts do not have it) the
/// swap falls back to rename-aside — `overviews/` → `overviews.old/`, staging →
/// `overviews/`, then `overviews.old/` is dropped. The previous sidecar's
/// CONTENTS are never destroyed before the new one is in place, but the PATH
/// `overviews/` is briefly absent between the two renames; a reader that opens
/// the layer in that window must re-scan rather than cache "no overviews". A
/// crash mid-swap leaves the previous sidecar as `overviews.old/`, recoverable by
/// hand. `overviews.tmp/` also acts as the per-layer run lock: it is claimed with
/// a failing `create_directory`, so a second concurrent build over the same layer
/// refuses rather than trampling the first.
///
/// @param progress Optional stream for per-level progress lines (nullptr = quiet).
/// @return Counts, the discovered native levels, per-level derived counts, the
///   native-suppression count, and @c sidecar_replaced telling whether the swap
///   happened. The build is **refused** (previous sidecar left in place, staging
///   never created) when @c tiles_skipped > 0; the caller should surface that
///   loudly and exit non-zero.
/// @throws std::invalid_argument if @c min_level is outside 0..20, or is not
///   strictly below the layer's finest DISCOVERED native level (the no-upsample
///   guard).
/// @throws std::runtime_error if @c layer_dir is not a directory, holds no usable
///   native tiles at any level, holds tiles that are not the 2-band depth shape
///   at some discovered level (both refuse to replace a good sidecar for an
///   empty, mis-pointed, or wrong-store layer), or already has an
///   `overviews.tmp/` staging directory (concurrent run or crashed-run debris);
///   also on any tile I/O failure.
DepthOverviewBuildResult buildDepthOverviewPyramid(
  const DepthOverviewOptions & opts, std::ostream * progress = nullptr);

/// @brief Internals exposed for unit testing — not a stable public API.
namespace detail
{

/// @brief The shallowest-preserving depth fold policy (`uma-ADR-0010` D9),
///        exposed so its DETERMINISM is directly testable.
///
/// Each contributor is one child cell's whole `{depth (band 0), σ (band 1)}` pair.
/// Returns the shoalest contributor's pair verbatim — maximum ellipsoidal height
/// (band 0), never a mean, the σ carried coherently with its depth. An exact
/// depth tie is broken by a TOTAL order on σ (finite preferred over NaN, then
/// smaller σ — the more reliable pair), so the result depends only on the
/// contributor SET, not its order (the fold engine buckets contributors in
/// unspecified filesystem-iteration order). Precondition: at least one
/// contributor, each with a non-NaN depth (the engine's valid-cell gate).
std::vector<double> depthShallowestFold(
  const std::vector<std::vector<double>> & contributors);

/// @brief The conservative per-tile geometric error for a tile at @p level whose
///        children carry @p child_errors (`uma-ADR-0013` D1/D2).
///
/// `max(level GSD, max child ε)`. `uma-ADR-0013` D2 requires SATURATION — a
/// tile's error must be at least the maximum of its descendants' — and it
/// requires a producer that cannot compute a meaningful error to record "a
/// conservative upper bound rather than omit the field". This is that bound, and
/// it is computable from this producer alone: GGGS's nominal cell size is
/// monotone in level, so a child whose own ε is unrecorded contributes at most
/// its level's GSD, which is strictly smaller than the parent's — saturation
/// holds even across an edge where no native ε exists. Where nothing records a
/// finer error the value degenerates to exactly the level's ground sample
/// distance, which is today's level-as-resolution fallback, so no consumer sees
/// a behaviour change from its arrival.
///
/// @param child_errors One entry per child: its recorded ε, or `std::nullopt`
///   for a native child whose producer recorded none (its level's GSD is used).
/// @param child_level The children's GGGS level, for that GSD substitution.
double saturatedGeometricError(
  int level, int child_level,
  const std::vector<std::optional<double>> & child_errors);

}  // namespace detail

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__OVERVIEW_PYRAMID_HPP_
