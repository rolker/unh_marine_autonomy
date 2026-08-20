# Issue #311 — Gardening dispositions (host posts, with operator confirmation)

Seven tracker items surfaced by ADR-0010's Consequences/housekeeping list. Each
section below gives the **disposition** (the state change the host makes) and the
**exact comment text** to post verbatim — the fenced block *is* the comment,
signature block included. The host posts each with operator confirmation; the
final open/close call rests with the operator at post time.

All citations are host-verified 2026-08-20. These seven items are the ADR-0010
gardening set: most come from the ADR's own Consequences/housekeeping list
(uma#163, mru_transform#8, s57_tools#23, s57_tools#26, echoboats#276), while
uma#151 derives from decision D9 (staged refinement / multi-level) and
uma#189/#256 from decision D7 (atomic tile writes gating live chart regen).

---

## uma#163 — Bathy store: add Chart source layer

**Disposition:** retire (close). The chart source layer shipped; the corpus
updater it depended on is tracked elsewhere, so closing orphans nothing.

**Post this comment on uma#163:**

```markdown
The chart source layer is implemented, so retiring this issue.

ADR-0010 D3 defines the `chart` provenance layer for the depth theme, and D7
covers ENC-corpus regeneration semantics (wholesale rebuild from the corpus,
atomic swap, never cell-wise merged). What landed:

- `chart` layer + wholesale regeneration — uma#280
- `s57_to_geotiff` exporter (DEPARE/DRGARE/SOUNDG → depth + CATZOC→σ) — s57_tools#29
- chart operator CLI — uma#291
- cost-model precondition (worst-case clearance / `confidence_gate`, so
  CATZOC-grade σ can enter the store without wholesale keepout) — uma#290

The corpus **updater** (cron-friendly download + nav-liveness-gated wholesale
rebuild) is **not orphaned by this close**: it shipped in s57_tools#28 ([PR#33](https://github.com/rolker/s57_tools/pull/33)
merged). Only its operational cron-cycle validation remains, tracked there — not
here.

The lake-contour-prior import idea (importing a lake contour grid as a
`reference/` prior) is out of scope for this chart-layer issue. If it's still
wanted, a fresh issue with a concrete source + import recipe is the right vehicle
— please open one if so; otherwise it lapses with this close.

—
Authored-By: Claude Code Agent
Model: Claude Fable 5
```

---

## uma#151 — Heterogeneous GGGS levels (staged refinement policy)

**Disposition:** retire (close). Multi-level store + the D9 refinement policy are
both in place.

**Post this comment on uma#151:**

```markdown
Retiring this issue — the level infrastructure and the staged-refinement policy
it was left open for are both in place.

The store has been multi-level since uma#178. ADR-0010 D9 fills the
"refinement policy — staged" gap ADR-0002 D2 explicitly left to this issue:

- `draft`/`processed` layers get **shallowest-preserving** overview pyramids
  (never a mean — the rock must survive the downsample), per ADR-0011's sidecar
  layout + fold-policy contract. Implemented in uma#320 (the
  `build_depth_overviews` builder writing an `overviews/` sidecar).
- the `chart` layer's ENC scale ladder is its own cartographer-curated pyramid —
  no generation needed (D9).
- `reference` is stored as imported; upsampling is never done — consumers
  wanting finer detail fall through to coarser levels via the level-aware query.

Nothing about heterogeneous levels remains open, so closing.

—
Authored-By: Claude Code Agent
Model: Claude Fable 5
```

---

## uma#189 vs uma#256 — Atomic tile writes (dedupe)

**Disposition:** close #189 as a duplicate of #256 (which carries more context);
add the ADR-0010 D7 load-bearing note to #256.

**Post this comment on uma#189 (then close as duplicate of #256):**

```markdown
Deduplicating with #256, which requests the same mechanism — atomic temp+rename
tile writes in the tiled raster store — with more context. Closing this one in
favor of #256.
```

**Post this comment on uma#256:**

```markdown
Adding context from ADR-0010: atomic tile writes are now **load-bearing for live
chart regeneration**, not just a general robustness improvement.

ADR-0010 D7 explicitly defers live chart-layer regeneration *under a running
consumer* until atomic tile writes (this issue, #256 / former #189) and a
store-change invalidation in `bathymetry_layer` exist. Until then the chart
updater regenerates only while navigation is down (an enforced nav-liveness
gate, not an assumption). So #256 is the unblocker for lifting that
"nav-must-be-down" restriction.

Also folding uma#189 into this issue as a duplicate — both requested the same
temp+rename mechanism; #256 carries the fuller context.

—
Authored-By: Claude Code Agent
Model: Claude Fable 5
```

---

## mru_transform#8 — TF-frame datum hierarchy design

**Disposition:** update/close per ADR-0010 D5/D6. The datum question is answered;
confirm whether a non-datum frame-hierarchy doc is still wanted separately.

**Post this comment on mru_transform#8:**

```markdown
ADR-0010 (unh_marine_autonomy, Accepted 2026-08-20) resolves the datum side of
this issue:

- **D5** — the entire runtime vertical world is GNSS-ellipsoidal. `map_tide`,
  self-measured by `sea_surface_estimator`, is the only runtime vertical datum
  reference; there are no tide tables, gauge feeds, or datum grids in the
  navigation loop. The `chart_datum` TF frame **exits the autonomy stack** once
  `s57_layer` stops computing depth (D10) — and it was spatially wrong as a
  single-offset frame anyway (MLLW–ellipsoid separation is a spatially varying
  surface).
- **D6** — the datum machinery is extracted into a ROS-free library in core_ws
  (`marine_vertical_datum`, uma#279): the PROJ/VDatum grid query plus the
  existing `datum_config` resolution chain, consumed by the S57 exporter, CAMP,
  and a thin transitional `chart_datum_node`. This subsumes mru_transform#7 (the
  VDatum service, delivered earlier as `chart_datum_node`).

So the chart-datum TF question this issue raised is answered: chart datum is no
longer a runtime TF frame. The remaining horizontal/platform TF frames are
unaffected by ADR-0010.

Please update or close accordingly — specifically, **is a frame-hierarchy design
doc for the non-datum frames still wanted as a separate effort?** If yes, this
issue should be re-scoped to that; if the datum frame was the whole of it, it can
be closed.

—
Authored-By: Claude Code Agent
Model: Claude Fable 5
```

---

## s57_tools#23 — Clean-room ENC→costmap design

**Disposition:** close — answered by ADR-0010.

**Post this comment on s57_tools#23:**

```markdown
Answered by ADR-0010 (unh_marine_autonomy, Accepted 2026-08-20), which settles
the ENC→costmap chain end-to-end:

- ENC depths flow **through the store**, not directly to the costmap: the
  `s57_to_geotiff` exporter (s57_tools#29) emits chart depth + CATZOC→σ into the
  store's `chart` layer (D3/D7), regenerated wholesale from the corpus.
- In the costmap, `bathymetry_layer` is the single depth authority; `s57_layer`
  is reduced to non-depth semantics only — land, `restricted`, `overhead`,
  `caution`, `unsurveyed`, point hazards (D10, split in s57_tools#31). Per-cell
  best-source selection happens in the store, before cost is computed, which is
  what makes surveyed-deeper data able to relax charted shoal cost without the
  max-combine footgun (Context §1 / echoboats#276).

The clean-room design this issue asked for is captured in the ADR. Closing.

—
Authored-By: Claude Code Agent
Model: Claude Fable 5
```

---

## s57_tools#26 — (costmap-chain concern mooted by D5/D10)

**Disposition:** close for the costmap chain — mooted by ADR-0010 D5/D10.
Operator to confirm no non-costmap remainder before closing.

**Post this comment on s57_tools#26:**

```markdown
ADR-0010 (unh_marine_autonomy, Accepted 2026-08-20) moots the costmap-chain
portion of this issue:

- **D5** removes `chart_datum` from the runtime entirely — the vertical world is
  GNSS-ellipsoidal, `map_tide` is the only runtime reference, and datum
  conversion happens once at import time (per-cell VDatum for ENC), never in the
  navigation loop.
- **D10** makes `s57_layer` obstacles-only (s57_tools#31) and routes all charted
  depth through the store's `chart` layer, with `bathymetry_layer` as the single
  depth authority.

If this issue was tracking a costmap-side ENC datum/depth concern, that path no
longer exists in the form it addressed. Please confirm there's no remaining
non-costmap scope; if not, close as mooted.

—
Authored-By: Claude Code Agent
Model: Claude Fable 5
```

---

## echoboats#276 — dangling "#14" reference (ref-note)

**Disposition:** ref-note only (no close). #276 is the max-combine override
footgun that motivated ADR-0010 Context §1; its dangling "#14" cross-reference
should be re-pointed at the D10 split so the trail is not broken.

**Post this comment on echoboats#276:**

```markdown
Reference fix-up: this issue's dangling "#14" cross-reference should point at the
ADR-0010 **D10 split** (`s57_layer` depth/obstacle separation, s57_tools#31),
which is where the resolution now lives.

For context: the max-combine override footgun described here — `bathymetry_layer`
can only raise cost, so ordering it after `s57_layer` can't let surveyed-deeper
data relax charted shoal cost, and overwrite would clear charted rocks/wrecks on
the same `elevation` band — is exactly the motivating evidence in ADR-0010
Context §1. The fix is per-cell best-source **in the store** (D3/D4) with
`bathymetry_layer` as the single depth authority and `s57_layer` reduced to
non-depth semantics (D10). The interim "re-enable `s57_layer` whole" path noted
here is superseded by that split.

Leaving this issue open only for the reference correction — adjust or close per
your tracking of the interim vs. the D10 end-state.

—
Authored-By: Claude Code Agent
Model: Claude Fable 5
```
