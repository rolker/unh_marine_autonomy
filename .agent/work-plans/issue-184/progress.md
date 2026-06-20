---
issue: 184
---

# Issue #184 — marine_sidescan_mosaic: bag→Tier-1 importer + Tier-2 projectors + best-source processed store

## Local Review (Pre-Push) — 2026-06-20

**Reviewer:** sandboxed sub-agent running `review-code` (pre-push, deep), worktree-scoped, 2 adversarial lenses (logic/edge + systemic/durability). Copilot off (default).
**Subject:** `feature/issue-184` @ `4138c6d` (base `jazzy`) — 17 files (+1354 −51), package `marine_sidescan_mosaic`.
**Verdict:** changes-requested → **all 5 must-fixes + the actionable suggestions addressed in-package.**

### Must-fix — RESOLVED
1. **Tier-1 unbounded `resize(n)` → OOM on corrupt stream** → `kMaxSamplesPerPing` ceiling + reject (`tier1.cpp`); test `RejectsBogusSampleCount`.
2. **Truncated record reported as clean EOF / half-valid ping** → all post-`stamp_ns` reads chain-checked; truncation returns false without populating (`tier1.cpp`); test `RejectsTruncatedRecordMidField`.
3. **Unguarded `stoi`/`stod` → `std::terminate` on bad CLI input** → `toInt`/`toDouble` (try/catch → usage error, exit 2) in all three executables. Verified: `--level notanumber` exits 2.
4. **Host-endian Tier-1 format, no endianness marker** → documented contract: the magic IS the endianness guard (a byte-swapped magic fails `readTier1Header`, so a mismatched host rejects rather than reads garbage); explicit-LE noted as the localized change if a BE host appears.
5. **`registry.json` hand-rolled, no escaping → invalid JSON poisons merges** → `jsonEscape` on all string fields. Verified: `campaign='site"A\test'` round-trips as valid JSON.

### Suggestions — applied
- **S1** local-vs-global source-id: guard the uint16 narrowing (`--source-id` in [1,65535]); comment ties cell=local-index vs registry=global-id (ADR-0005 D4).
- **S2** `assume_zero` collapses grazing quality → warn in `tier2_processed`.
- **S3** registry write-once vs ADR-0005 D8 append-only → `TODO(#179)` at `writeRegistry`.
- **S4** importer no output-integrity check → `out.flush()` + `good()` check, non-zero on disk-full truncation.
- **S6** `ecefPoseToGeoBeam` had no validity signal → added `GeoBeam::valid`; both tier2 tools skip degenerate-quaternion pings (`bad-pose` counter) instead of projecting due north.
- **S7** unused `<cstring>` / `<algorithm>` in the importer → removed.
- **S5** (accumulator quality-0 precondition) — already documented at the `add()` signature in `processed_accumulator.hpp`; left as-is.
- **S8** (argValue / per-sample loop duplication across the two tier2 tools) — acknowledged prototype debt; folds into the ADR-0006 D12 engine extraction. Not addressed now.

### Status
Build clean; gtest all green (tier1 5/0, projection 6/0, processed_accumulator 3/0, accumulator/normalizer unchanged). `ament_uncrustify` failures are the known local 0.78.1 version drift (flags untouched base files too) — CI's pinned version is the gate. Ready to push.
