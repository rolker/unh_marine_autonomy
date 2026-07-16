---
issue: 240
---

# Issue #240 — Prototype a SonarInfo message (CameraInfo analog for acoustic-sensor calibration / intensity semantics)

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-16 10:11 -0400
**By**: Claude Code Agent (Claude Fable 5)
**Verdict**: changes-requested (all findings addressed in `2c31317` same session)

**Branch**: feature/issue-240 at `613656f` (fixes at `2c31317`)
**Mode**: pre-push
**Depth**: Deep (reason: new ADR in docs/decisions/ — Deep promotion trigger; 309 lines)
**Must-fix**: 3 | **Suggestions**: 7
**Round**: 1 | **Ship**: recommended — all must-fixes were doc/wire-format corrections, applied and re-verified (build + lint green) in the same session

### Findings
- [x] (must-fix) NaN/bool sentinels not honest under default construction, contradicting the ADR's `*_UNKNOWN=0` honesty rule — `marine_interfaces/msg/SonarInfo.msg` (Claude Adversarial / Lens A) → conventions block added; bool → tri-state `angular_normalization`; ADR rule rescoped
- [x] (must-fix) latched transient_local + change-only republish loses SonarInfo on rosbag2 bag splits — `docs/decisions/0009-sonar-info-message.md` (Claude Adversarial / Lens B) → publish contract now change + slow heartbeat (≤10 s)
- [x] (must-fix) `SourceRecord.calibration_ref` is a retired name; field moved to `StoreMetadata` (ADR-0005 #248 amendment) — msg + ADR (Governance + Lens B, cross-confirmed) → both citations fixed
- [x] (suggestion) FM effective pulse length unrecoverable without bandwidth — `float32[] bandwidths` added (Lens B)
- [x] (suggestion) `REFERENCE_DB_RE_1UPA` re-conflated scale into the reference axis — renamed `REFERENCE_ABSOLUTE_1UPA`, axes orthogonal (Lens A)
- [x] (suggestion) empty `tx_signal_types` fallback + parallel-array length contract unstated — documented (Lens A + Lens B)
- [x] (suggestion) four "absent" encodings without a stated convention; `offset` meaning when `scale==0` — conventions block + offset-ignored note (Lens A)
- [x] (suggestion) future bag-migration (.bmr) debt of the local-then-upstream path unowned — ADR Consequences now owns it (Lens B)
- [x] (suggestion) `.agents/README.md` message count stale (39 → 46) (Governance)
- [x] (suggestion) `docs/interfaces.md` missing SonarInfo listing (ADR-0008 messages set precedent) — section added (Governance)

Static analysis: no linter profile configured for these file types (`.msg`, CMake, `.md`) — content review only. Plan drift: none (diff matches plan; plan updated inline with review-round-1 changes per plan-sync rules).
