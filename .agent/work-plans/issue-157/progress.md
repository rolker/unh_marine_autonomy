---
issue: 157
---

# Issue #157 — Contact manager: CRUD store + distribution + CAMP rendering for unified Contacts (consumes #156)

## Issue Review
**Status**: complete
**When**: 2026-06-15 09:30 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Issue**: #157
**Comment**: https://github.com/rolker/unh_marine_autonomy/issues/157#issuecomment-4704664605
**Scope verdict**: needs-splitting

### Actions
- [ ] Make #157 an umbrella; carve a v1 slice: `.srv` (marine_interfaces) + manager node + distribution topic + CAMP display+confirm. Defer full in-CAMP create/edit/delete, multi-producer ingest, heavy persistence to sub-issues (CAMP layer → camp repo sub-issue).
- [ ] Settle three design forks before plan-task (need Roland's decision): Fork 1 CAMP one-way+confirm vs two-way (lean one-way); Fork 3 new `contact_manager` package vs in `unh_marine_autonomy` (lean new package); Fork 2 standalone store vs reuse #86 bathy-store (lean standalone for v1).
- [ ] Capture settled forks in a project ADR-0005 (manager architecture), mirroring ADR-0004.
- [ ] Align distribution with ADR-0003 / marine_control state/change contract (coordinate with #140) — don't invent a contact-specific state dialect.
- [ ] Point rqt_operator_tools#59 at `Contact` (currently emits `TargetAnnotation`) — producer alignment dependency.
- [ ] Plan tests with the manager (CRUD, proposal dedup/ingest, lifecycle transitions, persistence round-trip) — real logic, unlike #156's pure IDL.
- [ ] Consequences when building: new `.srv` → marine_interfaces CMakeLists/package.xml/README/tests; new node → build order + package inventory + launch.
