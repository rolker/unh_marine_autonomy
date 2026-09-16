---
issue: 389
---

# Issue #389 — Depth overview pyramids go stale: rebuild them whenever a depth layer is updated, and make staleness detectable

## Issue Review
**Status**: complete
**When**: 2026-09-16 08:36 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #389
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: needs-more-detail

### Actions
- [ ] Pin the staleness-signal design as an ADR amendment (to `docs/decisions/0011-overview-pyramid.md` and/or `0013-bounded-lod-navigation.md`, the same pattern #331 used) before/alongside plan-task — the comparison basis (native tile mtime vs. a `cube_bathymetry`-style content fingerprint), the manifest schema-version bump, and who reads it (CAMP LOD loader? a new `store_admin` check?) are all undecided design points, not implementation details.
- [ ] Cite the precedent as `cube_bathymetry` ADR-0003 ("Build fingerprint for incremental regen staleness detection"), not bare "ADR-0003" — this repo's own ADR-0003 is "Bridgeable device control," an unrelated topic; the issue body's cross-repo reference is ambiguous as written.
- [ ] Confirm the staleness signal stays advisory-only per uma-ADR-0013 D8 — any consumer (CAMP warn/skip, a `store_admin` check) must never let a stale-overview flag gate or filter what shoal-finding/least-depth queries return; only the display/derived path may branch on it.
- [ ] Decide whether the "incremental fold (touched-tile ancestors only)" half is in scope for this PR or deferred — the issue itself says a full rebuild is an acceptable first step. Consider generalizing the touched-tile fold in the shared `marine_tiled_raster_store/overview_builder.hpp` engine (ADR-0011 §4: "Fold policies are per-store; the engine is shared") so `marine_sidescan_mosaic` and the new MBES backscatter builder (#390) can adopt it too, rather than building it depth-store-specific.
- [ ] Note the explicit dependency direction to `rolker/unh_echoboats_project11#490`: that issue's `build_bathy_store.sh` trigger wiring consumes whatever interface this issue defines (a CLI re-run, a new "rebuild if stale" mode, etc.) — #490 is blocked on this issue's interface decision, not merely related to it.
- [ ] Update `marine_bathymetry_store/README.md` §"Depth overview pyramids" (~line 234) for **both** new behaviors — the issue's checkbox only names the trigger, but the new staleness-signal field/semantics need documenting there too, consistent with the section's existing level of detail.
- [ ] Scope the "rebuild the dev-host world store's `processed` pyramid now" checkbox as a post-merge operational step (run `build_depth_overviews` against `~/data/world/depths/processed`), not a code/test deliverable — overviews are derived/regenerable per ADR-0011 so this doesn't touch data-of-record, but it shouldn't be conflated with the PR's test plan.
- [ ] plan-task should include explicit tests for: incremental fold touching only ancestors of changed tiles (if implemented), staleness detection correctly flagging native-newer-than-overview across a mixed-level layer, and a refused/partial swap not falsely clearing the stale flag.

## Plan Authored
**Status**: complete
**When**: 2026-09-16 09:00 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-389/plan.md` at `fa32792`
**Branch**: feature/issue-389 at `fa32792`
**Phases**: single

### Open questions
- [ ] None that block implementation — ADR-0011 §6's three deliberately-deferred mechanism choices (digest preimage, link-vs-copy carry-over, report format) are settled in the plan's Approach steps 1–3.
- [ ] Confirm at review-plan/implementation time whether the post-fold re-stat drift refusal (step 4h) should fire on both the incremental and full-fold paths, or incremental-only — plan currently specifies both, flagged as a judgment call, not a blocker.
