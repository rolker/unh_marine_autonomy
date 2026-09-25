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
#include <cstdint>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "marine_autonomy/gggs.h"

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

// --- Multi-band (rev-3) overviews -------------------------------------------
//
// Everything below writes the **rev-3** world store's 4-band overview tile
// (`docs/world_store_design.md` §7, spine decision 2): MIN, MEAN, COUNT and σ
// per parent cell instead of one folded `{depth, σ}` pair. It is ADDITIVE —
// `buildDepthOverviewPyramid` above and the `draft/processed/reference/chart`
// tree it serves are untouched. The two schemas coexist deliberately: a changed
// process is a new fingerprint, never a migration.

/// @brief Which rule produces the overview tile's σ band.
///
/// **The rule is NOT decided** (Roland, 2026-09-22: "this seems like something
/// that should be thought about much more"). Rev 2's "mean and max of the
/// children" named two numbers without saying how they combine into one stored
/// value, so it was never a decision — see `docs/world_store_design.md` §7.
///
/// Until it is decided the writers default to @c kUndecided: the fourth band is
/// RESERVED and written as nodata, and the rule name is recorded in the tile's
/// per-tile record (`<level>_<row>_<col>.json`, which `marine_world_store`'s
/// `mws_regenerate_catalog` carries into the tile's STAC Item as
/// `mws:sigma_fold`) so the later decision is a new fingerprint rather than a
/// migration. The candidates are implemented here
/// because the measurement that decides between them must exercise the same
/// arithmetic the writer would — not a second copy of it.
enum class SigmaFold
{
  /// Write nodata. The default, and the only rule the CLI can select.
  kUndecided,
  /// Within-child variance plus the spread of the child means, count-weighted,
  /// over the children that carry a σ only (owner decision 2026-09-25): a
  /// σ-less child is left out of the σ fold, never counted as zero variance.
  kPooled,
  /// The largest child σ.
  kMaxChild,
  /// The count-weighted mean of the child σ.
  kMeanChild,
};

/// @brief The rule's stable name, as recorded on disk and in Items
///        (`"undecided"`, `"pooled"`, `"max_child"`, `"mean_child"`).
///
/// The string is part of the tile's recorded provenance, so it is a contract:
/// changing a spelling changes every fingerprint that quotes it.
std::string sigmaFoldName(SigmaFold rule);

/// @brief Parsed options for one multi-band overview-pyramid build.
struct MultiBandOverviewOptions
{
  std::string layer_dir;   ///< a rev-3 quantity layer (`<root>/depths/<state>/<origin>/`)
  int min_level = 0;       ///< coarsest level to build (0 = apex)
  bool dry_run = false;    ///< run the guards, report, write nothing
  /// Which σ rule to apply. Left at @c kUndecided the σ band is nodata.
  SigmaFold sigma_fold = SigmaFold::kUndecided;
};

/// @brief Rebuild `<layer_dir>/overviews/` as a 4-band MIN/MEAN/COUNT/σ pyramid.
///
/// Same level discovery, native-wins rule, staging directory, run lock and
/// atomic swap as `buildDepthOverviewPyramid` — only the tile schema and the
/// fold policy differ. Native children are 2-band `{depth, σ}` and are PROMOTED
/// to `{depth, depth, 1, σ}` as they are read, so one fold serves both the
/// first derived level and every level above it (see
/// `detail::promoteNativeDepthCell`).
///
/// **The σ band is reserved, not written**, unless
/// @c MultiBandOverviewOptions::sigma_fold names a decided rule. The sidecar
/// records the schema and the rule name in `overview_schema.json` beside the
/// tiles, so a reader never has to infer which of the four bands is meaningful.
///
/// **Geometric error** (`uma-ADR-0013` D1/D2/D3) is recorded per tile in the
/// staged `coverage.json` exactly as the single-band writer records it —
/// `max(level GSD, max child ε)`, saturated, so a rev-3 overview tile satisfies
/// the nesting condition the D7 selection core (uma#395) will rely on.
///
/// **Cross-schema guard**: if `<layer_dir>/overviews/` already holds tiles of a
/// DIFFERENT band count, the build is refused rather than silently replacing a
/// single-band pyramid with a multi-band one (and vice versa in the single-band
/// writer). Consumers read the sidecar by band index; swapping the schema under
/// them is the one mistake neither writer can detect after the fact. The band
/// count is read from `overview_schema.json` when there is one, else from one
/// tile; a record or probe tile that cannot be read is REFUSED, named, rather
/// than treated as "no schema in residence".
///
/// @throws Everything `buildDepthOverviewPyramid` throws, plus
///   `std::runtime_error` on the cross-schema guard.
DepthOverviewBuildResult buildMultiBandDepthOverviewPyramid(
  const MultiBandOverviewOptions & opts, std::ostream * progress = nullptr);

