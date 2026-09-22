# marine_world_store

The rev-3 world store's foundation: where the store is, how its directories are
named, how a source is identified, how a product's inputs are hashed, and what
its STAC Item promises a consumer.

The design this implements is
[`docs/world_store_design.md`](../docs/world_store_design.md) (rev 3) — read it
first; this README says what the code does, not what the store is for.

**It builds alongside the existing store.** Nothing here reads or writes
`marine_bathymetry_store`'s `draft/`, `processed/`, `reference/` or `chart/`
tree. The rev-3 layout is a different directory tree, not a rename of that one,
and every existing consumer is unaffected.

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

1. `--store-root` (every `mws_*` CLI has it)
2. `$WORLD_STORE_ROOT`
3. `store_root:` in `~/.config/marine_world_store/config.yaml`
4. the documented default

A config file that exists but cannot be read is an **error**, never a silent
fall-through to the default: falling back would write a survey into the wrong
tree because of a stray character, and nothing would say so. Every CLI prints
which of the four decided the root it used.

## Modules

| Module | What it is |
|---|---|
| `store_root.py` | The resolver above, plus `StoreRoot` (path + where it came from) |
| `layout.py` | `<root>/<quantity>/<state>/<origin>/` path builders; `State`/`Origin`/`Quantity` enums, so a typo'd directory name is an error rather than a new folder. `trajectories/` and `observations/` are surveyed-only, as §5 says |
| `source_identity.py` | The Merkle bag id (§3, spine 1): sha256 over the sorted `<split filename>\t<file key>` lines of a bag's `.mcap` files, file key = git-annex `SHA256E`. `metadata.yaml` and every non-data file are excluded, so `ros2 bag reindex` is a repair rather than a new source. No git-annex dependency |
| `fingerprint.py` | §9's input hash. Additive: an input a stage does not have is omitted, never nulled. Id collections are sorted; `consumer_ordering` is not, because its order *is* the input |
| `coverage.py` | Reads `marine_tiled_raster_store`'s `coverage.json` (`coverage-manifest/1`) — the existing convention for per-tile `geometric_error_m` (uma-ADR-0013 D1–D3), not a parallel one. Tolerant, with the same filename-scan fallback |
| `footprint.py` | A tile's geometry and bbox, read from the raster's own georeferencing with GDAL rather than from a second Python implementation of the GGGS grid maths |
| `item_schema.py` | The Item and Collection documents, as plain dicts, with Part 2's consumer-contract fields. No `pystac`, so the schema is testable where the library is not installed |
| `stac_catalog.py` | Validates those documents with `pystac` and writes them — **only the ones that changed** (§9's replica rule), gated on a canonical-JSON content hash, never an mtime |
| `revisions.py` | Append-only `revisions/` records: geometry revisions and datum records. The id is the content hash, so an edited record is detected on read |
| `depth_subset.py` | The native-tile **adapter** (see below) |
| `sigma_fold_measure.py` | The evidence design §7's **open** σ-fold rule is decided from: what each candidate would write, against the true spread of the native cells under a parent. Decides nothing (see below) |
| `fingerprint_sidecar.py` | The regenerate pre-step's `.fp` sidecar — a tile's **content** hash, which is *not* §9's input fingerprint; one answers "did this file change?", the other "was this built from the same things?" |
| `overview_records.py` | Assembles the per-tile records the per-parent overview writer leaves into one `coverage-manifest/1` document (uma-ADR-0013 D3) |

### Field names are namespaced

The consumer contract names its fields `state`, `origin`, `fingerprint`; STAC
requires fields outside common metadata to be prefixed, so they are written as
`mws:state`, `mws:origin`, `mws:fingerprint`, … `item_schema.CONTRACT_FIELDS`
maps one spelling to the other. The prefix is a spelling, not a second
vocabulary.

## CLIs

| Command | Does |
|---|---|
| `mws_import_source PATH` | Compute a source's content id and write its `sources/` Item. `--dry-run` prints the id and writes nothing (and needs no `pystac`) |
| `mws_write_revision DESCRIPTION` | Append one `revisions/` record from a small YAML/JSON description |
| `mws_regenerate_catalog` | Rebuild each present cell's `collection.json`, reporting only what changed |
| `mws_link_depth_subset` | The adapter below |
| `mws_measure_sigma_fold TILE…` | Measure the candidate σ-fold rules over native depth tiles and print a markdown table. Tile paths are **arguments** |
| `mws_refresh_fingerprints LAYER_DIR` | The regenerate pre-step: reconcile `.fp` sidecars with the tiles, resetting an unchanged tile's mtime |
| `mws_assemble_coverage LAYER_DIR` | Write `overviews/coverage.json` from the per-tile overview records, once, after the DAG |

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
levels: [12]
start: '2026-06-22T13:22:29Z'
end: '2026-06-22T15:00:00Z'
```

A tile whose coverage manifest recorded no geometric error gets an Item with
**no** `mws:geometric_error_m` — never a zero, which would claim a perfect tile
to the uma#395 selection core — and the run says how many.

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

`layer_dir` is required and has no default: the store root is resolved by
`store_root.py`, and a path written into the workflow would be the hard-coded
path the guard test forbids (which now scans `Snakefile` too).

- **The pre-step is the load-bearing part.** §9 makes fingerprints, not mtimes,
  the trigger; Snakemake's DAG decides from mtimes. `mws_refresh_fingerprints`
  resets an unchanged tile's mtime to its `.fp` sidecar's, so a rebuild that
  produced the same bytes stops looking like a change — without it, everything
  above a rewritten-but-identical tile re-runs, which is the behaviour the
  prototype found in the batch builder.
- **One `checkpoint` per level.** The parents at level N cannot be enumerated
  until N+1 exists, because a derived tile is itself a contributor; a DAG whose
  shape depends on a previous step's output is what a checkpoint is for. The
  enumeration is `build_depth_overview_parent --list-parents`, never Python in
  the rules: the parent/child mapping is GGGS, whose column counts vary by
  latitude band, and a test asserts it is not reimplemented there.
- **`mws_assemble_coverage` is a single serialised step after every parent.**
  The per-parent writer leaves a per-tile record instead of touching a shared
  `coverage.json`, because parallel folds would race over that one file and the
  only lock that would fix it is one that serialises the DAG.
- **The GTI index is derived and never synced** (§7) — regenerated locally from
  the Collection, which is the record.

Snakemake is not installed on the development host (it resolves through the
repo-root `rosdep.yaml` local key), so the `--dry-run` test skips with that
reason; the remaining rule checks are static and run today.
