# ADR-0008: Live Sonar Coverage Transport & Render (`SonarVisualizationTile`)

## Status

Proposed (2026-06-27). Tracked by
[rolker/unh_marine_autonomy#230](https://github.com/rolker/unh_marine_autonomy/issues/230),
Part of the sidescan-mosaic umbrella
[#171](https://github.com/rolker/unh_marine_autonomy/issues/171). Realizes the
**I3 / #86 Phase 6** tiled-raster transport and the consumer half of **I4**
([#175](https://github.com/rolker/unh_marine_autonomy/issues/175)).

A **cross-cutting** ADR per the ADR-0001 convention: it spans `marine_interfaces`
(the messages), `cube_bathymetry` (the boat producer,
[#78](https://github.com/rolker/cube_bathymetry/issues/78), follow-on to cube#70),
`camp` (the operator render + cache,
[#121](https://github.com/rolker/camp/issues/121)), and `udp_bridge` (metering,
[#19](https://github.com/rolker/udp_bridge/issues/19)).

Builds on **ADR-0002** (bathy store — GGGS tiling, layer-as-subdirectory, the
Phase-6 sync) and reads from the durable stores defined in **ADR-0006** (sidescan
backscatter) and **ADR-0007** (MBES backscatter). References **ADR-0005**
(cross-store provenance/registry) and **ADR-0001** (shared colormap).

## Context

Operators need to watch survey coverage **accumulate live in CAMP** as the boat
works — depth, uncertainty, and backscatter placed on the map — and the web
viewer (#166) wants the same stream. The boat's CUBE node already produces and
persists GGGS-tiled draft bathymetry (cube#21, and the bounded incremental
publish of cube#70). What is missing is the **boat→operator transport** and the
**operator-side render** of those tiles.

The naive approach — bridging the whole accumulated `grid_map` over the link —
**saturated `udp_bridge`** (#250): the monolithic grid is densely filled over a
large area, where compression does not help, and it grows without bound.

The transport and render were already sketched as design text — "#86 Phase 6
content-hash tile-sync" and the #171 I3/I4 phases — but never specified or filed,
and the sketches disagreed internally (hash vs version vs Int64-ns time). This ADR
specifies them and resolves the inconsistencies, after a four-repo
overlap/divergence review.

The load-bearing distinction: this is a **display transport**, not a store. The
authoritative data of record stays full-precision in the bathy store (ADR-0002)
and the backscatter stores (ADR-0006/0007) on the boat and is reconciled offline.
The transported tiles are a **lossy display projection** that can always be
rebuilt; losing them loses only the live preview.

## Decision

### D1 — `SonarVisualizationTile`: one transport, named self-describing bands

A single message carries any GGGS-tiled sonar **display product**:

- `GridIndex {level, row, col}` + a per-tile **version/timestamp** + the dirty
  **sub-window**.
- A list of **named, self-describing bands**, each `{name, dtype, scale, offset,
  nodata}`. The consumer dequantizes generically (`value = raw·scale + offset`)
  with no hardcoded per-band knowledge.
- v1 bathy bands: `depth` (int16, fixed `scale=0.01 m`, `offset=0`,
  `nodata=−32768`), `uncertainty` (uint8), `backscatter` (uint8). Sidescan later:
  `intensity` (uint8). Per-band metadata is **required, not optional**, because
  sidescan auto-ranges — its scale must travel with each tile.

One transport serves bathy (from cube, L10) **and** sidescan (#173, L13); the
level travels in `GridIndex`, so producers/levels/bands vary freely. This is the
unified transport #171 calls for.

`SonarVisualizationTile` is the **display projection** of the durable stores
(ADR-0002 bathy, ADR-0006 sidescan, ADR-0007 MBES backscatter) — never a competing
schema. It carries no provenance for arbitration; it is for looking at, not
processing.

`TileCatalog` (periodic **complete** snapshot of `{GridIndex, version}` + a
catalog generation-time) and `TileRequest` (operator's "need" list of
`{GridIndex}`) accompany it.

### D2 — Encoding: dense quantized, lean on `udp_bridge` zlib (no custom codec)

The message does **not** compress itself. `udp_bridge` already zlibs the wire,
and a mostly-empty dense tile zlibs ~310× (a real 0.34%-full tile: 14.4 MB → 49 KB
for float64 depth+uncertainty). A hand-tuned sparse+delta encoding beat dense
quantized by only ~1.4× on the wire — not worth sparse cell-lists / varints /
change-tracking, and the sparse advantage inverts once a tile fills.

The real lever is **quantization** (float64 → int16 cm depth, uint8 uncertainty /
backscatter): ~5× on the wire **and** ~5× on the raw/serialization footprint
(14.4 MB → 2.7 MB), the latter mattering because the uncompressed message hits DDS
at full size before zlib. So: **dense quantized raster, quantize at the source,
let `udp_bridge` zlib it.**

### D3 — Change-detection key: timestamp/version (not content-hash)

The manifest keys on a **monotonic per-tile version** (a counter or the tile's
latest-cell timestamp), not a content hash. This resolves the prior inconsistency
(ADR-0002/#86 "version", #171 "content-hash", #180 "Int64-ns time") toward
timestamp: the push path needs ordering for newest-wins regardless (a hash cannot
order two versions), the cells already carry timestamps, the boat already knows
its changed tiles from its dirty set, and it is clock-skew-safe because all
versions compared come from the same source (the boat). ADR-0002's "content-hash
sync" wording and #171's prose are reconciled to this.

### D4 — Anti-entropy reconciliation (push + full catalog, converge-to-catalog)

- **Live push** of dirty tiles (low latency) + **periodic full `TileCatalog`**.
- The operator converges its cache to **exactly the catalog's set**: request tiles
  it is missing or stale on; **prune-on-absence** tiles not in the catalog. This
  recovers from link outages, cold starts, lost packets, **and a boat-side reset**
  (a fresh boat advertises a small catalog → the operator prunes the rest) with
  one mechanism — no generation id needed.
- Two correctness conditions: (a) the catalog is a **complete** snapshot (prune is
  invalid against a partial/paged catalog); (b) prune is **timestamp-gated** — an
  absent tile is pruned only if it is older than the catalog's generation-time, so
  a late/reordered catalog cannot delete a just-pushed fresh tile.

### D5 — Operator cache: CAMP-managed, disk-backed, render-from-memory

CAMP owns the cache directly — **no separate receiver process**. The catalog
reconcile already covers CAMP's own downtime, so an always-on receiver buys
nothing. CAMP renders from an **in-memory** GGGS layer and **writes through to
disk** (atomic temp+rename, off the GUI thread), **warm-loading** the cache on
restart. Rendering from memory dissolves the cross-process read/write race; the
atomic write is for crash-safety only. The cache is a **display-grade preview**,
not data of record.

### D6 — Unified, source-agnostic, band/colormap render abstraction

Put tiling **below** a `RasterFieldSource` interface (`bands()`,
`metadata(band)`, `read(band, extent, lod)`) and band-select + colormap **above**
it, shared. Three peer sources: the existing GGGS **file store** (upgraded from
single-band grayscale — superseding camp#108), the **live cache** (a distinct
live/preview source, mechanically identical, kept out of the durable store
catalog), and **non-tiled rasters**. One display layer picks the band
(depth/uncertainty/backscatter) and associates a colormap via **`marine_colormap`**
(ADR-0001; camp#63's pre-library "camp-internal ColorMap" wording is reconciled to
this, per #175). A full-precision file tile is identity `scale=1/offset=0`
metadata, so the same path spans live-quantized and file-full-precision with no
fork. Tiling/LOD (#103), GPU warp (#175), and compositing (#109) live below the
interface.

### D7 — GGGS levels

Single-level in v1 (`gggs::Level::fromCellSize`), but level-agnostic plumbing — the
`GridIndex` carries the level, so the consumer renders each tile at its own level
and pyramids (#188) drop in later. No spec-version guard: trust the shared global
GGGS spec; revisit only if GGGS itself changes (the guard then rides that change).

### D8 — No `source_index` in v1

Source identity lives at **topic/stream granularity** for the display path; the
band name encodes the product. Multi-platform fusion with per-cell `source-id` +
registry (ADR-0005 / #179) is a separate, later central-server sync tier, not this
single-boat display transport.

### D9 — Metering

Bulk catch-up must never starve operations. Rides `udp_bridge#19`'s per-topic
priority classes: **nav SA > live tile push > bulk catch-up**. The operator may
order its `TileRequest` (on-screen / nearest-the-boat first) for relevance.

### D10 — v1 backscatter = M3-co-estimated (ahead of #180's roadmap)

v1 carries **M3 CUBE-co-estimated backscatter** (ADR-0007's MBES store is its
durable home), riding the bathy producer at L10 — ahead of #180's
sidescan-first/M3-later sequencing, but cheap (the co-estimation, cube#54, is
done) and orthogonal (different sensor and level than #180's sidescan @ L13).
Requires the producer (cube#78) to add a `beam_angle` field to the soundings cloud
(`detections_to_pointcloud`) and read `{intensity, beam_angle}` in `pingCallback`
— completing the ADR-0007 D3 sufficient-statistic pair so backscatter is
angle-aware, not a nadir hot-stripe.

## Consequences

**Positive**

- One transport + one render path for all live sonar coverage (bathy now,
  sidescan via #173 later); the live view falls out as just-another-source.
- Bounded, link-friendly payloads; the #250 saturation is fixed at the source
  (per-dirty-tile + quantization, not a whole-grid bridge).
- Durable across CAMP restarts without a separate process; self-healing under loss
  / outage / reset via one reconciliation invariant.
- Supersedes the atomic-write stopgap #189 (this transport is the durable fix) and
  the per-band-select request camp#108.

**Negative / costs**

- New ROS-topic ingestion in CAMP (previously file-store only) and a new GPU
  render path (#175); coordinate `.msg` additions against in-flight
  `marine_interfaces` churn (#158/#162/#167).
- Full `TileCatalog` is ~20 B/tile × N — tens of KB on a large survey; send
  periodically and paced.
- v1 M3 backscatter precedes the durable MBES backscatter store (ADR-0007)
  landing; the live band is display-grade until then.

**Follow-ups**

- Pyramids/LOD (#188, #103); the multi-platform fusion sync tier (#179) carrying
  `source-id` + registry; retiring cube's `~/tiles` full-precision GridMap if it
  never gains a local consumer; revisiting `clear_grid` semantics.

## Alternatives considered

- **Content-hash manifest** — rejected for the change-detection key (D3): no
  ordering for the push path, extra hashing, and the boat's dirty set already
  knows what changed. (Its only edge — exact content-match and ETag-style web
  caching — does not outweigh needing a timestamp anyway.)
- **Sparse / custom-compressed tiles** — rejected (D2): redundant with
  `udp_bridge`'s zlib; quantization captures the win without the complexity.
- **Separate always-on receiver process** on the operator side — rejected (D5):
  the catalog reconcile covers CAMP downtime, so it adds a moving part for no gain.
- **Memory-only operator cache** — rejected in favor of disk-backed (D5): a
  memory-only CAMP re-pulls the whole day over the link on every restart,
  competing with live nav SA on a constrained link.
- **Reserving a `source-id` field now** (ADR-0005 insurance) — deferred (D8): the
  display transport is single-source; the extensible message adds it cleanly when
  multi-platform display is real.

## References

- Transport umbrella: [rolker/unh_marine_autonomy#230](https://github.com/rolker/unh_marine_autonomy/issues/230)
- Producer: [rolker/cube_bathymetry#78](https://github.com/rolker/cube_bathymetry/issues/78)
- Operator (CAMP) consumer, disk-backed per D5: [rolker/camp#121](https://github.com/rolker/camp/issues/121)
- **Second consumer — `marine_web_view/coverage_renderer`**
  ([rolker/unh_marine_autonomy#345](https://github.com/rolker/unh_marine_autonomy/issues/345)):
  a Python reconciler over the same catalog/request/tile triple, rendering to
  static web-map PNGs. It **departs from D5's disk-backed cache**: its durable
  output is the bucket it publishes into, so a restart re-requests from the
  catalog and every already-published tile is still standing meanwhile — the
  re-pull-over-a-constrained-link cost D5 was avoiding does not apply to a
  shore-side renderer on a wired uplink. Recorded here so the departure is
  discoverable from this ADR; the reasoning and its one known cost (coverage
  pruned across a restart is never un-published) are in
  `marine_web_view/README.md` under "Memory-only, by design".