/// @brief Outcome of folding ONE parent tile (`buildMultiBandDepthOverviewParent`).
struct MultiBandParentResult
{
  /// Whether the parent tile was written. False when no child exists, or when a
  /// native tile already occupies the parent's `(level, index)`.
  bool written = false;
  /// The parent was left to a native tile at the same `(level, index)`.
  bool suppressed_by_native = false;
  /// How many of the up-to-four children were found and folded.
  std::size_t children_used = 0;
  /// The saturated geometric error recorded for the written tile
  /// (`uma-ADR-0013` D1/D2); NaN when nothing was written.
  double geometric_error_m = 0.0;
  /// A DERIVED tile already at the parent's index was removed, because it no
  /// longer describes anything: a native tile now occupies that index
  /// (native-wins), or no child of it exists any more. Leaving it would keep
  /// advertising coverage that is not there — and, in the native-wins case,
  /// make the next coarser fold refuse the layer as "both native and derived".
  bool removed_stale = false;
};

/// @brief One parent a per-parent run should build, with the inputs it folds.
struct MultiBandOverviewParent
{
  gggs::GridIndex parent;
  /// The contributing children, as paths RELATIVE to the layer directory: a
  /// native child is `<name>`, a derived one `overviews/<name>`. A DAG uses
  /// these as the job's inputs, so a changed, added or vanished child reruns
  /// exactly the parent it feeds — without the DAG knowing any GGGS arithmetic.
  std::vector<std::string> children;
};

/// @brief Fold ONE parent tile from its up-to-four children and write it.
///
/// The per-parent work unit the design draft's Part 4 owes and the prototype's
/// component 5 asked for: `build_depth_overviews` is one batch call, so a
/// regenerate re-folds a whole layer to refresh one tile. This entry point is
/// what Snakemake's per-tile rules invoke, so the DAG — not a full rebuild —
/// decides what is stale.
///
/// Children are looked up in two places, exactly as the batch builder's
/// contributor sets are: a NATIVE child (2-band, promoted on read) directly in
/// @p layer_dir, and a DERIVED child (4-band) in `<layer_dir>/overviews/`. The
/// two are disjoint by construction — a derived tile is never written where a
/// native one exists — so a child found in both is a corrupted layer and throws
/// rather than resolving silently by precedence.
///
/// **Native-wins** still holds: if a native tile occupies the parent's own
/// `(level, index)`, nothing is written and @c suppressed_by_native is set.
/// A derived tile left at that index by an earlier run is REMOVED (with its
/// record), as is one whose children have all gone — see
/// @c MultiBandParentResult::removed_stale and
/// `pruneMultiBandOverviewLevel`, which finds such tiles without being told
/// their index.
///
/// **Atomicity is per tile**, not wholesale: the tile is written to a unique
/// temporary beside its destination and renamed over it, which `rename(2)` makes
/// atomic within a directory. There is no `overviews.tmp/` staging copy and no
/// run lock — a single tile has no partial-pyramid hazard to guard against, and
/// a per-parent DAG runs many of these concurrently over one directory, which a
/// whole-sidecar lock would serialise into the batch builder it replaces.
///
/// Each written tile also gets a per-tile `<level>_<row>_<col>.json` sidecar
/// carrying its `geometric_error_m`, band schema and σ rule. That is what makes
/// the manifest race-free under a parallel DAG: nothing rewrites a shared
/// `coverage.json` mid-run, and the assembly step reads the per-tile records
/// once the DAG is done (`mws_assemble_coverage` in `marine_world_store`).
/// Before its first tile it also writes `overviews/overview_schema.json` when
/// absent — the same record the batch builder leaves — and it refuses a
/// sidecar whose record names a different σ rule.
///
/// **Guards run first**: the cross-schema guard (as in the batch builder) and
/// the σ-rule check run before any tile is removed or any child loaded, so a
/// mis-pointed call leaves `overviews/` exactly as it found it.
///
/// @param layer_dir The rev-3 quantity layer holding the native tiles.
/// @param level Parent level; @p row, @p col its GGGS index at that level.
/// @param rule The σ rule; @c kUndecided (the default) writes the band as nodata.
/// @throws std::invalid_argument if @p level / @p row / @p col do not name a
///   grid that exists at that level.
/// @throws std::runtime_error if @p layer_dir is not a directory, if a child is
///   present both natively and as a derived tile, or on any tile I/O failure.
MultiBandParentResult buildMultiBandDepthOverviewParent(
  const std::string & layer_dir, int level, uint32_t row, uint32_t col,
  SigmaFold rule = SigmaFold::kUndecided);

