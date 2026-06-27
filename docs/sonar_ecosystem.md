# Sonar Data Ecosystem — Big-Picture Map

A single living map of how sonar data flows from sensor to operator, and which
umbrella issues + ADRs own each stage. This is a **tracker, not a spec** — the
authoritative design lives in the linked ADRs and umbrella issues. Keep it
current; when an umbrella closes or a frontier shifts, update the relevant row.

> Status legend: ✅ done · 🔨 in progress · 📋 designed, not built · ⚠️ blocked/degraded

_Last verified: 2026-06-27._

## The two arcs

Sonar data drives **two peer arcs** that share the GGGS store and the operator's
map:

```
ARC 1 — COVERAGE  ("where did we survey / what's the depth & backscatter")
  ACQUIRE ─→ ESTIMATE ─→ STORE ─→ ┬─→ LIVE TRANSPORT → CAMP / web viewer
  (sensor)   (live grid)  (tiled) │
                                  ├─→ COSTMAP → Nav2
                                  ├─→ RENDER (display)
                                  └─→ REPROCESS → processed layer

ARC 2 — TARGETS   ("what did we find")
  EXPLORE ─→ MARK ─→ CONTACT ─→ DISTRIBUTE ─→ DISPLAY (CAMP / web)
  (rqt views over   (draw-box   (contact_manager
   sidescan & MBES)  → TargetAnnotation)  CRUD / curate / confirm)
```

Everything is GGGS-tiled (Colin Ware's Global Geographic Grid System); the
**store is the hub** for Arc 1, and both arcs converge on the operator's map.
Arc 2 is the **search mission's payload** — for Lake Massabesic (submerged-object
search), finding and marking targets *is* the point; Arc 1 tells you where you've
looked.

## Arc 1 — Coverage

