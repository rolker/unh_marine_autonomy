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
- [x] #157 is now the umbrella; v1 slice carved into sub-issues ([tracking comment](https://github.com/rolker/unh_marine_autonomy/issues/157#issuecomment-4706986617)):
  - #167 — core: CRUD `.srv` + `contact_manager` node + standalone store + distribution + ADR-0005 + tests (do first)
  - rolker/camp#95 — CAMP contacts layer (display + confirm/reject; depends on #167)
  - rqt_operator_tools#59 — producer alignment, emit `Contact` (comment posted; depends on #167)
  - Deferred on umbrella: full in-CAMP edit, multi-producer ingest (radar post unh_marine_radar#13), web viewer #166.
- [x] Three design forks DECIDED with Roland 2026-06-14/15 ([comment](https://github.com/rolker/unh_marine_autonomy/issues/157#issuecomment-4704925993)): Fork 1 = one-way + confirm, rqt#59 sole creator (service stays full-CRUD); Fork 2 = standalone store (lean SQLite, clean interface for future content-hash sync); Fork 3 = new `contact_manager` package in `unh_marine_autonomy` (sibling to `marine_bathymetry_store`), `.srv` in `marine_interfaces`.
- [ ] Capture settled forks in a project ADR-0005 (manager architecture), mirroring ADR-0004.
- [ ] Align distribution with ADR-0003 / marine_control state/change contract (coordinate with #140) — don't invent a contact-specific state dialect.
- [ ] Point rqt_operator_tools#59 at `Contact` (currently emits `TargetAnnotation`) — producer alignment dependency.
- [ ] Plan tests with the manager (CRUD, proposal dedup/ingest, lifecycle transitions, persistence round-trip) — real logic, unlike #156's pure IDL.
- [ ] Consequences when building: new `.srv` → marine_interfaces CMakeLists/package.xml/README/tests; new node → build order + package inventory + launch.