/// @brief The parent tiles at @p parent_level that a per-parent run should build.
///
/// What a Snakemake DAG enumerates before it can schedule anything. It is here,
/// not in the rules, because the parent↔child mapping is GGGS
/// (`gggs::parent`), whose column counts vary by latitude band — a second
/// implementation of that arithmetic in Python would be a second thing to get
/// wrong, and the one that is wrong is the one with no tests.
///
/// Children are the native tiles in @p layer_dir and the derived tiles already
/// in `<layer_dir>/overviews/`. A parent already occupied by a NATIVE tile is
/// omitted: native data wins on disk, so scheduling it would only produce a
/// suppressed no-op per invocation.
///
/// @return The parents, in GGGS order (row then column), deduplicated.
/// @throws std::invalid_argument if @p parent_level has no child level.
/// @throws std::runtime_error if @p layer_dir is not a directory.
std::vector<gggs::GridIndex> listMultiBandOverviewParents(
  const std::string & layer_dir, int parent_level);

/// @brief `listMultiBandOverviewParents`, with each parent's contributing
///        children named (see @c MultiBandOverviewParent::children).
///
/// @throws Everything `listMultiBandOverviewParents` throws, plus
///   `std::runtime_error` when a child is present both natively and as a
///   derived tile — the same corrupted-layer refusal the fold makes, raised
///   while the DAG is still being planned rather than mid-run.
std::vector<MultiBandOverviewParent> listMultiBandOverviewParentInputs(
  const std::string & layer_dir, int parent_level);

/// @brief Remove the DERIVED tiles at @p level that no longer describe anything.
///
/// A derived tile is stale when a native tile now occupies its index
/// (native-wins: compiled data replaced the fold), or when none of its
/// children — native in @p layer_dir or derived in `overviews/` — exists any
/// more. A per-parent DAG only ever schedules parents that HAVE children and
/// no native tile, so without this nothing would ever remove such a tile: it
/// would stay in `overviews/`, in the assembled coverage manifest and in the
/// derived index, and a native-covered one would make the next coarser fold
/// refuse the layer.
///
/// Removes each stale tile with its per-tile record (`.json`) and content
/// sidecar (`.fp`). Run it for a level only once the level below is final —
/// the regenerate DAG runs it immediately before listing that level's parents.
///
/// @return The indices removed, in GGGS order.
/// @throws std::invalid_argument if @p level is not a GGGS level.
/// @throws std::runtime_error if @p layer_dir is not a directory, or a removal
///   fails.
std::vector<gggs::GridIndex> pruneMultiBandOverviewLevel(
  const std::string & layer_dir, int level);

/// @brief Remove EVERY derived tile at @p level, with its record and `.fp`.
///
/// For a level the pyramid no longer builds: a regenerate configured with a
/// coarser `min_level`, or a finer `fine_level`, than the run that wrote
/// `overviews/` leaves whole levels nothing would ever rebuild or prune — yet
/// the coverage manifest and the overview Items scan all of `overviews/`, so
/// they would stay published, going stale. The regenerate DAG calls this for
/// every derived level outside its configured range. Native tiles are never
/// touched.
///
/// @return The indices removed, in GGGS order.
/// @throws Everything `pruneMultiBandOverviewLevel` throws.
std::vector<gggs::GridIndex> removeMultiBandOverviewLevel(
  const std::string & layer_dir, int level);


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