| Stage | What | Owning umbrella(s) | ADR | Status |
|-------|------|--------------------|-----|--------|
| **Acquire — MBES** | M3 multibeam driver (`kongsberg_em_bridge`) | — | — | ✅ functional, ⚠️ data-quality bugs: timing [echoboats#338], gyro/roll [echoboats#337]/[echoboats#339] |
| **Acquire — sidescan** | Garmin GCV driver + platform integration | [#185](https://github.com/rolker/unh_marine_autonomy/issues/185) | — | 🔨 protocol cracked; driver/platform in flight |
| **Acquire — other** | DeltaT sonar driver | [#111](https://github.com/rolker/unh_marine_autonomy/issues/111) | — | 📋 |
| **Estimate — bathy** | CUBE draft tiles (live), backscatter co-estimation, slope correction | — | 0007 | ✅ mature ([cube#54], [cube#70] closed); slope-correction merged but dormant (needs [cube#59]) |
| **Estimate — sidescan** | `marine_sidescan_mosaic` live mosaic (L13, uint16) | [#171](https://github.com/rolker/unh_marine_autonomy/issues/171) (I2), [#185](https://github.com/rolker/unh_marine_autonomy/issues/185) | — | 📋 design locked, not built |
| **Store — core** | Generic band/dtype tiled-GeoTIFF store core | [#172](https://github.com/rolker/unh_marine_autonomy/issues/172) | 0002 | ✅ closed (`marine_tiled_raster_store`) |
| **Store — bathy** | Multi-source bathy store; layers (draft/processed/chart), priority query | [#86](https://github.com/rolker/unh_marine_autonomy/issues/86) | 0002 | ✅ readiness arc complete; sub-features open: [#151] levels, [#163]/[cube#44] chart, [#188] pyramids, [#189] atomic write |
| **Store — backscatter** | Two-tier backscatter store (GeoCoder, draft/processed) | [#180](https://github.com/rolker/unh_marine_autonomy/issues/180) | 0006 / 0007 | 📋 core supports it; sidescan/MBES specifics not built |
| **Store — provenance** | Cross-store multi-platform source-id + registry | [#179](https://github.com/rolker/unh_marine_autonomy/issues/179) | 0005 | 📋 deferred (multi-platform later tier) |
| **Live transport** | `SonarVisualizationTile` + anti-entropy tile-sync | [#230](https://github.com/rolker/unh_marine_autonomy/issues/230) (= #86-Phase-6 / #171-I3) | 0008 | 🔨 **current** — PR1 messages done; reconciler + [cube#78] producer + [camp#121] consumer next |
| **Costmap** | Bathy → Nav2 costmap layer | [#127](https://github.com/rolker/unh_marine_autonomy/issues/127) | — | ⚠️ built but **unusable** until the tide-frame fix [uma#220] + cost-model rework (midpoint+uncertainty) |
| **Render — CAMP** | Unified band-select + colormap raster render + live cache | [#175](https://github.com/rolker/unh_marine_autonomy/issues/175) (I4), [camp#121], [camp#108], [camp#63] | 0001 / 0008 | 🔨 file-store display works; unified render + live cache not built |
| **Render — web** | Browser SA viewer (contacts + bathy + sidescan) | [#166](https://github.com/rolker/unh_marine_autonomy/issues/166) | — | 📋 |
| **Reprocess** | Offline M3 bag → store tiles; PINGMapper offline sidescan EGN | [#171](https://github.com/rolker/unh_marine_autonomy/issues/171) (C1) | — | ✅ M3 import landed ([cube#63] closed); 📋 sidescan offline pipeline to validate; ⚠️ draft→**processed** promotion workflow thin |
| **Metering** | Per-topic priority on the `udp_bridge` rate-limiter | [udp_bridge#19](https://github.com/rolker/udp_bridge/issues/19) | 0008 (D9) | 📋 |

[cube#15]: https://github.com/rolker/cube_bathymetry/issues/15
[cube#44]: https://github.com/rolker/cube_bathymetry/issues/44
[cube#54]: https://github.com/rolker/cube_bathymetry/issues/54
[cube#59]: https://github.com/rolker/cube_bathymetry/issues/59
[cube#63]: https://github.com/rolker/cube_bathymetry/issues/63
[cube#70]: https://github.com/rolker/cube_bathymetry/issues/70
[cube#78]: https://github.com/rolker/cube_bathymetry/issues/78
[cube#80]: https://github.com/rolker/cube_bathymetry/issues/80
[camp#63]: https://github.com/rolker/camp/issues/63
[camp#90]: https://github.com/rolker/camp/issues/90
[camp#108]: https://github.com/rolker/camp/issues/108
[camp#121]: https://github.com/rolker/camp/issues/121
[rqt#58]: https://github.com/rolker/rqt_operator_tools/issues/58
[uma#220]: https://github.com/rolker/unh_marine_autonomy/issues/220
[echoboats#337]: https://github.com/rolker/unh_echoboats_project11/issues/337
[echoboats#338]: https://github.com/rolker/unh_echoboats_project11/issues/338
[echoboats#339]: https://github.com/rolker/unh_echoboats_project11/issues/339
[#151]: https://github.com/rolker/unh_marine_autonomy/issues/151
[#163]: https://github.com/rolker/unh_marine_autonomy/issues/163
[#171]: https://github.com/rolker/unh_marine_autonomy/issues/171
[#185]: https://github.com/rolker/unh_marine_autonomy/issues/185
[#188]: https://github.com/rolker/unh_marine_autonomy/issues/188
[#189]: https://github.com/rolker/unh_marine_autonomy/issues/189

## Arc 2 — Targets (find, mark, curate)

The human-in-the-loop search arc: an operator explores sonar data in live `rqt`
views, marks a target, and that mark becomes a curated **Contact** distributed to
the map. Contacts ride a **separate** transport (SQLite + `marine_control` state
topic), intentionally *not* the Arc-1 raster tile-sync.

| Stage | What | Owning issue(s) | ADR | Status |
|-------|------|-----------------|-----|--------|
| **Explore** | `rqt` sonar review tools: waterfall over sidescan **and now MBES**, echogram, target views | [rqt#53] (echogram), [rqt#40] (MBES backscatter extractor), [rqt#50]/[rqt#69]/[rqt#73]/[rqt#82] (waterfall) | — | 🔨 waterfall + echogram active; MBES extractor 📋 |
| **Mark** | draw-a-box target marking in the live view → `TargetAnnotation` | [rqt#59] | — | 📋 |
| **Mark → Contact** | live-view marking publishes into `contact_manager` | [rqt#81] | 0004 | 📋 — the load-bearing link between the arcs |
| **Contact** | unified `Contact` CRUD store + curate/confirm + distribution | [#157](https://github.com/rolker/unh_marine_autonomy/issues/157) / [#167](https://github.com/rolker/unh_marine_autonomy/issues/167) (+#156) | 0004 | 🔨 v1 core in flight |
| **Device control** | bridgeable settings for the review tools / CA tuning | [#168](https://github.com/rolker/unh_marine_autonomy/issues/168) | 0003 | 🔨 |
| **Display** | contacts on the CAMP + web map (shared with Arc 1 render) | [#166](https://github.com/rolker/unh_marine_autonomy/issues/166) | — | 📋 |

[rqt#40]: https://github.com/rolker/rqt_operator_tools/issues/40
[rqt#50]: https://github.com/rolker/rqt_operator_tools/issues/50
[rqt#53]: https://github.com/rolker/rqt_operator_tools/issues/53
[rqt#59]: https://github.com/rolker/rqt_operator_tools/issues/59
[rqt#69]: https://github.com/rolker/rqt_operator_tools/issues/69
[rqt#73]: https://github.com/rolker/rqt_operator_tools/issues/73
[rqt#81]: https://github.com/rolker/rqt_operator_tools/issues/81
[rqt#82]: https://github.com/rolker/rqt_operator_tools/issues/82

> **Tracking gap (follow-up):** unlike Arc 1 (which has #86 / #171 / ADR-0008),
> the explore→mark→contact→display arc has **no unifying umbrella** tying the
> `rqt` marking tools ([rqt#59]/[rqt#81]) to the `contact_manager` backend
> (#157/#167) to display (#166). **Candidate follow-up: file a "target detection
> arc" umbrella** so this arc is as legible as the coverage arc. Not yet filed —
> noted here pending a decision (see #232).

## ADR spine

| ADR | Title | Stage |
|-----|-------|-------|
| [0001](decisions/0001-shared-scalar-colormap.md) | Shared scalar colormap | Render |
| [0002](decisions/0002-bathymetric-data-store.md) | Bathymetric data store (GGGS) | Store |
| [0003](decisions/0003-bridgeable-device-control.md) | Bridgeable device control | Arc 2 (tools/tuning) |
| [0004](decisions/0004-unified-perception-contact.md) | Unified perception contact | Arc 2 (Targets) |
| [0005](decisions/0005-multi-platform-provenance-registry.md) | Multi-platform provenance/registry | Store |
| [0006](decisions/0006-multi-platform-backscatter-store.md) | Multi-platform backscatter store | Store |
| [0007](decisions/0007-mbes-backscatter-store.md) | MBES backscatter store | Estimate/Store |
| [0008](decisions/0008-live-sonar-coverage-transport-and-render.md) | Live sonar coverage transport & render | Transport/Render |

## Where to direct efforts

### Near-term: last few days of Massabesic — three operator tools

Mission-driven priority (Roland, 2026-06-27): the three tools worth having before
the final surveying days. Coverage (Tools 1–2) is **band-selectable** — the
operator views it as **depth / uncertainty / backscatter** through one CAMP
band-picker + colormap (ADR-0008 D6; [camp#108]/[camp#121]).

1. **Display existing coverage, especially multibeam.** The M3 bathy is already
   in the GGGS store ([cube#63] closed); CAMP's GGGS raster layer ([camp#90],
   closed) renders it. Shortest path: point CAMP at the Massabesic store + a
   colormap. **Closest of the three**, and the render foundation Tool 2 reuses.
   - **Bands:** depth + uncertainty come from the bathy store today. **Backscatter
     is added by [cube#80]** — the offline M3 import also writes a float32
     backscatter store layer (float32 tiles already persist in
     `marine_tiled_raster_store`; ADR-0007 producer). First cut = beam-angle-vs-nadir;
     GeoCoder-grade incidence ([cube#15]/[cube#59]) is a follow-up.
2. **Display new coverage as it's collected.** The [#230] chain (messages +
   reconciler ✅ merged) → [cube#78] producer → [camp#121] live cache, **feeding
   the same band-generic render as Tool 1**. [cube#78] already carries the live
   3-band tile (depth/uncertainty/backscatter; co-estimation [cube#54] done).
   Farthest, but the live cache is the only new display piece.
3. **Mark a target on the live sidescan view → save a contact to the operator bag.**
   The waterfall's slant→ground geometry ([rqt#58]) is closed; add the draw-box
   marking ([rqt#59]'s UI half) and **publish a `marine_interfaces/Contact`**
   (already defined, `ORIGIN_HUMAN`) on a bag-recorded topic. **Sidesteps two
   blockers**: the unsettled `TargetAnnotation` message gating [rqt#59], and the
   `contact_manager` backend (#157/#167) — neither is needed to record a contact.

**Producer symmetry:** existing coverage's backscatter comes from the offline
import ([cube#80], durable store layer); live coverage's from [cube#78] (display
tile). One band-generic CAMP renderer shows either.

**Sequence:** Tool 1 first (closest; shared render foundation; [cube#80] gives it
backscatter) ‖ Tool 3 in parallel (independent rqt track, unblocked) → Tool 2
(live cache on Tool 1's render).

### Longer-term / supporting

- **Make the costmap usable** ([uma#220] tide-frame fix + cost-model rework) —
  built but 100% lethal today; best ratio of "unlocks autonomy" to work remaining.
- **Land acquisition data-quality bugs** ([echoboats#337]/[echoboats#338]/[echoboats#339])
  — corrupt every downstream product in *both* arcs; cheap, foundational.
- The **sidescan track** ([#171]/[#185]) and the **draft→processed + coverage-QC
  loop** (the gap between "collected data" and "finished survey") — [cube#80] is
  a first step, producing an authoritative backscatter layer off-boat.

## Related

- [interfaces.md](interfaces.md) — ROS message/topic/service definitions
- [data_flows.md](data_flows.md) — system-level data flow diagrams
- [decisions/](decisions/) — Architecture Decision Records
