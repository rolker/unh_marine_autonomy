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
#include <string>

/// @file
/// @brief Testable production path for the depth overview-pyramid builder
///        (ADR-0010 D9 / ADR-0011, on the #188 fold engine).
///
/// The `build_depth_overviews` CLI is a thin `main()` over the two entry points
/// here: `parseDepthOverviewArgs` (argv -> options, no side effects) and
/// `buildDepthOverviewPyramid` (the disk fold). Extracting them makes the grid
/// reconstruction, per-level fold, level loop and argument parsing unit- and
/// integration-testable without spawning a process (the generic cross-tile
/// engine lives in `marine_tiled_raster_store::overview_builder.hpp`; only the
/// depth **fold policy** — shallowest-preserving, ADR-0010 D9 — is here).
///
/// Scope (ADR-0010 D9): only the `draft` and `processed` depth layers get a
/// generated pyramid. `chart` inherits the ENC scale ladder (no pyramid);
/// `reference` stays as-imported. Upsampling is never done — the builder only
/// folds finer tiles into coarser parents, never the reverse.

namespace marine_bathymetry_store
{

/// @brief Parsed options for one depth overview-pyramid build.
struct DepthOverviewOptions
{
  std::string layer_dir;   ///< the layer directory (`draft/` or `processed/`)
  int fine_level = 13;     ///< the layer's native GGGS level (fine tiles' level)
  int min_level = 0;       ///< coarsest level to build (0 = apex)
  /// Run the guards and report what would be built, writing nothing — the check
  /// before pointing a destructive rebuild at a mistyped path.
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
DepthArgStatus parseDepthOverviewArgs(
  int argc, char ** argv, DepthOverviewOptions & out);

/// @brief Aggregate result of a pyramid build (for logging and exit codes).
struct DepthOverviewBuildResult
{
  std::size_t tiles_written = 0;   ///< total overview tiles written
  /// Input tiles skipped for a grid-reconstruction mismatch. **Any skip refuses
  /// the swap** — the pyramid is missing that tile's coverage, so it must not
  /// displace a previously-complete sidecar. The caller should exit non-zero.
  std::size_t tiles_skipped = 0;
  int coarsest_level = -1;         ///< coarsest level produced (-1 = none built)
  bool early_empty = false;        ///< a level above @c min_level produced nothing
  /// Whether the freshly-built pyramid actually replaced `overviews/`. False when
  /// the build was refused (@c early_empty or @c tiles_skipped > 0), in which
  /// case the previous sidecar is untouched and the staging dir is cleaned up.
  bool sidecar_replaced = false;
};

/// @brief Rebuild `<layer_dir>/overviews/` from the depth layer's fine tiles.
///
/// The rebuild is **wholesale**: on success the previous sidecar is discarded,
/// never merged (overviews are a derived, regenerable cache — ADR-0011). With
/// @c DepthOverviewOptions::dry_run the guards run and nothing is written.
///
/// Each level is folded from the level below it (the finest from the fine layer,
/// every coarser one from the sidecar just written), down to @c min_level. The
/// fold policy is depth **shallowest-preserving** (ADR-0010 D9): among the valid
/// contributors that land in one coarse cell, the one with the **maximum**
/// ellipsoidal height (most positive / least negative — shoalest, most hazardous
/// to navigation) is selected and its whole `{depth, σ}` pair is carried through
/// — never a mean, and the pair always travels together. A cell whose depth
/// (band 0) is NaN is the no-data sentinel and does not contribute.
///
/// **No upsample**: the builder only produces parent levels (L-1 from L), never a
/// finer level than the fine tiles — enforced by the `min_level < fine_level`
/// range check and by folding strictly toward the apex.
///
/// **Crash-safe** (not atomic — POSIX has no atomic directory swap): the pyramid
/// is built into a sibling `overviews.tmp/` and swapped in only on success, so an
/// interrupted or failing run never leaves a truncated sidecar that reads as
/// complete. The swap is rename-aside — `overviews/` → `overviews.old/`, staging
/// → `overviews/`, then `overviews.old/` is dropped — so the previous sidecar
/// exists at every instant; a crash mid-swap leaves it as `overviews.old/`,
/// recoverable by hand. `overviews.tmp/` also acts as the per-layer run lock: it
/// is claimed with a failing `create_directory`, so a second concurrent build
/// over the same layer refuses rather than trampling the first.
///
/// @param progress Optional stream for per-level progress lines (nullptr = quiet).
/// @return Counts, an @c early_empty flag set when a level above @c min_level
///   yields no tiles (a broken fine-tile chain), and @c sidecar_replaced telling
///   whether the swap happened. The build is **refused** (previous sidecar left
///   in place, staging cleaned up) when @c early_empty or @c tiles_skipped > 0;
///   the caller should surface either loudly and exit non-zero.
/// @throws std::invalid_argument if @p opts holds an out-of-range level (this is
///   also the no-upsample guard: @c min_level must be strictly below
///   @c fine_level).
/// @throws std::runtime_error if @c layer_dir is not a directory, holds no fine
///   tiles at @c fine_level, holds tiles that are not the 2-band depth shape
///   (both refuse to replace a good sidecar for an empty, mis-pointed, or
///   wrong-store layer), or already has an `overviews.tmp/` staging directory
///   (concurrent run or crashed-run debris); also on any tile I/O failure.
DepthOverviewBuildResult buildDepthOverviewPyramid(
  const DepthOverviewOptions & opts, std::ostream * progress = nullptr);

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__OVERVIEW_PYRAMID_HPP_
