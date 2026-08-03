---
issue: 289
---

# Issue #289 — Expose `chart` source layer in `import_geotiff` CLI

## Issue Review
**Status**: complete
**When**: 2026-08-03 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #289
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Issue asks to wire `layerFromName("chart")` → `SourceLayer::Chart` in
`import_geotiff_main.cpp`, implement D7's wholesale-regeneration semantics
(`chart_staging_writable` gate + `replaceChartLayer` atomic swap) via a CLI
shape yet to be decided, add a round-trip acceptance test against the Lewes NOAA
corpus, and migrate the Lewes reference tiles to `chart` when done.

The library side (`SourceLayer::Chart`, `chart_staging_writable`,
`replaceChartLayer`) landed in #275; this issue closes the CLI gap.

### Scope Assessment

**Well-scoped?** Yes — the change lives in one file (`import_geotiff_main.cpp`)
plus a staging-mode CLI extension, bounded by an already-merged library API.
Single PR material.

**Right repo?** Yes — `unh_marine_autonomy`/`marine_bathymetry_store`.

**Dependencies:**
- #275 (store chart layer) — **prerequisite, merged** ✓
- #288 (world/ home) — referenced but not blocking; import paths are independent
- rolker/s57_tools#28 (updater) — downstream consumer of the CLI pattern set
  here; the CLI shape decision should be compatible with cron-driven wholesale
  regeneration
- ADR-0010 D7 cost-model precondition: D7 says chart ingestion into the costmap
  is gated on the worst-case-clearance / confidence-gate rework. This issue
  only wires the CLI import path (data enters the store), it does not activate
  the chart layer in the costmap — so the cost-model precondition does not block
  this PR, but the implementation should make that boundary explicit (import ≠
  activate).

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | CLI shape must be explicit and documented; no hidden side effects if staged-but-not-committed state is possible |
| Enforcement over documentation | OK | `chart_staging_writable` gate is already enforced in the library; CLI must ensure it uses the gate correctly |
| Capture decisions, not just implementations | Action needed | CLI shape (one-shot vs `--stage`/`--commit` pair) is an open design decision called out in the issue; must be captured before or in the implementing PR — either as an ADR-0010 addendum or at minimum a detailed rationale in the PR description |
| A change includes its consequences | Watch | Usage string (`layer: survey \| reference`) needs updating to include `chart`; Lewes migration is deferred but clearly called out — OK as a follow-on if the migration note is retained |
| Only what's needed | OK | Minimal: one new `layerFromName` branch plus staging/commit flow |
| Improve incrementally | OK | Directly builds on #275's merged API |
| Test what breaks | Action needed | Round-trip acceptance test needs concrete pass/fail criteria: what exactly is checked (tile count, depth range, σ values), not just "run against Lewes corpus" |
| Safety First (project) | Watch | ADR-0010 D7 requires the updater to check a nav-liveness signal before swapping; if the CLI implements one-shot mode, document that it is offline-only or add the same liveness guard |
| Modularity and Decoupling (project) | OK | CLI change is self-contained; no cross-package API change |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0010 D3/D7 (project) | Yes | Core reference. D7's wholesale-regeneration semantics are mandatory: stage into a temp/staging dir with `chart_staging_writable=true`, then `replaceChartLayer(staged_dir, store_dir)`. Never cell-wise merge. |
| ADR-0013 (workspace) | Yes | `progress.md` entry written here per vocabulary |
| ADR-0001 (project) | Watch | CLI shape decision is non-trivial; if it deviates from or extends D7's described model, capture the rationale as an ADR-0010 cross-reference addendum (ADR-0012 carve-out) |
| ADR-0002 (project) | Watch | Legacy context: `import_geotiff_main.cpp` still references `survey|reference`; the A2 collapse of `survey` into ADR-0010's `processed` (D3) means `survey` is a deprecated layer name — check whether the CLI should accept `survey` as an alias for `processed` or warn |

### Consequences

From the workspace consequences map:
- **Package parameters / CLI help strings**: `import_geotiff`'s `usage()` output says
  `layer: survey | reference` — update to `survey | reference | chart` (or
  `processed | draft | reference | chart` per D3 taxonomy if the full rename is
  in scope).
- **README / API docs**: if a package README or `docs/sonar_ecosystem.md` describes
  the `import_geotiff` CLI, update it in the same PR.
- **s57_tools PR #29 README**: the "Note on the `chart` import target" interim
  guidance should be removed or updated once this lands — flag as a cross-repo
  follow-up.

### Recommendations

- **Decide the CLI shape early** (pre-plan or in the plan): the issue offers two
  options — (a) `--stage`/`--commit` pair that lets callers stage multiple files
  before a single swap; (b) one-shot that stages internally and swaps at the end
  of the same invocation. Option (a) is more composable with `s57_tools#28`'s
  cron-driven multi-cell regeneration loop; option (b) is simpler for one-off
  imports. Document the rationale in the PR.
- **Confirm cost-model boundary**: add a note or assertion in the CLI or its
  docs that importing into the chart layer does not activate chart data in the
  costmap — readers of the PR should not infer that importing = costmap-active.
- **Lewes migration**: the issue calls it a follow-on ("when this issue lands"),
  which is appropriate since it requires re-running `s57_to_geotiff` — but
  confirm that the 16 reference tiles' level-5/7/8 footprints are unambiguous
  identifiers so the migration step is not accidental.

### Actions
- [ ] Capture the CLI shape design decision (one-shot vs --stage/--commit) in the plan or as an ADR-0010 addendum before implementation begins.
- [ ] Define concrete pass/fail criteria for the Lewes round-trip acceptance test (tile count, depth range, σ range, or diff against the existing reference import).
- [ ] Update `usage()` string and any README references to include `chart` in the layer list.
- [ ] Document the import-≠-costmap-active boundary explicitly in the PR or CLI help.
