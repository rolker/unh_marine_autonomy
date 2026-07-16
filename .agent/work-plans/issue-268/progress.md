---
issue: 268
---

# Issue #268 — SonarInfo: angular-response TL provenance (tier-1 vs tier-2 curve self-description)

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-16 14:07 -0400
**By**: Claude Code Agent (Claude Fable 5)
**Verdict**: approved (suggestions applied in `8131611` same session)

**Branch**: feature/issue-268 at `f0c2470` (polish at `8131611`)
**Mode**: pre-push
**Depth**: Standard (reason: ADR edit — override-trigger file; 75 lines)
**Must-fix**: 0 | **Suggestions**: 4
**Round**: 1 | **Ship**: recommended — no must-fix; all suggestions applied and re-verified (build + 5/5 lint)

### Findings
- [x] (suggestion) dB/m vs dB/km unit flip between absorption fields deserves an in-message note — `marine_interfaces/msg/SonarInfo.msg` (Lens A) → note added citing cube#87's verbatim-alpha contract
- [x] (suggestion) plan promised a Consequences line (no-migration-needed) that was folded into the field-set bullet instead — ADR-0009 (Lens A) → line added adjacent to the bag-migration-debt bullet
- [x] (suggestion) amendment not marked per the ADR-0005/0007 convention (Status callout) — ADR-0009 (Governance) → **Amended 2026-07-16 (#268)** callout added
- [x] (suggestion) docs/interfaces.md summary mistyped the new fields as float32[] curves — fixed and regrouped with correction state (Governance)
- [ ] (carried to marine_tools#71) producer must set angular_response_absorption_db_per_m = NaN explicitly even with empty curves (float default 0.0 violates the sentinel convention)

Cleared by review: enum naming/values, TL formula agreement across msg/ADR/cube#87/derive tool, NaN-vs-0.0 divergence from the consumer struct (harmless — absorption only read on TL_REMOVED path), wire safety of mid-message insertion (zero curve-bearing bags, confirmed via producer test assertions), no build-time break for any consumer.
