# marine_world_store

The rev-3 world store's foundation: where the store is, how its directories are
named, how a source is identified, how a product's inputs are hashed, and what
its STAC Item promises a consumer.

The design this implements is
[`docs/world_store_design.md`](../docs/world_store_design.md) (rev 3) — read it
first; this README says what the code does, not what the store is for.

**It replaces the existing store; it does not coexist with it.** No
compatibility with `marine_bathymetry_store`'s `draft/`, `processed/`,
`reference/` and `chart/` tree is kept: once rev 3 works end to end, the
existing stores are wiped and rebuilt (owner decision, 2026-09-23). Until then
that tree is only an **input** — the adapter below reads it and refuses a
destination that overlaps the layer it reads. Nothing else guards the old tree,
so on a host that still holds one, build rev 3 under its own `--store-root`:
rev 3's `depths/draft/<origin>/` and the old store's `depths/draft/` layer
share a path at the default root.

## Not a ROS package (but installable as one)

`marine_world_store` is a plain Python package — `setup.py` + `setup.cfg`,
`console_scripts` entry points — with a `package.xml` **shim** beside it so
rosdep resolves its dependencies and colcon installs it into the ROS install
space. Nothing under `marine_world_store/` imports `rclpy` or `ament_*`, or
reads the ROS install space for data files.

That discipline is enforced by tests, not by this paragraph:

| Test | Enforces |
|---|---|
| `test/test_no_ros_imports.py` | No ROS import anywhere in the package; every module imports with no ROS environment sourced |
| `test/test_dependency_lists.py` | `package.xml` `<exec_depend>`s and `setup.cfg` `install_requires` name the same libraries, and every CLI module has an entry point |
| `test/test_no_literal_store_root.py` | The store root is never a literal (see below) |

Tests run unchanged under plain `pytest` from this directory **and** under
`colcon test`. The ament lint tests (`flake8`, `pep257`, `copyright`) skip
themselves off a sourced ROS environment rather than failing.

## The store root is a parameter

Design draft §2: *"The store root is a parameter, defaulting to `~/data/world`
and never a literal in code."* `store_root.py` is the one place that default is
written (`_DEFAULT_ROOT`), and `test_no_literal_store_root.py` greps the whole
repository for the literal and fails on anything its narrow, reasoned allowlist
does not name.

Precedence, highest first:

1. `--store-root`, on every CLI that resolves a root: `mws_import_source`,
   `mws_write_revision`, `mws_link_depth_subset` and `mws_regenerate_catalog`
2. `$WORLD_STORE_ROOT`
3. `store_root:` in `~/.config/marine_world_store/config.yaml`
4. the documented default

The other CLIs take no root at all, because they never resolve one: they are
handed exactly the directory or files they work on (`mws_refresh_fingerprints`,
`mws_assemble_coverage` and `mws_list_tiles` a `LAYER_DIR`,
`mws_measure_sigma_fold` tile paths), and `mws_regenerate_catalog --layer-dir`
likewise ignores the root.

A config file that exists but cannot be read is an **error**, never a silent
fall-through to the default: falling back would write a survey into the wrong
tree because of a stray character, and nothing would say so. So is a
**relative** path from `$WORLD_STORE_ROOT` or the config file — it would
resolve against whichever directory each tool runs in (`--store-root` keeps
ordinary command-line semantics). Every CLI that resolves a root prints which
of the four decided it.

## Modules

