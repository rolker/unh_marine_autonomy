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
- [ ] Add a project ADR-0004 capturing the Autoware-modeled design, the no-detect/contact-split decision, and the `Contact` name reuse.
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
- [ ] Project ADR-0004 for the design — write now or fast-follow? (Roland: time-crunch — leaning fast-follow.)
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
