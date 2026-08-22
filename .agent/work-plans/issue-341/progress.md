---
issue: 341
---

# Issue #341 — marine_web_view: public web view of live vessel state

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-22 23:17 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-341 at `84ac53f`
**Mode**: pre-push
**Depth**: Deep (reason: new ~1954-line package with a public S3/CloudFront publish path and an `aws` subprocess surface)
**Must-fix**: 1 | **Suggestions**: 4
**Round**: 1 | **Ship**: continue — one mechanical must-fix (launch/README track wiring); address and it ships

Static analysis clean (ament_flake8 + ament_pep257 + ament_copyright pass). Local Adversarial skipped (ollama not installed); Copilot off (default). gh offline — reviewed from code + commit messages; no work plan → Plan Drift skipped.

### Findings
- [ ] (must-fix) Launch file omits all `track_*` (and `msg_type`/`orientation_topic`/`platforms_topic`) params; README dry-run passes `track_local_path:=` which the launch file neither declares nor forwards, so the trail never renders in the documented local preview — `launch/state_renderer_launch.py:41` / `README.md:87`
- [ ] (suggestion) Leaflet CSS/JS loaded from unpkg.com without Subresource Integrity on a public page; add SRI or vendor Leaflet — `web/index.html:7,129`
- [ ] (suggestion) `RAMP`/`MAX_DEPTH`/`STEP` duplicated verbatim across Python and JS, synced only by a comment (drift risk, cf. ADR-0001) — `scripts/refresh_chart_tiles.py:93` / `web/index.html:176`
- [ ] (suggestion) `_subscribe_nav_sources` always subscribes discovered position topics as NavSatFix, ignoring `msg_type`; `msg_type:=geopoint` + discovered nav_source → AttributeError — `marine_web_view/state_renderer.py:294`
- [ ] (suggestion) Service-probe uses hardcoded `>1000` byte threshold while render loop uses configurable `--blank-bytes`; use `a.blank_bytes` in the probe — `scripts/refresh_chart_tiles.py:244`