/// @brief Band order of the rev-3 multi-band overview tile (§7, spine 2).
///
/// Named constants rather than bare 0..3 because every consumer of the sidecar
/// reads it BY INDEX: a silent reordering is the one change no reader could
/// detect. The names are also what `overview_schema.json` records on disk.
enum MultiBandIndex : std::size_t
{
  /// The shoalest depth — the design's MIN. In this store's ellipsoidal-height
  /// convention (positive up) shoalest is the MAXIMUM height, so "MIN" is the
  /// min of DEPTH, not of the stored number. The value is bit-identical to
  /// `depthShallowestFold`'s band 0 for the same contributor set.
  kMultiMinBand = 0,
  kMultiMeanBand = 1,    ///< count-weighted mean of the contributors' means
  kMultiCountBand = 2,   ///< how many NATIVE cells this cell summarises (lineage)
  kMultiSigmaBand = 3,   ///< RESERVED — nodata until §7's σ rule is decided
};

/// @brief Band count of the multi-band overview tile.
constexpr std::size_t kMultiBandCount = 4;

/// @brief Promote one native `{depth, σ}` cell to the 4-band schema:
///        `{depth, depth, 1, σ}`.
///
/// A native cell IS a one-cell summary of itself: its min and its mean are the
/// depth, its lineage count is 1, and its σ is its own. Promoting on read is
/// what lets ONE fold serve the first derived level (native children) and every
/// level above it (derived children) — the alternative, a second fold for the
/// native step, is two implementations of the same rule that drift apart.
std::vector<double> promoteNativeDepthCell(const std::vector<double> & native);

/// @brief The rev-3 multi-band fold (`docs/world_store_design.md` §7, spine 2).
///
/// Each contributor is one child cell in the 4-band schema (a native cell
/// arrives already promoted). Returns the parent cell's four bands:
/// - MIN: the maximum of the contributors' MIN band — shoalest wins, exactly as
///   `depthShallowestFold` selects, so the MIN band of a multi-band pyramid is
///   bit-identical to a single-band pyramid's depth band over the same inputs.
///   (No σ tie-break is needed or possible here: only the number is carried,
///   not a pair, so an equal-depth tie is already the same value.)
/// - MEAN: `Σ(mean_i × count_i) / Σ count_i`.
/// - COUNT: `Σ count_i` — the parent's lineage, the thing that makes a mean
///   readable as evidence rather than as a measurement.
/// - σ: per @p rule; @c SigmaFold::kUndecided writes NaN.
///
/// **Defensive substitution, and why.** A contributor whose COUNT is not a
/// finite number ≥ 1 is counted as 1, and one whose MEAN is NaN contributes its
/// MIN. Both cases mean an upstream tile is malformed; emitting a parent cell
/// whose COUNT disagrees with the existence of its MIN would corrupt the
/// lineage a consumer reads to decide whether to trust the MEAN at all, which
/// is worse than a conservative substitution that is at least self-consistent.
///
/// **σ beyond the first derived level.** Only the first fold above the native
/// level sees real per-cell σ: from there up the σ band is whatever the rule
/// wrote, which under @c kUndecided is NaN. Every candidate leaves a σ-less
/// child out of the σ fold, so over such children each one yields nodata at
/// level 2 and above. That is a property of leaving the rule open, not a
/// defect of the arithmetic, and it is exactly why the measurement that decides
/// the rule reads NATIVE cells and carries each candidate's own σ forward
/// rather than re-folding the pyramid.
///
/// Precondition: at least one contributor, each with a non-NaN MIN band (the
/// engine's valid-cell gate) and exactly @c kMultiBandCount bands.
std::vector<double> depthMultiBandFold(
  const std::vector<std::vector<double>> & contributors,
  SigmaFold rule = SigmaFold::kUndecided);

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
