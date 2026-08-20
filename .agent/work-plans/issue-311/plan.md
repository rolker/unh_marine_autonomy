# Plan: World-model arc cleanup: sonar_ecosystem.md reframe, ADR-0010 flip, and issue gardening

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/311

## Context

ADR-0010 (Geospatial World Model) is complete: D8 (uma#308 + cube#133) and D9
(uma#309) both merged 2026-08-20. The ADR still reads "Proposed"; `docs/sonar_ecosystem.md`
predates the world-model framing and is missing D3 taxonomy, clearOverlappedDraft (D8),
depths-overviews sidecar (D9), and ADR-0010/0011 in its spine table.

`~/data/world/` is the ADR-0010-adopted store root. It exists today as an eval store
only (uma#314: 13 ENC cells). The full migration is uma#310 (UNSTARTED). Docs must
present the D3 tree as the **adopted target, partially materialized** — not completed.

Items 3–6 are GitHub comments the host posts with operator confirmation. The plan
drafts the disposition text so the host can post verbatim.

## Approach

1. **Reframe `docs/sonar_ecosystem.md`** — add world-model framing to intro; add D3
   taxonomy table (chart/reference/draft/processed) and `~/data/world/` as adopted
   target; update Store-bathy row (✅ completed sub-features, clearOverlappedDraft,
   depths-overviews sidecar); add ADR-0010 and ADR-0011 to ADR spine; update
   "Where to direct efforts" frontier for Isles of Shoals / world-model context;
   update the "Last verified" date to 2026-08-20.

2. **Flip ADR-0010 status** — change "Proposed (2026-07-24)" to "Accepted (2026-08-20)"
   with implementing PR citations: D6 PR#279, D3/D7 PR#280 + s57_tools PR#29 +
   PR#291 (uma#289 CLI), D10 s57_tools#30, cost-model PR#290, D8 PR#313 + cube#134,
   D9 PR#320. #272 (tracking issue) was closed when implementation completed.

3. **(Host action) Garden #163** — retire; see draft below.
4. **(Host action) Garden #151** — retire; see draft below.
5. **(Host action) Dedupe #189 vs #256** — close #189 as duplicate; annotate #256; see draft below.
6. **(Host action) Comment on mru_transform#8** — update with ADR-0010 D5/D6 resolution; see draft below.

## Files to Change

| File | Change |
|------|--------|
| `docs/sonar_ecosystem.md` | World-model reframe: D3 taxonomy, ~/data/world/ target, D8/D9 features, ADR-0010+ADR-0011 in spine, updated store-bathy row and frontier |
| `docs/decisions/0010-geospatial-world-model.md` | Status: Proposed → Accepted (2026-08-20) with implementing PR citations |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Capture decisions, not just implementations | Central goal — ADR flip and doc reframe align the record with the as-built state |
| A change includes its consequences | sonar_ecosystem.md describes D8/D9 as-built; ~/data/world/ caveated as partial; gardening cross-links ADR-0010 |
| Only what's needed | Two file edits; four drafted comments; no code, no new files |
| Improve incrementally | Two independent atomic commits (sonar_ecosystem.md, ADR flip); items 3–6 are host actions |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0001 (Adopt ADRs) | Yes | ADR-0010 status flip follows the ADR update convention |
| ADR-0010 (Geospatial World Model) | Yes (central) | This PR executes ADR-0010's "Consequences / housekeeping" items directly |
| ADR-0013 (progress.md vocabulary) | Yes | Issue Review exists; this plan appends Plan Authored |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| sonar_ecosystem.md ADR spine | Add ADR-0010 and ADR-0011 rows | Yes |
| ADR-0010 status | sonar_ecosystem.md frontier updated in same PR | Yes |
| #163 retired | ADR-0010 Consequences mention of uma#163 stays accurate | Yes — cross-link in comment |
| #151 retired | ADR-0010 D9 mention of #151 gap stays accurate | Yes — cross-link in comment |

## Gardening Draft Texts (items 3–6; host posts with operator confirmation)

### #163 — Bathy store: add Chart source layer

> The chart source layer is implemented. ADR-0010 D3 defines the `chart` provenance
> layer; D7 covers the ENC-corpus regeneration semantics (wholesale rebuild, atomic
> swap). The `s57_to_geotiff` exporter and store integration landed in uma PR#280;
> the operator CLI landed in PR#291 (uma#289). The corpus updater (cron-friendly
> schedule + nav-liveness guard) is the next piece but the layer concept and ingest
> path are established.
>
> Retiring this issue. The lake-contour-prior import idea (a `reference/` import of a
> lake contour grid) is out of scope for this cleanup — if it resurfaces, a fresh
> issue with a concrete source + workflow is the right vehicle.

### #151 — Heterogeneous GGGS levels

> Multi-level store landed in uma#178. ADR-0010 D9 fills the staged-refinement-policy
> gap this issue was left open for: `draft`/`processed` layers get shallowest-preserving
> overview pyramids (ADR-0011; never mean — the rock must survive the downsample);
> the `chart` layer's ENC scale ladder is its native curated pyramid. uma#309
> (PR#320, merged 2026-08-20) implements the depths pyramid per ADR-0011 §4.
>
> Retiring this issue — level infrastructure and refinement policy are both in place.

### #189 vs #256 — Atomic tile writes (dedupe)

Post on #189:
> Deduplicating with #256, which also requests atomic temp+rename tile writes in the
> tiled raster store. The two issues request the same mechanism. Closing this one in
> favor of #256, which carries more context.
>
> Note for #256: ADR-0010 D7 explicitly defers live chart-layer regeneration under a
> running consumer until atomic tile writes exist — so #256 is now load-bearing for
> that path, not just a robustness improvement. Adding that context to #256.

*(Host: close #189 as duplicate of #256; add the ADR-0010 D7 note to #256.)*

### mru_transform#8 — TF frame hierarchy design doc

> ADR-0010 D5 resolves the chart-datum question in the TF tree: `chart_datum` exits
> the autonomy stack once `s57_layer` stops computing depth (D10); `map_tide`
> (self-measured by `sea_surface_estimator`) is the only runtime vertical datum
> reference. ADR-0010 D6 extracts the datum machinery into a ROS-free library in
> core_ws (subsuming the VDatum service from mru_transform#7).
>
> If this issue was tracking design-doc work for the full TF frame hierarchy, the
> datum question is answered by ADR-0010. The remaining TF tree (horizontal frames,
> platform-specific transforms) is unaffected. Please update or close accordingly —
> specifically whether a frame-hierarchy design doc for the non-datum frames is still
> wanted as a separate effort.

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `docs/sonar_ecosystem.md` and
  `docs/decisions/0010-geospatial-world-model.md` — both are the subject of
  this PR and land in it.
- **Agent-instruction candidates**: None — pure doc/tracker housekeeping; no new
  patterns surfaced.

## Open Questions

- For #163: is the lake-contour-prior import idea actively wanted as a follow-on issue, or definitively out of scope?
- For mru_transform#8: is the issue tracking only the datum design (answered by ADR-0010), or also a broader TF frame hierarchy doc?

## Estimated Scope

Single PR; two atomic doc commits (sonar_ecosystem.md reframe, ADR-0010 status flip).
Items 3–6 are host-posted GitHub comments, not PR commits.
