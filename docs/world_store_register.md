# The World Store — Appendix A: decision register, safety review, open questions (as of rev 2)

Working material moved out of the design draft at rev 3 (2026-09-21). The register's first pass is complete; verdicts stand unless rev 3's model says otherwise (R11 amended by spine decision 2; R17/Q10 answered by spine decision 5; Q1/Q9 superseded by spine decision 3's names). Kept for the record of *why*; not the design.

## Decision re-examination register *(first pass complete 2026-09-16; R13 deferred to the level thread)*

Roland, 2026-09-16: the decisions made during the deployment months — including the ones
that were later reversed — "could probably all use a new look in case time crunches made
us overlook important details." Each row gets a fresh read in the off-season: what was
decided, under what pressure, what has changed since, and a verdict. A verdict of *stands*
means re-examined and kept, not "never looked at". Rows are worked in conversation, a few
at a time; the register is the record.

| # | Decision | When / where | Pressure at the time | Changed since | Verdict |
|---|---|---|---|---|---|
| R1 | One GGGS-tiled store, per-cell {depth, σ}, layer priority; no PostGIS | ADR-0002 D1–D3, 2026-06-10 | Massabesic deployment #250 (June 10–11) | mixed levels, five categories | **stands** (09-16): GGGS defensible; tile contents to be revised per user needs → tile-contents thread |
| R2 | All heights ellipsoidal, datum conversion at import; `map_tide` only runtime vertical reference | ADR-0002 D4 / ADR-0010 D5 | GRANIT layer in the wrong datum (06-15) | geoid round-trip error 0.626 m found 08-21; frame realization never named (MaCORS = NAD83(2011)) | **stands** (09-16): stores stay WGS84, no datum labels on tiles; RTK-frame → WGS84 at the source (`mru_transform`, with transformation support data under `sources/datum/`); old recordings via correction records + regeneration — see the reference frame thread (NAD83(2011) vs WGS84 = +1.19 m in h here) |
| R3 | Per-tile GeoTIFFs, later 3-file split, later collapsed to value tile only | ADR-0002 D5, #178, #248 | tile-sync design, then #96 greenfield | — | not yet |
| R4 | Change key = version/timestamp, not content hash | ADR-0008 D3 (06-27) vs ADR-0002 D6 | live transport build for the last Massabesic days | never reconciled into 0002 | not yet |
| R5 | Per-day epochs → one fused grid per layer | uma#221, 06-25 | first M3 ingest blocked on it | — | **stands** (09-16) for the store; trajectories are NOT split by the store — user's choice, lookup by interval |
| R6 | Unified backscatter store → two sibling stores | uma#190, 06-21 | sidescan driver in progress | both now have an observations stage; backscatter is a product | **stands** (09-16): separate products (collection differs enough); any combination is a DERIVED third product (merged mosaic, seafloor classification) |
| R7 | `--append` → greenfield regeneration, stores are a regenerable cache | cube#96, 06-30 → 07-01 | 07-02 authoritative rebuild deadline | regenerate is not operational (no ledger, script broken) | **stands** (09-16): full regeneration must be possible when all bags + the corrections layer exist; **add a data-cleaning layer** (manual, automatic or both — decide later) |
| R8 | Per-cell source and time rasters dropped; σ carries quality | uma#248, 07-01 | same | multi-sensor fusion callback (cube#120); blunder gate cannot flag | **partly** (09-16): source/time stay out; uncertainty may be required for every data type (to debate); metadata-per-tile and time are two separate threads |
| R9 | Single `survey/` → `draft/` + `processed/` re-split | uma#308, 08-20 | pre-Shoals | backscatter never followed | **stands** (09-16): keep a live rung separate from processed; backscatter adopts it |
| R10 | Chart layer regenerated wholesale from the corpus, never merged; footprint clipping withdrawn | ADR-0010 D7, uma#337 | Shoals ENC-first prior, 08-20 → 22 | — | not yet |
| R11 | Pyramids = cross-tile parent tiles in a sidecar; depth fold shallowest-preserving, imagery mean | uma#188 / ADR-0011, 07-24 | between deployments | staleness (#389); no coarse-level query exists | **amended** (09-16): depth overviews fold REPRESENTATIVE (mean/median), not shoalest; a conservative/shoalest overview for a voyage planner stays possible, mechanism deferred until a planner needs it |
| R12 | Native wins on disk; derived overviews fill gaps only | uma#331, 08-21 | chart layer blank past level 5 on dev | display LOD partly solved (ADR-0013, camp#194/195); shared LOD library (ADR-0013 D7, mpt#36) is display-side | **stands for storage** (09-16); two scenarios still to be listed separately: live (`draft`, boat cache) vs processed/other rungs |
| R13 | Depth-adaptive levels: 0.05·depth, no floor, clamp [8, 14], shallowest depth per tile | ADR-0010 D9 #369, 09-09 | Shoals data in hand | writer as built differs (parents alive, achieved level, k = 0.71); uma#386 floor unmerged; cube#161 achieved 10–11 at 1400 s/m² | **deferred to a dedicated look** (09-16): compare with the chart ladder and with data that does not go through CUBE (S-102, reference grids, sidescan) — see the level thread |
| R14 | Capture distance floor 0.5 m removed; k = 0.71 | cube#143, 09-14 | — | — | **out of scope** (09-16): a CUBE processing question, not a store decision; lives with cube |
| R15 | Sidescan tier 1 bakes the full pose; nav/mounting change = reimport | ADR-0006 D2, 06-20 | driver + mosaic for the last Massabesic days | reversed by this draft (deferred pose) | **reversal stands** (09-16): trajectories take the pose half of tier 1; the samples half is the sidescan observations record; tier 1 dissolves |
| R16 | Sidescan fixed at L13 ("proposed position") | ADR-0006 D10 | same | mixed levels everywhere else | **withdrawn** (09-16): sidescan's level is determined by its data properties like every other quantity |
| R17 | Costmap: worst-case clearance = depth − σ; keepout only on trusted data < 0.4 m; chart never keepout | 06-25 discussion | Massabesic fences from the interpolated prior | `unsurveyed_is_lethal` doubles as a shoreline proxy | **stands** (09-16); the store must hold a real **shoreline/coastline** — representation to be designed (features theme? vector from chart land + curation polygons?) → open Q10. The Massabesic whole-survey sim dry-run to settle costmap details is still wished for |
| R18 | Costmap combine is raise-only | uma#296, 08-07 | — | uma#296 fix proposed (trusted samples only) | **usage question, not a store decision** (09-16): the store must supply the data + metadata (incl. uncertainties) the combine needs; the fix lives in `bathymetry_layer` (uma#296) |
| R19 | Safety queries never consult LOD; shoalest-reliable reads every rung and level | ADR-0013 D8, 08-21 | uma#376/#371 residency + fan-out are the price | **stands, refined** (09-16): safety queries never use *generic* (derived, folded) LOD layers; a NATIVE parent tile CUBE produced alongside its children is a real estimate and may be consulted |
| R20 | Live node writes only `draft`; chart prior primes the predicted surface only | cube#89, 06-29 | — | cube#160 depth-belief precedence proposed | **stands** (09-16): live and prior (chart/reference) rungs stay separate |
| R21 | Host roles: gabby live, salmon durable + curated, dev prototypes | 06-20 discussion | — | cadence reversed by the 09-16 field observation | **amended** (09-16): the design speaks of ROLES (live producer, archive, curation, replica), never actual hosts except as examples; capability table, not a schedule |
| R22 | CAMP composites with the selection as a ceiling rather than store-side pyramids for chart/reference | camp#194, 08-21 | Shoals prep | mpt#43 same bug in the explorer | **stands** (09-16): the stores are one component, the shared libraries designed for them are the other; one-off consumer implementations are acceptable as prototypes for eventual libraries |
| R23 | Explorer indexes ping geometry, not store acceptance; single pass = unit of sidescan interpretation | uma#258, 07-13 | ball-turret search | explorer evolved at deployment pace | **stands** (09-16); direction: as the stores mature, converge on an explorer that shows each store's `processed` rung WITH its source data, to see the big picture and find where processing needs improvement |
| R24 | S-102 import operator-run only; no deployed `chart/` until uma#276 | READMEs | — | uma#276 is CLOSED (stale in two READMEs) | **retire the #276 clause** (09-16); who may write `published` moves to the ladder's write gates; a strategy for gradual S-100 adoption is owed — the stores must accommodate S-100 products (Q11) |
| R25 | `~/data/world` = one collection by source class, never per campaign | 08-25 | Shoals import | — | stands (restated 09-16) |

## Owed review of safety-motivated decisions *(open)*

Roland, 2026-09-16: agents made safety a top requirement throughout this evolution and it
was not questioned much; the platforms are mapping vessels, and each such decision should
be weighed against its product-quality cost. Candidates, each to be stated with that cost:

- ADR-0013 D8: safety queries never consult an LOD level.
- ADR-0010 D9: depth pyramids fold shallowest-preserving, never mean.
- ADR-0002 D7: costmap treats no-data, stale, or over-σ as obstacle.
- ADR-0005 D5: the navigation-safety carve-out is priority-agnostic.
- #248: per-cell source and time dropped as not safety-relevant.
- The `reference` blunder gate (echoboats#488): false deeps survive because deep reads
  as safe.

## Open questions

| # | Question | Owner |
|---|---|---|
| 1 | Rung names | **decided**: `published | reference | draft | processed`; order is a default (2026-09-17) |
| 2 | Correction-record schema and canonical home | agent proposes, Roland decides |
| 3 | Trajectory product format and the day/mission unit | agent proposes |
| 4 | Backscatter | **decided**: a PRODUCT — full per-quantity treatment (#390 pyramid, #383 mixed levels, draft/processed rungs, cross-dataset work) |
| 5 | Tile contents per user; is uncertainty required for every data type? | tile-contents thread |
| 6 | Copy of record and replica rule | **proposed** (2026-09-17): input-set superset supersedes; incomparable → both kept, curated default — Roland to confirm; on-platform automatic `processed`, sync direction and trigger still open |
| 7 | `water/` theme | **decided**: in the model now as a named quantity with the same ladder and stage rules; implementation stays under uma#300 |
| 8 | Fingerprint: one schema or shared core? | agent proposes |
| 9 | Category name | **decided**: `curation/` (corrections + cleaning marks); datum polygons stay under `sources/datum/user/` |
| 10 | Shoreline representation: water-level-qualified products under `features/`; sourced from charts/reference now, our own `processed` shoreline admitted later | agent proposes (rev 2 sketch above), Roland decides |
| 11 | Gradual S-100 adoption strategy (S-102 now, S-101 features later); what the stores must accommodate | agent proposes |
| 12 | Name for a merged acoustic product, if ever built (derived store) | **proposed**: *seafloor reflectance*; `backscatter` stays MBES-only — Roland to confirm |
| 13 | Non-sonar sources: lidar bathymetry, ROV/camera imagery — confirm each fits the five categories via the checklist | agent proposes |
| 14 | Tile versions (pass-stacked sidescan): layout + sync mechanism, vetted against the zero-cost-when-unused constraint | agent proposes after the level thread |

