---
issue: 315
---

# Issue #315 — import_geotiff: vdatum-aware vertical conversion for reference imports (MLLW → ellipsoid)

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-20 17:10 -04:00
**By**: Claude Code Agent (Claude Fable 5)
**Verdict**: approved

**Branch**: feature/issue-315 at `f1766b4`
**Mode**: pre-push
**Depth**: Standard (reason: new conversion behavior feeding the nav costmap, ~400 lines / 11 files, C++)
**Must-fix**: 2 | **Suggestions**: 2
**Round**: 1 | **Ship**: recommended — all findings fixed in-session, 336 tests green across marine_bathymetry_store + marine_vertical_datum

Static analysis: covered by the packages' ament lint colcon tests (green). Plan drift: no work plan (direct implementation per operator direction). Copilot: off (default). Local model: skipped per operator guidance (--no-local while workspace#590 pends). Governance: README doc consequence carried; ADR-0010 D4 datum-at-import satisfied; Reference read-only gate untouched.

### Findings
- [x] (must-fix, cross-confirmed: Lens A + Lens B) --source-datum whole-record-replaced registry.json, blanking prior platform/sensor/survey/date (and conversely a later single-flag run erased the datum stamp) — field-wise merge — `marine_bathymetry_store/src/import_geotiff_main.cpp`
- [x] (must-fix, cross-confirmed: Lens A suggestion + Lens B must-fix) no-coverage hard error escaped main uncaught (std::terminate) — try/catch print + exit 1 on the normal-mode import/save — `marine_bathymetry_store/src/import_geotiff_main.cpp`
- [x] (suggestion, Lens B) vertical_datum_fn serial-invocation contract undocumented at the importer boundary — doc block added — `marine_bathymetry_store/include/marine_bathymetry_store/geotiff_import.hpp`
- [x] (suggestion, Lens B) per-pixel MHHW proj_trans discarded — VDatumConfig.want_mhhw MLLW-only mode, CLI opts in — `marine_vertical_datum`

### False positives
- (none)
