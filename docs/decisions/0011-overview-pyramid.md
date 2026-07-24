# ADR-0011: Overview Pyramids — Sidecar Layout and Fold-Policy Contract

## Status

Accepted (2026-07-24). Tracked by
[rolker/unh_marine_autonomy#188](https://github.com/rolker/unh_marine_autonomy/issues/188).

Implements the overview half of [ADR-0010](0010-geospatial-world-model.md) D9
(per-layer LOD) and **extends it to the imagery theme**: D9's overview clause
names the depth layers; imagery keeps its own tiering per D3, and this ADR
gives its survey-born layers the same derived-overview mechanism. Referenced
by header pointers from [ADR-0002](0002-bathymetric-data-store.md) and
[ADR-0006](0006-multi-platform-backscatter-store.md) (their layers gain a derived
sidecar, no change to their fine-tile formats).

## Context

Survey-born store layers are single-level (sidescan at L13, bathymetry at
L10). Zoomed out, a consumer must open every fine tile there is — the
1000-tile sidescan store costs a 3.6 GB eager read to display at any scale,
and survey data cannot participate in coarse-level queries (ADR-0010 D9's
voyage-planner case) at all. The chart layer does not share this problem: its
ENC scale ladder is a native, curated, shoal-biased pyramid.

GDAL's internal GeoTIFF overviews (`BuildOverviews`) were considered and
rejected: they are per-file, so a zoomed-out render still opens every fine
file; and one resampling algorithm applies to all bands of a dataset, which
cannot express per-band policies (depth mean-vs-shoalest, uncertainty
pairing).

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
   data can never be confused with derived overviews.

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
     source; provenance readers must use fine tiles.
   - **Depths: SHALLOWEST-PRESERVING, never mean** (ADR-0010 D9): the coarse
     cell carries its shoalest-reliable child's whole {depth, σ} pair, kept
     coherent. A mean would let a coarse corridor query plan over a rock.
     Safe by construction for every consumer, so depth overviews may feed the
     level-aware query; the conservative (shoal-exaggerating) world-zoom look
     is accepted — cartographic generalization does the same. **Reserved, not
     implemented here** — the depths pyramid follows the ADR-0010 D8
     re-split.

5. **Batch first, incremental later.** `build_sidescan_overviews` regenerates
   the sidescan sidecar as an offline batch step after ingest. The live
   coverage cache (camp ADR-0006/0010) later adopts the same fold engine
   incrementally, replacing its full-size `foldIntoParent` overviews — the
   root fix for camp#171 (fold frees no memory) — but that is CAMP-side work
   (step 4 of the sequence), out of scope here.

## Consequences

- CAMP's LOD renderer (step 3) reads overview levels by view scale from the
  pinned sidecar path; store opens stop scaling with survey size.
- Survey depth data becomes eligible for coarse-level queries once the depth
  policy lands (after the D8 re-split).
- Every ingest should be followed by an overview rebuild (cheap relative to
  ingest); a stale sidecar renders stale coarse imagery but can never corrupt
  fine data.
- Overviews add ~1/3 of a layer's fine-tile volume (geometric series).
