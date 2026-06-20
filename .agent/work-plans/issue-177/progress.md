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
