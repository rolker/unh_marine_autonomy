# ADR-0011: Overview Pyramids — Sidecar Layout and Fold-Policy Contract

## Status

Accepted (2026-07-24). Tracked by
[rolker/unh_marine_autonomy#188](https://github.com/rolker/unh_marine_autonomy/issues/188).

Implements the overview half of [ADR-0010](0010-geospatial-world-model.md) D9
(per-layer LOD) and **extends it to the imagery theme**: D9's overview clause
names the depth layers; imagery keeps its own tiering per D3, and this ADR
gives its survey-born layers the same derived-overview mechanism. Referenced
by header pointers from [ADR-0002](0002-bathymetric-data-store.md),
[ADR-0006](0006-multi-platform-backscatter-store.md), and
[ADR-0007](0007-mbes-backscatter-store.md) (their layers gain a derived
sidecar, no change to their fine-tile formats). The **MBES backscatter** layer
named under the imagery MEAN policy in §4 lives in ADR-0007; sidescan is the
first batch adopter, an MBES builder a later one.

**Amended 2026-09-16 ([#389](https://github.com/rolker/unh_marine_autonomy/issues/389)):**
§6 adds the **source catalog** a builder records beside its sidecar, the
**staleness check** and **`--if-stale`** trigger mode that read it, and the
**incremental refold** that re-folds only the changed native tiles' own slots
and ancestors. This makes the depth builder incremental; §5's clause about the
CAMP live-coverage cache adopting the engine is unaffected. The sidescan
builder and the MBES backscatter builder
([#390](https://github.com/rolker/unh_marine_autonomy/issues/390)) adopt the
same engine pieces when they next change.

## Decision

1. **Cross-tile GGGS parent tiles.** Overviews are tiles at coarser GGGS
   levels, folded from the level below: in temperate bands 4 children → 1
   parent. Each level is built from the one below it, down to the apex
   (level 0) by default. Files-to-touch shrinks geometrically with zoom-out.

2. **Per-layer `overviews/` sidecar, flat, filename-addressed.** Overviews for
   a layer live in a single flat `<layer>/overviews/` directory; tiles are
   named `<level>_<row>_<col>.tif` exactly as in the fine layer (the level
   rides in the filename — no per-level subdirectories). This is the consumer
   contract (CAMP's LOD loader, the level-aware query). The sidecar is
   **derived and regenerable**: builders delete and recreate it wholesale
   (idempotent; safe after every ingest), it is never merged into the fine
   layer, and it never enters anti-entropy/possession sets. Native coarse
   data can never be confused with derived overviews. Regeneration is
   **crash-safe, and atomic where the filesystem allows it**: a builder writes
   into a sibling `overviews.tmp/` and swaps it in only after every level
   succeeds, so an interrupted or failed run leaves the previous sidecar intact
   rather than a truncated one a consumer would read as complete. The swap
   prefers **`renameat2(RENAME_EXCHANGE)`**, which exchanges the two directory
   entries atomically, so `overviews/` resolves to a complete sidecar at every
   instant and a concurrent reader never observes a missing directory. Where
   that is unavailable (pre-3.15 kernels, NFS, some overlay/FUSE mounts) the
   swap falls back to **rename-aside**: `overviews/` → `overviews.old/`,
   staging → `overviews/`, then `overviews.old/` is deleted. On the fallback
   path the previous sidecar's *contents* are never destroyed before the new one
   is in place, but the *path* `overviews/` is briefly absent between the two
   renames — **a consumer must treat a missing `overviews/` as "re-scan", not as
   a cached "this layer has none"**. A crash between the two renames leaves the
   previous sidecar as `overviews.old/`, recoverable by hand, and a failing
   second rename is rolled back. A builder also refuses to replace `overviews/` unless
   the layer holds fine tiles at the declared level (an empty or mis-pointed
   layer never destroys a good sidecar), and refuses when any fine tile was
   skipped (a partial pyramid must not displace a complete one).
   `overviews.tmp/` doubles as the **per-layer run lock**: it is claimed with a
   failing `create_directory`, so two concurrent builds cannot trample one
   staging directory. A stray `overviews.tmp/` is a crashed run's debris and
   must be removed by hand before the next build — deliberate, since silently
   deleting it would defeat the lock. A stray `overviews.old/` needs no such
   care: the next successful build reclaims it automatically (the rename-aside
   deletes any leftover `overviews.old/` before retiring the current sidecar,
   and it is created only after staging is complete, so it is never the sole
   copy).

   **Amended by [#331](https://github.com/rolker/unh_marine_autonomy/issues/331)
   (mixed-level layers).** Two clauses above are superseded for the depth
   builder; both remain as written for `build_sidescan_overviews`, whose store is
   genuinely single-level.

   - *"refuses to replace `overviews/` unless the layer holds fine tiles **at the
     declared level**"* — there is no declared level any more. The depth builder
     **discovers** the layer's native levels with one all-level scan
     ([ADR-0013](0013-bounded-lod-navigation.md) D3), and `--fine-level` is
     deleted rather than kept as an assertion. The guard generalises to: refuse
     unless the layer holds at least one usable native tile **at some level**.
     The refusal-on-any-skip clause is unchanged but now has a **wider surface** —
     an unreadable tile name at any level refuses the swap, because under
     discovery that name's coverage would be missing from every level built
     beneath it.
   - *"tiles are named `<level>_<row>_<col>.tif`"* — the sidecar is no longer
     tiles only. It also holds **`overviews/coverage.json`**, the run's derived
     coverage manifest ([ADR-0013](0013-bounded-lod-navigation.md) D3), including
     each derived tile's geometric error (D1/D2). It is written into
     `overviews.tmp/` **before** the swap, so it rides the rename-aside above and
     is crash-consistent with the tiles it describes for free — the property this
     clause exists to guarantee, extended rather than weakened. Consumers of this
     contract (CAMP's LOD loader, the level-aware query) must therefore expect one
     non-`.tif` file in the sidecar; the flat-layout tile loaders already skip
     non-`.tif` regular files silently, so no loader change was needed.

   The **native** coverage manifest is *not* written by the overview builder. It
   does not own the native tiles, so a file it wrote would go stale on the next
   import with nothing able to detect it — the scan fallback fires on a
   manifest's absence, not on its staleness. Persisting native coverage belongs
   with the importers, if and when a consumer needs it.

   **Fold scope (#331): derived overviews FILL GAPS beneath native data; they
   never merge into it.** A derived tile is written only at a `(level, index)`
   that holds no native tile. This is deliberately *not* a merge policy: folding
   a fine harbour band up into a natively-compiled coarse tile would collide
   universally, and declining to create that fold removes the question of what it
   should mean. See [ADR-0010](0010-geospatial-world-model.md) D9's `reference`
   entry for the rule, its [ADR-0013](0013-bounded-lod-navigation.md) D8 safety
   argument, and the storage-rule/display-rule inversion that must be read with
   it.

3. **The fold's load-bearing mapping is `gggs::parent()` / `gggs::children()`
   (`marine_autonomy/gggs/index_math.h`) plus per-cell geographic
   accumulation** — each child cell's centre is located in the parent grid
   via `gggs::CellIndex`. Going through geography (not row/column halving)
   stays correct across the polar `latitudeScaleFactor` bands.

4. **Fold policies are per-store; the engine is shared.** The generic engine
   (`marine_tiled_raster_store/overview_builder.hpp`) folds whole cells (all
   bands of a contributor together) so cross-band-coherent policies are
   expressible. Policies:
   - **Imagery (sidescan, MBES backscatter): MEAN** of valid contributors per
     band. In the sidescan 3-band tile, intensity and quality fold by mean;
     the **source band is 0 in every overview** — a composite has no single
     source; provenance readers must use fine tiles. **Validity is the
     quality band, not intensity**: the processed store's no-data sentinel is
     `quality == 0` (a cell starts there; a real return's quality is floored to
     ≥1 by `marine_backscatter::grazingQuality`), whereas intensity is an
     unfloored clamp of the sample. A zero-intensity, non-zero-quality cell is
     an **acoustic shadow** — surveyed, real, dark — and must fold; gating on
     intensity would erase every shadow and bias overviews bright.
   - **Depths: SHALLOWEST-PRESERVING, never mean** (ADR-0010 D9): the coarse
     cell carries its shoalest-reliable child's whole {depth, σ} pair, kept
     coherent. A mean would let a coarse corridor query plan over a rock.
     Safe by construction for every consumer, so depth overviews may feed the
     level-aware query; the conservative (shoal-exaggerating) world-zoom look
     is accepted — cartographic generalization does the same. **Reserved, not
     implemented here** — the depths pyramid follows the ADR-0010 D8
     re-split. (Shipped in
     [uma#320](https://github.com/rolker/unh_marine_autonomy/pull/320);
     generalised to mixed-level layers in
     [#331](https://github.com/rolker/unh_marine_autonomy/issues/331).)

5. **Batch first, incremental later.** `build_sidescan_overviews` regenerates
   the sidescan sidecar as an offline batch step after ingest. The live
   coverage cache (camp ADR-0006/0010) later adopts the same fold engine
   incrementally, replacing its full-size `foldIntoParent` overviews — the
   root fix for camp#171 (fold frees no memory) — but that is CAMP-side work
   (step 4 of the sequence), out of scope here.

6. **Staleness is recorded, checkable, and repairable in place
   ([#389](https://github.com/rolker/unh_marine_autonomy/issues/389)).**
   Nothing re-ran the batch builders after an import, and nothing could tell a
   current sidecar from a week-old one: on 2026-09-15 the dev host's
   `depths/processed/overviews/` was a fold of a store regenerated a week
   later, still carrying the blunders the regen had removed, and CAMP drew it
   as truth. Three additions, all on the builder side of the contract:

   - **Source catalog.** A builder writes `overviews/source.json`
     (schema `overview-source/1`) into staging beside `coverage.json`, so it
     rides the same swap and is crash-consistent with the tiles it describes.
     It records the **native tile catalog the pyramid was folded from**: every
     regular `*.tif` directly in the layer directory — well-named or not, since
     a mis-named tile appearing or changing is a change — with each file's
     `name`, `size` and `mtime_ns` as the filesystem reports them, sorted by
     name; a `digest` of that list; the builder's **policy version** (a string
     the depth builder bumps whenever its fold or error-saturation rules
     change); the run's `min_level`; and the levels built. The catalog is a
     **stat pass, never a tile read**: milliseconds at any store size, which is
     what lets the check run unconditionally after every import. It is taken
     **after the last native tile has been read**, never at the start of the
     run — a fold takes minutes to hours, and a catalog captured first would
     record the new mtime of a tile an importer rewrote mid-run under content
     that was never folded, and the pyramid would then read current forever.
     Before the swap the builder re-stats the layer; any drift from the
     catalog it is about to write fails the run with the previous sidecar
     untouched. What the catalog detects: added, removed and rewritten tiles.
     What it does not: a rewrite that preserves size and mtime to the
     filesystem's resolution (a same-size rewrite inside one second on a
     1-s-granularity mount) — accepted; a content hash would close that gap
     only by reading every tile (gigabytes under depth-adaptive levels), which
     is what the check exists to avoid. Inode numbers are deliberately **not**
     recorded: they would make a plain copy of a store (`rsync`, the
     salmon→dev sync) read as stale and force a full rebuild for no change in
     content. The catalog is **derived and advisory** exactly as
     `coverage.json` is: an absent, unreadable or schema-mismatched
     `source.json` means "staleness unknown", never "current". **Migration:**
     every sidecar that exists today has no `source.json`, so the first
     `--if-stale` run over it is a full build — which is the Massabesic case
     this amendment exists for.

   - **Staleness check and trigger mode.** `build_depth_overviews <layer>
     --check` answers two questions and writes nothing: are the **inputs**
     unchanged (recomputed catalog equals the recorded one, digest first, then
     the lists), and is the **sidecar intact** (the `.tif` set in `overviews/`
     equals the set `coverage.json` declares — a hand-deleted tile or a
     half-retired directory is damage, not currency). Exit **0** = current on
     both counts; **3** = stale, printing the added / removed / changed tile
     counts (and the first few names of each) or the sidecar damage found;
     **5** = unknown (no sidecar, no `source.json`, or one this builder cannot
     read). **4 stays what it already means** in this CLI — a build refused
     because a native tile could not be reconstructed (`tiles_skipped > 0`),
     sidecar unchanged — and **6** is new: the layer is busy (`overviews.tmp/`
     exists: a concurrent build, or a crashed run's debris the operator must
     clear). `--if-stale` builds when the check answers stale or unknown and
     returns the build's own codes; when the check answers current it is a
     no-op — exit 0, one line — so an import pipeline calls it
     unconditionally after every ingest and the rebuild-on-update rule stops
     depending on anyone remembering. A refused build (4) is a real defect in
     the layer to fix, not a reason to loop: a mis-named tile changes the
     catalog, the next `--if-stale` builds, and the build refuses again,
     loudly, every time until the tile is fixed — by design. The trigger lives
     in the **general regenerate process** for the world store (the design draft's
     dependency walk, [#391](https://github.com/rolker/unh_marine_autonomy/issues/391)),
     which runs it for every layer an import touched and fails the run on any non-zero exit
     (it holds its own store lock, so it never meets 6). Until that process exists, the
     platform import script
     ([rolker/unh_echoboats_project11#490](https://github.com/rolker/unh_echoboats_project11/issues/490))
     calls it the same way as an interim — platform-specific scripts are not the
     long-term home of store operations (operator, 2026-09-16); the platform repo
     holds configuration, not process. A builder in
     a *different* package cannot be invoked from `import_bag`'s process, and
     the shell already sequences the two, so the trigger is a call site, not a
     library dependency. The same exit codes are the contract a display
     consumer (CAMP's Stores tab, the explorer) will use to **warn** that a
     layer's overviews are stale — a follow-up on the consumer side, and
     warn-only by construction: per [ADR-0013](0013-bounded-lod-navigation.md)
     D8 the staleness of a derived sidecar is a rendering fact, and no safety
     query (shoal-finding, least-depth, clearance) may ever consult it, skip a
     level on it, or change what it reads because of it.

   - **Incremental refold.** The builder refolds incrementally when **all** of
     these hold: `source.json` and `coverage.json` are both readable, the
     recorded policy version and `min_level` match the run's, and the sidecar
     is intact (its `.tif` set equals the manifest's). Otherwise — including
     `--full` — it folds wholesale and says in one line why. Incrementally, it
     diffs the two catalogs and computes the **dirty set**: for every added,
     removed or changed native tile, **its own `(level, index)` and every
     ancestor** up to `min_level` (a removed native tile frees its own slot for
     a derived tile folded from the level below, which no ancestor rule would
     reach). Dirty slots are re-folded; every other derived tile is **carried
     over** from the previous sidecar into staging **before** any dirty tile is
     written, and the two sets are disjoint by construction — a carried entry
     is never a name the refold will write, so a carry-over that shares the
     live sidecar's storage (a hard link) can never be truncated through by a
     write. The dirty set is exact by induction over the fold order: a slot is
     re-folded iff a native tile at or beneath it changed; its contributors
     are the native tiles at the child level plus the derived children, which
     are either re-folded in this run (their own subtree changed) or carried
     over (it did not) — in both cases current. A dirty slot left with no
     contributor is **deleted**, and one now occupied by a native tile is
     dropped (native wins on disk, §2 #331). The new `coverage.json` is
     **carried ∪ refolded − deleted**, with each carried tile's geometric error
     taken from the previous manifest and **pre-seeded into the run's derived
     manifest before folding**, so a refolded parent's error saturates over
     its carried children exactly as [ADR-0013](0013-bounded-lod-navigation.md)
     D2 requires; a previous manifest missing a carried tile's error is
     treated as damage and forces a full fold. The dirty-set walk and the
     carry-over live in the shared engine (`marine_tiled_raster_store`), keyed
     on `gggs::parent()` so the polar-band child counts need no special case;
     the depth builder is the first adopter, and the sidescan and MBES
     builders take it without a policy change when they next change.
     Recording the per-tile catalog rather than a digest alone is what makes
     this possible: the digest answers "is it stale", the catalog answers
     "which tiles".

   **Filesystems.** The three "where the filesystem allows" clauses in this
   ADR degrade together: on the NAS (`/mnt/nadata`, the season archive) and
   other NFS/FUSE mounts, the §2 swap is already rename-aside rather than
   atomic, mtime granularity may be coarse, and hard links may be unavailable
   (carry-over then copies). None of them changes correctness — only the
   width of the swap window, the size of the undetectable rewrite, and the
   cost of a carry-over.

   **What this does not change.** The fold policies of §4, the sidecar layout
   and swap of §2, and the native-wins rule of the #331 amendment are
   untouched. `coverage.json` keeps schema `coverage-manifest/1`; the new
   record is a sibling file so no consumer of the manifest needs to change.
   The **wholesale** language in §2 ("builders delete and recreate it
   wholesale") is now read as *the live sidecar is replaced whole*, not *every
   tile is recomputed*. Mechanism detail this amendment deliberately leaves to
   the plan: the digest's exact preimage, the link-versus-copy carry-over, and
   the report format.

## Consequences

- CAMP's LOD renderer (step 3) reads overview levels by view scale from the
  pinned sidecar path; store opens stop scaling with survey size.
- Survey depth data becomes eligible for coarse-level queries once the depth
  policy lands (after the D8 re-split).
- Every ingest should be followed by an overview rebuild (cheap relative to
  ingest); a stale sidecar renders stale coarse imagery but can never corrupt
  fine data. **Since #389 (§6)** the ingest pipeline runs `--if-stale`
  unconditionally, a stale sidecar is detectable (`--check`, exit 3), and the
  rebuild after a small ingest re-folds only the changed tiles' ancestors.
  A display consumer may warn on a stale sidecar; nothing on a safety path may
  read the signal (ADR-0013 D8).
- Overviews add ~1/3 of a layer's fine-tile volume (geometric series).
- **Deferred, for the depths pyramid:** `marine_bathymetry_store`'s flat-layout
  loader (`tile_io.cpp`) WARNs and skips **any** subdirectory it finds under a
  layer dir (the `#221` flat-layout guard against stale epoch dirs). The reserved
  depth `overviews/` sidecar is such a subdirectory, so when the depths pyramid
  lands (after the ADR-0010 D8 re-split) that loader must be taught to skip
  `overviews/` silently — not warn about it — or the sidecar will trip a spurious
  "ignoring unexpected subdirectory" warning on every load. No action needed for
  sidescan (this run), whose store loader does not share that guard.
