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
- [x] (must-fix) Launch file omits all `track_*` (and `msg_type`/`orientation_topic`/`platforms_topic`) params; README dry-run passes `track_local_path:=` which the launch file neither declares nor forwards, so the trail never renders in the documented local preview — `launch/state_renderer_launch.py:41` / `README.md:87`
- [x] (suggestion) Leaflet CSS/JS loaded from unpkg.com without Subresource Integrity on a public page; add SRI or vendor Leaflet — `web/index.html:7,129`
- [x] (suggestion) `RAMP`/`MAX_DEPTH`/`STEP` duplicated verbatim across Python and JS, synced only by a comment (drift risk, cf. ADR-0001) — `scripts/refresh_chart_tiles.py:93` / `web/index.html:176`
- [x] (suggestion) `_subscribe_nav_sources` always subscribes discovered position topics as NavSatFix, ignoring `msg_type`; `msg_type:=geopoint` + discovered nav_source → AttributeError — `marine_web_view/state_renderer.py:294`
- [x] (suggestion) Service-probe uses hardcoded `>1000` byte threshold while render loop uses configurable `--blank-bytes`; use `a.blank_bytes` in the probe — `scripts/refresh_chart_tiles.py:244`

## Implementation
**Status**: complete
**When**: 2026-08-22 23:28 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-341 at `7e81ba7`
**Addressed**: Local Review (Pre-Push) of 2026-08-22 23:17 +00:00 at `84ac53f` (1 must-fix + 4 suggestions, all open)
**Commits**: `6df9c49`, `6688062`, `c703173`, `4ec33de`, `7e81ba7`

All five findings actioned — none deferred. Post-fix checks: `marine_web_view`
package tests pass (`ament_flake8`, `ament_pep257`, `ament_copyright`, and the
new `test_ramp_sync`); each touched Python file byte-compiles. Not a full
`review-code` — the next re-review is the quality gate.

### Actions
- [x] (must-fix) Launch file now declares and forwards every `StateRenderer` parameter — `track_*`, `msg_type`, `orientation_topic`, `platforms_topic`, and the `vessel_*` fallbacks — so the documented `track_local_path:=` dry-run reaches the node and the trail renders — `launch/state_renderer_launch.py` (`6df9c49`)
- [x] (suggestion) Pinned both Leaflet 1.9.4 CDN tags with SRI `integrity` + `crossorigin` (hashes verified against the downloaded assets) — `web/index.html:7,131` (`6688062`)
- [x] (suggestion) Added `test/test_ramp_sync.py`, which imports the script's `RAMP`/`MAX_DEPTH`/`STEP` and compares them to the values parsed from `index.html`, turning the sync comment into an enforced check; updated the sync notes in the script, page, and README to point at it — `marine_web_view/test/test_ramp_sync.py` (`c703173`)
- [x] (suggestion) `_subscribe_nav_sources` now subscribes discovered position topics with the configured `fix_type` instead of a hardcoded `NavSatFix`, so `msg_type:=geopoint` no longer feeds `_on_fix` an unreadable message — `marine_web_view/state_renderer.py:294` (`4ec33de`)
- [x] (suggestion) `pick_service` takes `blank_bytes` and probes with `> blank_bytes`, matching the render loop's `--blank-bytes` skip rule so the two decisions cannot disagree — `scripts/refresh_chart_tiles.py:244` (`7e81ba7`)

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-22 23:40 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-341 at `7ced83e`
**Mode**: pre-push
**Depth**: Deep (reason: new ~2100-line package with a public S3/CloudFront publish path and an `aws` subprocess surface)
**Must-fix**: 1 | **Suggestions**: 4
**Round**: 2 | **Ship**: recommended — one mechanical must-fix (stale-fix guard); address and it ships, must-fix not rising vs round 1

Static analysis clean (ament_flake8 + ament_pep257 + ament_copyright + test_ramp_sync all pass under the package's own test config). Two Claude Adversarial passes (Lens A logic + Lens B systemic). Local Adversarial skipped (ollama not installed); Copilot off (default). gh offline — reviewed from code + commit messages; no work plan → Plan Drift skipped. Round-1's 1 must-fix + 4 suggestions were all addressed; this round's must-fix is a new deeper finding.

### Findings
- [x] (must-fix) `_on_fix` sets `self._fix = msg` before the newest-stamp guard, so an out-of-order fix from one of the multiple position subs is published, jumping the public point backward — violates the stated "newest wins" contract — `marine_web_view/marine_web_view/state_renderer.py:250`
- [x] (suggestion) `decimate_track` has no catch-all band; points older than the hardcoded 14400 s are dropped even when `track_seconds` is set larger, so a longer configured track never renders — `marine_web_view/marine_web_view/state_renderer.py:74,136`
- [x] (suggestion) Dry-run local writes are non-atomic (`open('w')`); the local http.server preview can read truncated JSON mid-write — use tmp+os.replace — `marine_web_view/marine_web_view/state_renderer.py:419,457`
- [ ] (suggestion) `--name` flows unsanitized into the local path and S3 key in the tile script (`../`/absolute escapes workdir or rewrites another prefix) — add a charset guard — `marine_web_view/scripts/refresh_chart_tiles.py:305`
- [ ] (suggestion) index.html hardcodes the CCOM live service 20230922 while the pre-render script picks the newest, and every public viewer pan hits gis.ccom.unh.edu until the #342 tile swap — gate public launch on that swap — `marine_web_view/web/index.html:243`
