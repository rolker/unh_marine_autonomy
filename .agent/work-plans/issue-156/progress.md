---
issue: 156
---

# Issue #156 — marine_interfaces: unified perception Contact message (sensor-agnostic detect/contact) + retire legacy Contact/Detect

## Issue Review
**Status**: complete
**When**: 2026-06-14 11:10 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Issue**: #156
**Comment**: https://github.com/rolker/unh_marine_autonomy/issues/156#issuecomment-4702093149
**Scope verdict**: needs-splitting

### Actions
- [ ] Split into coordinated PRs/sub-issues by repo, ordered to avoid build breaks: (A) additive new-message PR in `marine_interfaces` → (B) camp dead-code cleanup (`ui_ws/src/camp`) → (C) delete legacy `Contact.msg` → (D) migrate `marine_radar_tracker` (`unh_marine_radar`) + retire `Detect`. Open sub-issues in `camp` and `unh_marine_radar` referencing #156.
- [ ] Resolve the `KeyValue` choice before defining the message: reuse `marine_interfaces/KeyValue` (which exists to avoid a `diagnostic_msgs` dependency) or document why the dependency-minimization decision is being reversed.
- [ ] Add a project ADR-0004 capturing the Autoware-modeled design, the no-detect/contact-split decision, and the `Contact` name reuse.
- [ ] Confirm no out-of-workspace consumers (operator/boat manifests, gitcloud repos) of legacy `Contact`/`Detect` before deleting + redefining `Contact`.
- [ ] Carry consequences in-PR: `package.xml` + `CMakeLists.txt` (new rosidl entries, `diagnostic_msgs` dep if kept), `.agents/README.md` msg count/inventory, package README, camp dead-code removal, radar tracker docs/tests.
- [ ] Add round-trip/field-convention tests for the `covariance[0]==-1` "unknown" and `orientation_availability` sentinels.
