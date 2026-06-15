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
- [x] Split DONE. #156 narrowed (body updated) to the `marine_interfaces` message work; cross-repo pieces moved to sub-issues:
  - rolker/camp#93 — remove dead legacy `Contact` references (must merge before #156 deletes `Contact.msg`)
  - rolker/unh_marine_radar#13 — migrate `marine_radar_tracker` to unified `Contact`, retire `Detect` (depends on the `Contact` definition landing)
  - Build-break-safe order: define `Contact` (additive) → camp#93 + radar#13 → retire `Detect` → delete legacy `Contact.msg`.
- [ ] `KeyValue` choice DECIDED ([comment](https://github.com/rolker/unh_marine_autonomy/issues/156#issuecomment-4702236131)): `Contact.attributes` uses `diagnostic_msgs/KeyValue`, not a clone — `diagnostic_msgs` is a core `common_interfaces` package already in the `core_ws` build (via `udp_bridge`), so the dependency is free, and cloning produces a distinct, non-interoperable DDS type. Add `diagnostic_msgs` to `marine_interfaces` `package.xml`/`CMakeLists.txt`.
- [x] Follow-up issue filed: #158 — standardize `marine_interfaces` on `diagnostic_msgs/KeyValue`, migrate `Heartbeat`/`Detect`, retire the clone (wire-breaking, deploy in lockstep; own change, not bundled into #156).
- [x] Add a project ADR-0004 capturing the Autoware-modeled design, the no-detect/contact-split decision, and the `Contact` name reuse — DONE (folded into PR #161, `docs/decisions/0004-unified-perception-contact.md`).
- [ ] Confirm no out-of-workspace consumers (operator/boat manifests, gitcloud repos) of legacy `Contact`/`Detect` before deleting + redefining `Contact`.
- [ ] Carry consequences in-PR: `package.xml` + `CMakeLists.txt` (new rosidl entries, `diagnostic_msgs` dep if kept), `.agents/README.md` msg count/inventory, package README, camp dead-code removal, radar tracker docs/tests.
- [ ] Add round-trip/field-convention tests for the `covariance[0]==-1` "unknown" and `orientation_availability` sentinels.

## Plan Authored
**Status**: complete
**When**: 2026-06-14 19:40 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-156/plan.md` at `da912f9`
**PR**: (see Implementation below)
**Phases**: single

### Open questions
- [x] Project ADR-0004 for the design — DONE, folded into PR #161 as `docs/decisions/0004-unified-perception-contact.md` (Roland chose fold-into-PR over fast-follow, 2026-06-14).
- [ ] No convention tests added here (no test/ dir; IDL has no logic); they land with the first producer. Acceptable?

## Implementation
**Status**: complete
**When**: 2026-06-14 19:55 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Commit**: `7157885` — marine_interfaces: add unified perception Contact message family
**Verification**: `./core_ws/build.sh marine_interfaces` clean (35s); `ros2 interface show marine_interfaces/msg/Contact` confirms the composed family generates and resolves (Classification[]/Kinematics/Shape/GeoPose/diagnostic_msgs/KeyValue[]).

### What changed
- New `Classification`, `Kinematics`, `Shape`, `ContactArray` messages; `Contact.msg` replaced in place with the unified perception contact (legacy vessel msg was orphaned; camp#93 merged removed its last consumer).
- `attributes` uses `diagnostic_msgs/KeyValue`; added `diagnostic_msgs` to `package.xml` + `CMakeLists.txt`.
- `.agents/README.md` msg count 34 → 39 (also corrected pre-existing drift).
- No plan deviations.

### Deferred (per Roland, time-crunch)
- [ ] `Detect` retirement — split to its own issue (filed; depends on rolker/unh_marine_radar#13 migrating the producer first). NOT done here.
- [ ] Confirm no out-of-workspace consumers of legacy `Contact` — in-tree clean (only camp, merged); operator station = camp (cleaned). Residual: gitcloud mirrors pick this up via reconciliation.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-14 20:30 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved
**Branch**: feature/issue-156 at `05e7666`
**Mode**: pre-push
**Depth**: Deep (reason: ADR add + shared interface-package edit)
**Must-fix**: 0 | **Suggestions**: 4

Specialists: Static (package.xml well-formed; no other linters apply), Governance, Claude Adversarial Lens A (logic) + Lens B (systemic). Lens B grepped the whole tree: no live pub/sub or surviving consumer of legacy `Contact` (ADR-0004's "camp#93 merged" claim verified true). README "39" confirmed accurate by two passes (old `34` was pre-existing drift).

### Findings
- [ ] (suggestion) Heads-up only — open camp branches `feature/issue-59` + `feature/issue-76` still reference old `marine_interfaces::msg::Contact` in `ais/*` (bases predate camp#93); they'll need a rebase or hit a compile break. Not fixable in this PR — flag to whoever lands those camp PRs.
- [ ] (suggestion) `Contact.msg` `geo_pose` has no "unset/unresolved" sentinel convention (unlike covariance) — consider pinning it down before merge while the interface is cheap to change.
- [ ] (suggestion) ADR-0004 Status `Proposed` vs project ADR-0001 `Accepted` — decide whether merging flips it to `Accepted`.
- [ ] (suggestion) No convention tests for covariance/`orientation_availability` sentinels — deferred to first producer (Watch).
