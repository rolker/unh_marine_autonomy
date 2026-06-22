---
issue: 203
---

# Issue #203 — marine_bathymetry_store: export nlohmann_json (and GDAL) deps

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-21 11:27 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved
**Round**: 1
**Ship**: recommended

**Branch**: feature/issue-203 at `f3a384f`
**Mode**: pre-push
**Depth**: Light (reason: one-line ament_export_dependencies change; CMake export only)
**Must-fix**: 0 | **Suggestions**: 0

Host self-review (full adversarial dispatch disproportionate for a 1-line export change).
Verified: the generated ament_cmake_export_dependencies-extras.cmake now lists
`nlohmann_json;GDAL` (was missing them); store builds clean; gtests pass (the 3 test.sh
failures are the known local uncrustify-0.78.1 drift on unmodified base files). The change
only ADDS exports consumers already needed via the static lib's $<LINK_ONLY> propagation —
no interface widening risk. Follow-up: remove cube_bathymetry's find_package(nlohmann_json)
workaround once this merges + syncs (noted in #57's review entry / unh_marine_autonomy#203).

### Findings
- [ ] No issues found. LGTM.
