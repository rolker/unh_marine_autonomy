# ADR-0005: Cross-Store Multi-Platform Provenance & Source Registry

## Status

Proposed (2026-06-20). Tracked by
[rolker/unh_marine_autonomy#179](https://github.com/rolker/unh_marine_autonomy/issues/179).

This is a **cross-cutting** decision in the sense ADR-0001 establishes for this
repo's `docs/decisions/`: it spans the bathymetric store (ADR-0002 / #86) and the
backscatter stores — **sidescan** (ADR-0006) and **MBES** (ADR-0007), both under
#180 — and defines the provenance contract all three share. It is foundational to
those store ADRs rather than owned by any one of them.

## Context

Depth and backscatter data will increasingly come from **more than one platform
and sensor**, and be fused on a shared/central server. A representative near-term
case: a joint operation where a **DriX** carrying a **Kongsberg EM2040** MBES
contributes bathymetry *and* backscatter, while **BizzyBoat** contributes
bathymetry from its **Norbit M3** and side-scan imagery from its **Garmin GCV**.
We want one "best bottom" answer per location — and the ability to ask "show me
only the DriX EM2040 backscatter," or "what did BizzyBoat see here last week."

The spatial side of fusion is already solved: **GGGS** keys every contribution by
a global `GridIndex`, so two platforms covering the same patch simply write the
same tile. What is missing is **provenance** — a minimal, consistent way to tag
each contribution by platform, sensor, and modality, and to arbitrate between
contributions, **without** bloating the per-cell record and **without** each store
inventing its own scheme.

ADR-0002's source-layer concept (`processed` / `draft` / `chart`) is a
**quality/maturity** axis only — it is not platform- or sensor-aware. Multi-platform
fusion adds axes ADR-0002 did not need to name. Naming them once, here, keeps the
bathy and backscatter stores consistent and avoids a painful migration when the
second platform arrives.

## Decision

### D1 — Three orthogonal axes, each resolved at the right layer

Provenance is not one tag; it is three independent axes, and each belongs at a
different layer of the design:

1. **Modality → which store.** Bathymetry and backscatter are different value
   types, dtypes, and consumers, so they are **separate stores**. DriX EM2040
   bathy and BizzyBoat M3 bathy both land in the bathy store. Backscatter is a
   **family of sibling stores keyed by `sensor_class` ingest architecture** — a
   **sidescan** store (two-tier, slant-archived: Garmin GCV; ADR-0006) and an
   **MBES-backscatter** store (single-tier, co-estimated with bathy by the CUBE
   pass: M3 / EM2040; ADR-0007) — because those ingest paths differ fundamentally
   (sidescan has no bathy of its own and must archive + project later; MBES
   backscatter is a co-registered by-product of the depth solution). The sibling
   stores share this registry and the GGGS GridIndex, so a unified
   "best backscatter here" surface is composed **across** them at the query /
   central-server layer (D5/D7), not inside one monolithic store. *(Updated
   2026-06-20, [#190](https://github.com/rolker/unh_marine_autonomy/issues/190);
   the original wording put EM2040 and Garmin in a single backscatter store.)*
2. **Quality / maturity → layer subdirectory** (`processed` / `draft`), exactly
   as ADR-0002 D3/D5. Unchanged.
3. **Platform / sensor / campaign → a compact per-cell `source-id` plus a
   registry sidecar** (D2). This is the new axis this ADR introduces.

Keeping these orthogonal is what keeps the metadata minimal: no axis is encoded
redundantly, and each store reuses the same mechanism for axis 3.

### D2 — Compact per-cell source index, rich metadata in a registry sidecar

This ADR **defines** two linked identifiers, kept separate so the per-cell cost
stays tiny while the global identity stays wide:

- a **`source-id`** — the globally-unique, origin-namespaced identity of a
  contribution (D4); the cross-store / sync key.
- a compact **per-cell source index** — a small interning handle actually written
  into each tile cell. A **registry** (a JSON sidecar at the store root,
  `registry.json`) maps `local source index → { source-id, metadata record }` (D3).

A store that already has a per-sample provenance field (e.g. the GeoCoder "source"
field in the backscatter store, ADR-0006) populates it with the **local source
index**, not a local acquisition-line number.

Per-cell cost is therefore **one small integer**; the wide `source-id` and all rich
metadata live **once** in the registry, not replicated across millions of cells.
This is the deliberate "don't go crazy with metadata" choice: enough to sort and
arbitrate by platform, sensor, and modality, with no per-cell bloat.

**Persistence — this amends ADR-0002 D5.** ADR-0002 D5 stores a tile with *no*
per-cell source band, reasoning that "a tile is single-layer by construction, so a
per-cell source band would be a constant." **Multi-platform fusion breaks that
premise:** different platforms contribute different cells of the *same* GGGS tile,
so `source-id` is non-constant within a tile and a per-cell `source-id` channel is
required. The **local source index** (not the wide `source-id`) is persisted as a
per-cell band alongside the value bands, so it fits a narrow type: **natively
`uint16`** in the backscatter tile (ADR-0006 D7), and for the bathy store a
small-int band — exactly representable as `Float64` (ADR-0002's tile dtype) because
the local index is small, or a parallel small-int tile (left to the bathy
migration, #178). The wide 64-bit `source-id` from D4 is **not** exactly
representable in `Float64` and does **not** go in the cell — only the local index
does; the registry resolves index → `source-id`. The per-cell index **is part of
the ADR-0002 D6 content-hash payload**: a re-arbitration that changes a cell's
winning source flips the tile hash and re-syncs — the accepted cost of correct
provenance.

### D3 — Registry record schema: core + store-specific

Each `source-id` resolves to a record. The schema is a small **universal core**
plus **store-specific extensions**, so entries are not padded with fields they do
not need.

**Core (every store):**

| Field | Purpose / example |
|-------|-------------------|
| `platform` | `bizzyboat`, `drix-12` |
| `sensor` | model — `garmin-gcv20`, `kongsberg-em2040`, `norbit-m3` |
| `sensor_class` | the **arbitration class** D5 priority keys on — `sidescan`, `mbes-backscatter`, `mbes-bathy`. Not the store identity (that is implicit). |
| `campaign` | survey / deployment id + UTC time range |

**Store-specific extensions:**

| Field | Store | Purpose / example |
|-------|-------|-------------------|
| `datum` | bathy | the **original** datum the data was converted *from* — provenance, not a read-time instruction (see below) |
| `calibration_ref` | both | optional, **forward-looking** — pointer to the per-hardware calibration applied (beam-pattern table version, gain model, mounting offsets; ADR-0006). Empty until those artifacts exist. |

`modality` is deliberately **not** a field: the store *is* the modality (D1) and
the registry is a per-store sidecar, so it would be redundant.

**`datum` is converted-from provenance, never a read-time conversion.** Per
ADR-0002 D4 the store holds a single vertical reference — WGS84 ellipsoidal — and
all datum conversion happens at the **import boundary**. Stored values are
therefore *always already WGS84*; `datum` records only what the source was
originally in. It enables audit and **re-conversion from source** if a transform
is later corrected (e.g. a VDatum revision): by the same rule as a nav/mounting
change (ADR-0006), you re-import from the original source — you never re-convert
the stored WGS84 in place. (Re-import-from-source presumes the original source is
retained; source retention is a producer/operational responsibility, out of scope
here.)

The schema is **additive**: new fields can be introduced without touching the
per-cell tiles. Records are immutable once a `source-id` is allocated.

### D4 — `source-id` allocation is collision-safe by construction

The wide **`source-id`** (the registry / global identity, D2) must be **globally
unique and never reused**, and stay so even though D7 lets robot, operator, and
central server each hold a registry and sync later. A flat per-registry counter
would let two partitioned nodes assign the same id to *different* `(platform,
sensor, campaign)` records.

So `source-id` is **namespaced by origin**: the high bits identify the allocating
node/origin, the low bits a local monotonic sequence (e.g. a 64-bit id =
`origin_id << 40 | local_seq`). Each node allocates only in its own namespace, so
two source-ids never collide across a partition and merging registries (D7) is
append-only. **`source-id` 0 is reserved for no-data / unset.** This is the
never-renumbered discipline ADR-0001's palette registry already uses — a stable key
mapped to a value, never reissued.

The compact **per-cell source index** (D2) is a per-store interning of the
source-ids present in that store's tiles; the registry resolves index → source-id.
Keeping local indices consistent across synced stores is part of registry
reconciliation (Consequences, deferred); the *global* `source-id` is always the
authoritative identity.

**v1 is collision-safe trivially** — a single allocator (BizzyBoat ingest) in one
namespace — but the namespacing is fixed **now**, as the cheap insurance:
introducing it later, after ids are already in tiles, *is* the painful migration.
A local acquisition-line id (the GeoCoder default) is exactly what this prevents.

### D5 — Arbitration is query-mode-specific; priority-first is the in-mode rule

There is **no single global rank**. The store exposes the provenance axes —
maturity layer (`processed`/`draft`/`chart`), `sensor_class` priority, per-cell
**quality**, **recency** (timestamp), and `source-id` — and each **query mode**
composes them for its use. This extends ADR-0002, which already distinguishes a
default query from a shallowest-reliable safety query. The canonical modes:

- **Live / working view** (operator display; *and* a producer that reads an
  existing layer as a prior then writes a newer result that subsumes it — e.g. CUBE
  ingesting with the `processed` grid as a prior): **recency-first — the newest
  contribution wins.** The operator always sees the latest, and a fresh result that
  already incorporated an older prior supersedes it. This is the `draft`-layer
  newest-valid-wins behavior.
- **Curated / best-available** (the durable `processed` product): a **deliberate
  composition decided at post-processing time.** *Within* a contribution set,
  arbitration is **priority-first** — `sensor_class` is the primary key (calibrated
  `mbes-backscatter` outranks `sidescan`), and the store's quality measure
  (grazing-angle for backscatter, uncertainty for bathy) breaks ties *within* a
  class. How the maturity layer and recency wrap around that is the curated
  product's choice and is **deferred** (Consequences).
- **Navigation-safety** (bathy): the carve-out below — priority-agnostic.

`priority-first` (`sensor_class` → quality) is thus the rule *within* a mode's
contribution set; it **adds** an N-source priority *within* each maturity layer
rather than replacing ADR-0002 D3's `processed`/`draft`/`chart` layering, which
remains the orthogonal maturity axis (D1, D8). All modes are non-destructive
**overlays** — a losing source is never erased. Queries may additionally **filter**
by any registry field (platform, sensor, `sensor_class`, campaign, time window).

**Safety carve-out — priority does not govern the navigation-safety query.**
The mode-specific arbitration above governs the **live and curated/visualization**
query modes only. The bathy store's **shallowest-reliable-depth** mode (ADR-0002
D3/D7), consumed by the Nav2 collision costmap, is a distinct mode **never
overridden by registry priority or recency**: it returns
the shallowest *reliable* depth across **all** qualifying sources, retaining
ADR-0002 D7's conservative no-data / stale / over-uncertainty policy (unknown
treated as not-safe). A higher-priority but deeper source must not hide a
lower-priority but shallower reliable one. Per **Simulation-First**, any change to
costmap arbitration is validated in `unh_marine_simulation` before field use.

### D6 — Provenance is per-contribution; re-arbitration cost depends on the store

Every tile cell records the **winning** `source-id`. Whether a re-arbitration (new
platform data, or a priority change) can be done **without re-reading the source
bags** depends on what archive the store keeps:

- The **sidescan backscatter store keeps a slant-indexed Tier-1 per-ping archive**
  (ADR-0006), so it re-projects from the retained contributions and re-arbitrates
  without re-reading bags.
- The **MBES backscatter store is single-tier** (ADR-0007): it keeps no separate
  per-contribution archive because the **soundings bags are its archive**, so a
  re-arbitration / bathy-refine re-runs the CUBE + node-output pass over the bags
  (the same offline path the bathy store uses).
- The **bathy store likewise has no in-store archive** (ADR-0002's draft layer is
  in-memory CUBE tiles arbitrated in place), so re-arbitration there means
  re-importing the affected sources from their bags.

The provenance contract (per-cell `source-id` + registry) is identical for all
three; only the *no-bag-reread guarantee* is specific to a store that keeps an
in-store contribution archive (the sidescan Tier-1).

### D7 — The central server is a third sync tier

ADR-0002 D6 defines change-only tile sync (robot → operator) keyed by a
`{GridIndex → content-hash}` manifest. This ADR extends the **same** mechanism to
**robot/operator → central server**: each node pushes changed tiles, and the
**registry rides along**. Registry merging is **append-only because of the D4
origin-namespaced ids** — each node only ever adds records in its own namespace, so
two registries never disagree about what a `source-id` means (without D4's
invariant the merge would not be conflict-free). The server merges tiles by
`GridIndex` and arbitrates per D5. No new distribution mechanism is invented; the
registry is the only addition to the existing manifest sync.

### D8 — Both stores adopt this; ADR-0002 gains it additively

Both backscatter stores (ADR-0006 sidescan, ADR-0007 MBES) are designed against
this from the start. The bathy store (ADR-0002) **adopts it additively**: its existing `processed` /
`draft` / `chart` remain the **quality/maturity layer** axis, while
platform/sensor moves to `source-id` + registry. The additive change for bathy is
the per-cell `source-id` band (D2, amending ADR-0002 D5) plus the registry;
existing single-platform data maps to a **default registry entry**, so the
migration is non-destructive (ADR-0002's per-cell record carries no platform field,
so nothing is lost). The **safety carve-out (D5)** is the load-bearing constraint
for bathy adoption: registry priority must not override the shallowest-reliable
navigation query.

## Consequences

- **Positive:** minimal per-cell cost (one int); one provenance contract shared by
  both stores; multi-platform fusion, filtering, and priority with no per-cell
  schema churn; central-server-ready by reusing the existing tile-manifest sync;
  cheap v1 insurance (global `source-id` now) that averts a later migration.
- **Cost:** a registry artifact to maintain, version, and sync; an arbitration
  contract (D5) consumers pin to; ADR-0002 acquires a compatible additive
  provenance dimension and a default-registry migration step.
- **Risk if ignored:** encoding `source` as a local acquisition-line id (the
  GeoCoder default) forces a store-wide migration when the second platform/sensor
  arrives — exactly the outcome D4 exists to prevent.
- **Deferred (not decided here):** the concrete **numeric** priority table across
  `sensor_class` values (priority-first as the *in-mode* rule is decided; only the
  per-class ranking is staged); the **curated-product cross-layer / recency
  composition** (D5 — the `processed` mode's exact maturity-vs-recency policy is a
  post-processing decision); the central-server **namespace-registrar authority**
  (D4 fixes the id *scheme* — partitioned allocation is collision-safe by
  construction — but who issues origin namespaces server-side, and how per-store
  **local indices** stay consistent across synced stores, is deferred); and the
  reconciliation policy for divergent registry records. Staged with multi-platform
  ingest, after single-platform (Garmin) ingest is real.

## References

- Bathymetric store: ADR-0002 / [#86](https://github.com/rolker/unh_marine_autonomy/issues/86)
- Backscatter stores: ADR-0006 sidescan, ADR-0007 MBES (both proposed) / [#180](https://github.com/rolker/unh_marine_autonomy/issues/180); ADR split [#190](https://github.com/rolker/unh_marine_autonomy/issues/190)
- Sidescan mosaic umbrella: [#171](https://github.com/rolker/unh_marine_autonomy/issues/171)
- Bathy time-band Int64 alignment: [#178](https://github.com/rolker/unh_marine_autonomy/issues/178)
- Spatial index: `marine_autonomy` GGGS (`marine_autonomy/include/marine_autonomy/gggs/`)
- Cross-cutting `docs/decisions/` convention: ADR-0001
