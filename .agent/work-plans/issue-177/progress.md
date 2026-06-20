---
issue: 177
---

# Issue #177 — marine_sidescan_mosaic: draft-layer newest-valid-wins (recency) compositing policy

## Issue Review
**Status**: complete
**When**: 2026-06-20 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Issue**: #177
**Comment**: https://github.com/rolker/unh_marine_autonomy/issues/177#issuecomment-4757747386
**Scope verdict**: well-scoped

### Actions
- [ ] Clarify the validity predicate (`value == 0` as the no-data guard) and document it in the `add()` docstring or inline comment — the issue says "null/zero/dropout" but the implementation must pin it to the normalizer output contract.
- [ ] Add `newest` to the `splat` parameter row in `marine_sidescan_mosaic/README.md`.
- [ ] Make the splat-param parse a 3-way branch (`mean` / `max_hold` / `newest`) and add a WARN log for unrecognised values.
- [ ] Include a note in the PR description calling out the default change (`mean` → `newest`) so operators know to set `splat: mean` explicitly if they relied on the old default.

## Plan Authored
**Status**: complete
**When**: 2026-06-20 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Plan**: `.agent/work-plans/issue-177/plan.md` at `e744fd8`
**Branch**: feature/issue-177 at `e744fd8`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-06-20 09:15 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context)) (in-context — author self-review)

**Plan**: `.agent/work-plans/issue-177/plan.md` at `e744fd8`
**PR**: PR-less
**Verdict**: approve

### Findings
- [ ] (suggestion) Plan cites `normalizer.cpp:75` for the clamp; the `std::clamp(scaled, 1.0, 65535.0)` floor is actually at line 74 (line 75 is the `static_cast`). Predicate is correct; the line ref is off by one. — `plan.md:18,30,89`
- [ ] (suggestion) Live-node log line (`mosaic_node.cpp:182`, `splat=%s`) already echoes the active splat string, so the `mean`→`newest` default change is observable in node startup logs — worth keeping when implementing. — `plan.md:53`
