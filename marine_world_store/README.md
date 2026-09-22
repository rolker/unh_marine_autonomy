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
| `snakemake` | `snakemake` | The regenerate driver (§9); the rules themselves are Group B of #397 |

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

## Still to come (Group B of #397)

The multi-band (MIN/MEAN/COUNT/σ) overview writer, `build_depth_overviews`
per-parent mode, and the Snakemake regenerate rules under
`marine_world_store/snakemake/`. The hooks they need are already here: an Item
can carry a nested `geometric_error_m` and a `sigma_fold` rule name. **The σ
fold rule is deliberately open** (design §7 as amended) — no σ band is written
until it is decided from Group B's measurement.