| Module | What it is |
|---|---|
| `store_root.py` | The resolver above, plus `StoreRoot` (path + where it came from) |
| `layout.py` | `<root>/<quantity>/<state>/<origin>/` path builders; `State`/`Origin`/`Quantity` enums, so a typo'd directory name is an error rather than a new folder. `trajectories/` and `observations/` are surveyed-only, as §5 says |
| `source_time.py` | The observation interval an Item is dated by: a bag's `metadata.yaml` start plus duration (or the union of its `files:` entries), the union over a product's sources, and a named refusal when neither is derivable |
| `source_identity.py` | The Merkle bag id (§3, spine 1): sha256 over the sorted `<split filename>\t<file key>` lines of a bag's `.mcap` files, file key = git-annex `SHA256E`. `metadata.yaml` and every non-data file are excluded, so `ros2 bag reindex` is a repair rather than a new source. No git-annex dependency |
| `fingerprint.py` | §9's input hash. Additive: an input a stage does not have is omitted, never nulled. Id collections are sorted; `consumer_ordering` is not, because its order *is* the input |
| `coverage.py` | Reads `marine_tiled_raster_store`'s `coverage.json` (`coverage-manifest/1`) — the existing convention for per-tile `geometric_error_m` (uma-ADR-0013 D1–D3), not a parallel one. Tolerant, with the same filename-scan fallback |
| `footprint.py` | A tile's geometry and bbox, read from the raster's own georeferencing with GDAL rather than from a second Python implementation of the GGGS grid maths |
| `item_schema.py` | The Item and Collection documents, as plain dicts, with Part 2's consumer-contract fields. No `pystac`, so the schema is testable where the library is not installed. Every Item it builds is dated (below) |
| `stac_catalog.py` | Validates those documents with `pystac` and writes them — **only the ones that changed** (§9's replica rule), gated on a canonical-JSON content hash, never an mtime |
| `revisions.py` | Append-only `revisions/` records: geometry revisions and datum records. The id is the content hash, so an edited record is detected on read |
| `depth_subset.py` | The native-tile **adapter** (see below) |
| `sigma_fold_measure.py` | The evidence design §7's **open** σ-fold rule is decided from: what each candidate would write, against the true spread of the native cells under a parent. Decides nothing (see below) |
| `fingerprint_sidecar.py` | The regenerate pre-step's `.fp` sidecar — a tile's **content** hash plus the mtime the pre-step gave the tile when that content was recorded, which is *not* §9's input fingerprint; one answers "did this file change?", the other "was this built from the same things?" |
| `overview_records.py` | Assembles the per-tile records the per-parent overview writer leaves into one `coverage-manifest/1` document (uma-ADR-0013 D3), with an entry for every tile on disk — a tile with no record keeps the error the previous manifest gave it |
| `overview_items.py` | The derived overview tiles' STAC Items, built from their lineage: the interval is the union of their children's, the inputs the union of their children's sources and revisions, the frame their children's (see below) |
| `atomic_io.py` | Publishes every file this package writes whole and durably: a private temporary (never a shared `<name>.tmp`), fsync, rename, fsync of the directory |

### Every Item is dated, from its sources

Part 2 line 2 promises a time range, and STAC gives an Item exactly two legal
shapes: one `datetime`, or a null `datetime` with **both** `start_datetime`
and `end_datetime`. There is no third shape for "the time is unknown".

So the rule (operator decision, 2026-09-22): **every Item carries the
observation interval of the material it is made of, and it is derived from the
sources.** A bag directory's `metadata.yaml` records the recording's start and
its duration — that is the interval. A product (a tile, a revision where
applicable) takes the **union** of its sources' intervals: a tile built from
three bags was observed over all three.

An Item with no derivable interval is a **provenance defect**, not a thin
record: it could not be found by the time search line 1 promises. The writer
raises a named error (`source_time.TimeIntervalError`, or the calling module's
own) and writes nothing — `stac_catalog` refuses an undated Item at the point
of writing rather than letting pystac report it as a schema error about a
document the store should never have built.

`metadata.yaml` is excluded from the Merkle source id on purpose (`ros2 bag
reindex` rewrites it and must not mint a new source), which is exactly what
makes it readable here: the **identity** is the sensor data, the **time** is
lookup metadata about it.

`--start`/`--end` (or `start:`/`end:` in a subset manifest) exist for material
that has an interval but does not record one — a cast file, a prior grid. They
are an operator statement, never a default, and a file's modification time is
never used: when a file was copied is not when its data was observed.

### Field names are namespaced

The consumer contract names its fields `state`, `origin`, `fingerprint`; STAC
requires fields outside common metadata to be prefixed, so they are written as
`mws:state`, `mws:origin`, `mws:fingerprint`, … `item_schema.CONTRACT_FIELDS`
maps one spelling to the other. The prefix is a spelling, not a second
vocabulary.

## CLIs

| Command | Does |
|---|---|
| `mws_import_source PATH` | Compute a source's content id, read its recorded interval out of the bag's `metadata.yaml`, and write its `sources/` Item. `--dry-run` prints the id and the interval and writes nothing (and needs no `pystac`); a source that cannot be dated is refused |
| `mws_write_revision DESCRIPTION` | Append one `revisions/` record from a small YAML/JSON description |
| `mws_regenerate_catalog` | Build the overview tiles' Items (removing the Item of a pruned tile) and rebuild each present cell's `collection.json`, writing only what changed. `--layer-dir LAYER_DIR` does exactly one layer — what the regenerate DAG runs |
| `mws_link_depth_subset` | The adapter below |
| `mws_measure_sigma_fold TILE…` | Measure the candidate σ-fold rules over native depth tiles and print a markdown table. Tile paths are **arguments** |
| `mws_refresh_fingerprints LAYER_DIR` | The regenerate pre-step: decide from each tile's content whether it changed, then set its mtime to say so — an unchanged tile gets back its recorded mtime, a changed or new native one is advanced to now and recorded, and a derived (`overviews/`) one whose content is not the recorded content is removed with its record so the DAG rebuilds it. `--record TILE` records a tile the DAG just built, with its build mtime. A symlinked tile is named and left alone (`utime` would reach through it) |
| `mws_assemble_coverage LAYER_DIR` | Write `overviews/coverage.json` from the per-tile overview records, once, after the DAG |
| `mws_list_tiles LAYER_DIR --kind native\|overview` | The data assets of a layer's Items of one kind, one path per line — the tile index's inputs, taken from the record rather than a glob |

A revision description (the fields are `revisions.build_revision`'s arguments):

```yaml
kind: datum
applies_to:
  source_id: 6f1c...
parameters:
  height_offset_m: 1.19
  from_frame: NAD83(2011)
  to_frame: ITRF2020
evidence: |
  PROJ transformation measured 2026-09-16; MaCORS/GEOID18 are NAD83(2011).
reviewer: Roland Arsenault
valid_from: '2026-06-22T00:00:00Z'
```

## `mws_link_depth_subset` is an adapter, not a builder

It takes COG tiles `marine_bathymetry_store` already wrote, copies them
**byte-identical** into `<root>/depths/<state>/<origin>/`, and gives each one
the rev-3 record: a `sources/` Item with the Merkle id of the bag it came from,
a fingerprint over the inputs that are actually known, and the per-tile
`geometric_error_m` read from the producer's own coverage manifest. That
demonstrates the layout, the identity and the Items end to end on real data
without rebuilding the observation→link pipeline, which is future work
(design Part 3/4).

It requires its bag directories for two reasons, not one: they are what the
tiles fingerprint over, and they are what the tiles are **dated** by. Each
source's Item carries its own recording interval and the tile Items carry the
union; a source whose interval cannot be read stops the run before anything is
copied.

Two things it deliberately does **not** claim:

- **No reframing.** A byte-identical copy has not been transformed into the
  store frame, so its Item declares the georeferencing the tile actually
  carries and names the transformation as owed. An Item claiming ITRF2020 over
  tiles written in WGS84 would be a false claim in the record.
- **No re-link.** The datum and geometry records in `revisions/` are applied at
  link; this adapter does not link. A record only enters a tile's fingerprint
  when the caller names it with `--revision`.

The existing store is opened read-only and is never modified. The bag paths are
arguments or entries in a subset manifest — never literals in code:

```bash
mws_link_depth_subset --source-store <existing store> --layer processed \
    --store-root /tmp/rev3 --subset subset.yaml
```

```yaml
# subset.yaml
sources:
  - path: /path/to/logs/bizzyboat_sonar/2026-06-22T13-22-29+00-00
    platform: bizzyboat
  - path: /path/to/a/cast/file
    start: '2026-06-22T14:00:00Z'   # this source's own interval, as a pair
    end: '2026-06-22T14:05:00Z'
levels: [12]
# Optional: the interval for a source that records NONE. It never overrides
# an interval a bag recorded in its metadata.yaml.
start: '2026-06-22T13:22:29Z'
end: '2026-06-22T15:00:00Z'
```

A source named twice (on the command line and in the manifest, or twice in one
manifest) is one source — identified by its content id — and is fingerprinted
and written once.

A tile whose coverage manifest recorded no geometric error gets an Item with
**no** `mws:geometric_error_m` — never a zero, which would claim a perfect tile
to the uma#395 selection core — and the run says how many.

The destination must not overlap the source layer (equal to it, inside it, or
containing it): the existing store is read-only, and a rev-3 tree inside it
would put Items and a `collection.json` into the store the adapter promises not
to touch.

## Overview tiles have Items too

Every product carries the consumer contract's Item (Part 2), and a derived
overview tile is a product. `mws_regenerate_catalog` builds one per tile in
`overviews/` from the per-tile record `build_depth_overview_parent` leaves,
which names the children the tile folded:

- **time** — the union of its children's intervals, down to the native tiles'
  Items, which are dated from their bags. A tile whose lineage is unknown (no
  record, or a child with no Item) is refused, named, like any undated Item;
- **inputs** — the union of its children's sources and revisions, with a
  builder version that names the fold and its σ rule over the children's own
  builder versions, so a decided σ rule is a new fingerprint;
- **frame** — its children's, which must agree (an adapted layer's overviews
  declare the same untransformed frame its native tiles do);
- `mws:geometric_error_m`, `mws:sigma_fold` (`undecided`), the 4-band
  `mws:cell_fields` (MIN, MEAN, COUNT, σ — σ described as reserved nodata), and
  `mws:children`.

The Collection's temporal extent is the **union** of its Items' intervals
(STAC reads `interval[0]` as the overall extent), and it declares the frame its
Items declare.

## Dependencies

| rosdep key | Distribution | Why |
|---|---|---|
| `python3-yaml` | `PyYAML` | The config file and the revision/subset descriptions |
| `python3-gdal` | `GDAL` | Tile footprints, read from the raster itself |
| `python3-pystac` | `pystac` | STAC validation at write time. The Items are plain JSON, so it is droppable if it ever becomes a burden |
| `snakemake` | `snakemake` | The regenerate driver (§9); the rules are in `snakemake/` |
| `python3-numpy` | `numpy` | The σ-fold measurement reduces whole tile rasters band by band |

`python3-pystac` and `snakemake` have no upstream `ros/rosdistro` key yet, so
they resolve through this repo's root `rosdep.yaml` local keys — see the
workspace's dependency policy note (`ros2_agent_workspace#654`). Until
`rosdep install` has run for this repo, the tests that need `pystac` skip with
that reason; nothing here pip-installs anything.

## Build and test

```bash
# From the layer worktree (sources the lower layers):
./core_ws/build.sh marine_world_store
./core_ws/test.sh marine_world_store

# Or, from this directory, with no ROS at all:
python3 -m pytest test/
```

## The σ-fold measurement decides nothing

`docs/world_store_design.md` §7 leaves the overview tile's σ band's fold rule
**open** (Roland, 2026-09-22: "this seems like something that should be thought
about much more"). Rev 2's "mean and max of the children" named two numbers
without saying how they combine into one stored value, so it was never a
decision.

`mws_measure_sigma_fold` produces the evidence the decision is taken from, in
the style of spine decision 2's own `fold_measure`:

```bash
# The Massabesic subset's native depth tiles — a path on the operator's disk,
# passed in, never written into this repo.
ros2 run marine_world_store mws_measure_sigma_fold \
  "$WORLD_STORE/depths/reviewed/surveyed" --steps 3 --output sigma_fold.md
```

Per fold step it reports what each candidate (`pooled`, `max_child`,
`mean_child`) would have written, and how often that σ **covers the true spread
of the native cells under the parent** — the population standard deviation,
accumulated exactly through every step, so the candidates are scored against the
data rather than against a fold of themselves. Rev 2's literal "mean and max" is
printed as the two numbers it is, side by side.

Each candidate is carried forward in its own right: step two folds the σ step
one would have *written* under that rule. Anything else would say nothing about
what a pyramid built under each rule actually holds, and the multi-step
behaviour is what the decision turns on — a per-child statistic cannot grow,
while pooled compounds.

The report ends with no recommendation, and a test asserts it does not. §7's
rule is chosen by a person reading these numbers; a "suggested rule" line would
be that decision taken by the measurement instead. Until one is chosen, the
overview writers emit the σ band as nodata and record `sigma_fold: undecided`.

One approximation, stated: cells are aggregated in aligned 2×2 blocks within a
tile rather than through the GGGS geographic parent mapping. In the non-polar
envelope the two agree away from tile edges, every statistic is per-parent-cell,
and a measurement is not a writer.

## The regenerate workflow (`snakemake/`)

Design §9's regenerate for one rev-3 `depths/` layer, over the per-parent work
unit `marine_bathymetry_store`'s `build_depth_overview_parent` provides:

```bash
snakemake -s "$(ros2 pkg prefix marine_world_store)"/share/marine_world_store/snakemake/Snakefile \
  --config layer_dir="$WORLD_STORE/depths/reviewed/surveyed" fine_level=13 min_level=8 -j8
```

`fine_level` must be the layer's finest native level: a run whose `fine_level`
is coarser than a native tile in the layer is refused before anything is
written or removed (a slip there would otherwise remove the whole derived
pyramid as "levels this run does not build" and exit 0).
`layer_dir` is required and has no default: the store root is resolved by
`store_root.py`, and a path written into the workflow would be the hard-coded
path the guard test forbids (which now scans `Snakefile` too). Each tool is
taken from `--config <tool>_tool=<path>` when given, else from `PATH`, else from
`<prefix>/lib/<package>/` in the sourced ament install space (colcon installs
both packages' executables there, not on `PATH`).

- **The pre-step is the load-bearing part, and it runs when the Snakefile
  loads.** §9 makes fingerprints, not mtimes, the trigger; Snakemake's DAG
  decides from mtimes, while PLANNING — so the reconciliation has to happen
  before planning, not as a job in the DAG. `mws_refresh_fingerprints` decides
  from each tile's **content** and then sets the mtime to say so: a tile
  rewritten byte for byte gets back its recorded mtime, and a changed native
  tile is advanced to now — including one copied in with an *old* mtime
  (`copy2`, `rsync -t`, a restore), which by mtime alone would look older than
  its products. A **derived** tile whose content is not the recorded content
  (a restore from a backup, a partial copy, bit rot), or that has no sidecar,
  is removed with its record and rebuilt as a missing product: advanced to
  now it would look newer than its children, never be rebuilt, and have its
  parents rebuilt from its stale content. Each tile a rule builds is recorded
  with its build mtime (`--record`), so a parent stays newer than the children
  it absorbed even when it came out byte for byte the same. The first refresh
  over a layer with no sidecars counts every native tile as changed and
  removes every overview, so the pyramid is rebuilt once.
  An invocation that does not execute the workflow — a dry run, `--summary`,
  `--list-input-changes`, `--dag`, `--lint`, `--unlock` and the other query
  modes, as Snakemake itself parsed them (abbreviations and profiles
  included) — skips the pre-step and the listing reset; it still creates
  `.regenerate/` (Snakemake's `workdir:`) and takes the run lock.
- **Every rule's inputs and outputs are the real files.** A parent's job takes
  the child tiles it folds as inputs and declares the tile and its record as
  outputs, so a changed child reruns exactly its ancestors, and an added or
  vanished child changes the parent's recorded input set.
- **One `checkpoint` per level, re-derived every run.** The parents at level N
  cannot be enumerated until N+1 exists, because a derived tile is itself a
  contributor. The checkpoint prunes level N (`--prune`: derived tiles a native
  tile now covers, or whose children are gone) and lists its parents with their
  children (`--list-parents`) — never Python in the rules: the parent/child
  mapping is GGGS, whose column counts vary by latitude band, and a test asserts
  it is not reimplemented there. Derived levels coarser than `min_level` are
  removed when the Snakefile loads (`--remove-level`), so a run configured
  differently from the last leaves no stale level published — including a run
  with no level left to build. A derived tile at or finer than `fine_level`
  describes nothing a fold made and is refused by name, never removed by
  guess.
- **A missing product is rebuilt.** The listings, and a rule that asks for
  every derived tile and its record, are deleted when the Snakefile loads and
  are targets of `rule all`, so every run plans them — and a tile or record
  deleted since the last run is a missing output Snakemake rebuilds.
- **`mws_assemble_coverage` is a single serialised step after every parent.**
  The per-parent writer leaves a per-tile record instead of touching a shared
  `coverage.json`, because parallel folds would race over that one file.
- **The bookkeeping steps run every run and write only what changed.** Because
  the listings are re-derived every run, the manifest assembly, the catalog and
  the tile indexes run every run too; the manifest and the Items/Collection are
  rewritten only when their content changed (§9's replica rule), so an
  unchanged layer's published files keep their bytes and mtimes. The catalog
  reads every overview tile's footprint each run — a cost that grows with the
  layer, next to folds that run only for what changed.
- **The catalog is this layer's** (`mws_regenerate_catalog --layer-dir`), not
  whatever tree the environment's store root names. It removes an overview
  Item whose tile was pruned, but a native tile and its Item are the link
  step's: a native tile gone with its Item left behind is refused by name,
  before the Collection is rewritten — remove the Item with its tile, or
  restore the tile.
- **The tile indexes are derived and never synced** (§7): one per band schema
  (`<layer>/index.gti.fgb` for the native tiles, `overviews/index.gti.fgb` for
  the overviews), built from the Items' data assets (`mws_list_tiles`). They are
  written with only the `gdaltindex` options every supported GDAL has, so the
  workflow runs on this repo's GDAL 3.8.4 hosts; reading one **as a raster** is
  GDAL's GTI driver, which needs GDAL ≥ 3.9, and the workflow says so once per
  run on an older host (there the index is an ordinary vector tile index).
- **Host-local files live inside the layer; exclude them from any sync.**
  `.regenerate/` (the run lock, listings and Snakemake's metadata),
  `overviews.lock`, the `*.fp` content sidecars (their mtimes are this host's)
  and the `*.gti.fgb` indexes (absolute paths) are regenerated on each host and
  must not be replicated: for `rsync`, `--exclude=.regenerate/
  --exclude=overviews.lock --exclude='*.fp' --exclude='*.gti.fgb'`.
- **Runs over one layer are serialised** by a lock on
  `<layer>/.regenerate/regenerate.lock`, whatever directory each run starts
  from; the C++ writers additionally exclude a concurrent batch build
  (`<layer>/overviews.lock`).

The end-to-end tests (`test/test_regenerate_workflow.py`) run snakemake for
real over a toy layer, with a stand-in for the C++ per-parent tool that keeps
its command-line contract (`test/fake_build_depth_overview_parent.py`); the
tool's own semantics are pinned by `marine_bathymetry_store`'s tests.
