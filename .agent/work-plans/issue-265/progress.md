---
issue: 265
---

# Issue #265 — survey_index: decimated nav track table for the explorer map (schema bump)

## Issue Review
**Status**: complete
**When**: 2026-07-16 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #265
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Actions
- [ ] Pin the decimation strategy (time-based, distance-based, or hybrid) during planning and record the decision in plan.md — it is explicitly TBD in the issue body.
- [ ] Confirm tests are in scope: schema version detection + re-index trigger, new accessor queries, decimation logic (the test/ directory exists in marine_survey_index).
- [ ] Ensure docs/survey_index_schema.md is updated in the same PR (not a follow-up) — the issue calls this out but plan should enforce it.
- [ ] Define the accessor API contract clearly (bounding-box and bag-id retrieval) so marine_perception_tools (#258) can build against a stable interface.
