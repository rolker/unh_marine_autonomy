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
- [x] (suggestion) `--name` flows unsanitized into the local path and S3 key in the tile script (`../`/absolute escapes workdir or rewrites another prefix) — add a charset guard — `marine_web_view/scripts/refresh_chart_tiles.py:305`
- [x] (suggestion) index.html hardcodes the CCOM live service 20230922 while the pre-render script picks the newest, and every public viewer pan hits gis.ccom.unh.edu until the #342 tile swap — gate public launch on that swap — `marine_web_view/web/index.html:243`

## Implementation
**Status**: complete
**When**: 2026-08-22 23:49 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-341 at `1b577ba`
**Addressed**: Local Review (Pre-Push) of 2026-08-22 23:40 +00:00 at `7ced83e` (5 open findings — 1 must-fix, 4 suggestions)
**Commits**: 889fa7b, 6837cad, f9ddd2b, 7527020, 1b577ba

### Actions
- [x] (must-fix) `_on_fix` no longer assigns `self._fix = msg` until the newest-stamp guard passes, so an out-of-order fix from a second position sub can't jump the published point backward — `marine_web_view/marine_web_view/state_renderer.py:250` → `889fa7b`
- [x] (suggestion) Oldest `TRACK_BANDS` entry is now a catch-all (`float('inf')`) so the whole `track_seconds` window renders instead of being clipped at the hardcoded 14400 s — `marine_web_view/marine_web_view/state_renderer.py:74` → `6837cad`
- [x] (suggestion) Dry-run position/track writes go through a new `_write_atomic` (same-dir temp + `os.replace`), so the local http.server never reads a truncated file — `marine_web_view/marine_web_view/state_renderer.py:419,457` → `f9ddd2b`
- [x] (suggestion) `--name` is validated against `[A-Za-z0-9][A-Za-z0-9._-]*` right after parse, blocking `../`/absolute/slash escapes of the workdir and S3 key prefix — `marine_web_view/scripts/refresh_chart_tiles.py:305` → `7527020`
- [x] (suggestion) index.html now fires a runtime `console.warn` while `IMG` still points at the live CCOM host, turning the passive "pre-render before public (uma#342)" comment into a loud public-launch gate; the pinned 20230922 date is documented as a deliberate placeholder retired by the #342 swap — `marine_web_view/web/index.html:243` → `1b577ba`

Sanity pass (not a full re-review): `python3 -m py_compile` clean; ament flake8 clean on both changed Python files; `test_ramp_sync`, `test_flake8`, `test_pep257` all pass (3 passed). No findings deferred.

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off to a fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 341 --skill review-code

## Integrated Review
**Status**: complete
**When**: 2026-08-22 22:23 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))

**PR**: #346 at `849b149`
**Sources**: 3 (Copilot @ `849b149`, Local Review (Pre-Push) @ `84ac53f` and `7ced83e`, CI rollup)
**Cross-source confirmations**: 0
**CI**: all-pass (build: success, copilot-pull-request-reviewer: success)

Copilot reviewed the current head, so none of its findings are stale. The two
prior Local Reviews sit at earlier SHAs with all 20 findings already checked
off, and they touch different code, so nothing is cross-confirmed this round --
Copilot reached parts of the diff the local rounds did not.

### Findings
- [x] (valid, Copilot) `_history` is a plain list trimmed by full-list
  comprehension on every callback once the window is full. At the default
  `track_seconds` of 14400 s and the simulator's 5 Hz nav that is ~72k tuples
  rebuilt 5x/second inside the subscriber callback. Use `collections.deque`
  with `popleft()` (O(1)); it is a drop-in here because `decimate_track`
  only iterates and indexes `[0]`/`[-1]`. Degrades with runtime and message
  rate, so short test runs never surface it --
  `marine_web_view/marine_web_view/state_renderer.py:266`
- [x] (valid, Copilot) Relief (hillshade) tile requests set no explicit
  `interpolation`, relying on the ArcGIS server default, while the code
  comments state bilinear is deliberate for hillshade and nearest-neighbour for
  the banded colours. Only the Bathy layer sets it (line 293). Make it explicit
  so a server-default change cannot silently switch it --
  `marine_web_view/web/index.html:325`
- [x] (valid, Copilot) The trailing comment `// renderer writes at 1 Hz` sits
  on `TRACK_MS = 30000`, not on `POLL_MS = 1000` where it belongs -- it
  migrated when the constant block was reformatted to add the track poll. Reads
  as though the track is fetched at 1 Hz, which is exactly the cadence/cost
  figure someone would tune from --
  `marine_web_view/web/index.html:150`

### False positives
- None. All three Copilot findings were verified against the current code and
  hold.

## Implementation
**Status**: complete
**When**: 2026-08-23 02:30 +00:00
**By**: Claude Code Agent (Claude Opus)

**PR**: #346 at `90454bc`
**Addressed**: Integrated Review of 2026-08-22 22:23 -04:00 at `849b149` (3 open findings, all valid Copilot, no cross-source confirmations)
**Commits**: `40ee52f`, `e7ad0e8`, `90454bc`

All three findings actioned — none deferred. Each was verified against the
current source before the fix. Post-fix checks: `marine_web_view` package tests
pass (`ament_flake8`, `ament_pep257`, `ament_copyright`, `test_ramp_sync` — 4
passed); `state_renderer.py` byte-compiles and lints clean at ament's 99-col
config; the deque operations `decimate_track` and the trim rely on (`list()`,
iteration, `[0]`/`[-1]`, `len`, `append`, `popleft`) were exercised standalone.
Not a full `review-code` — the next re-review is the quality gate.

### Actions
- [x] `_history` is now a `collections.deque`, trimmed from the left with a
  `popleft()` loop instead of an O(n) full-list comprehension rebuilt inside the
  subscriber callback; `decimate_track` and every other reader are unchanged
  (they only iterate/index `[0]`/`[-1]` and take `len`) — `marine_web_view/marine_web_view/state_renderer.py:222,265` (`40ee52f`)
- [x] Relief (hillshade) tiles now request `interpolation=RSP_BilinearInterpolation`
  explicitly, so the smooth-relief resample no longer depends on the ArcGIS
  server default; a comment contrasts it with the Bathy layer's deliberate
  nearest-neighbour — `marine_web_view/web/index.html:325` (`e7ad0e8`)
- [x] Moved the `renderer writes at 1 Hz` cadence note off `TRACK_MS` (30 s) and
  onto `POLL_MS` (1000 ms), where the 1 Hz point cadence belongs; `TRACK_MS`
  now carries its own accurate "every 30 s" note — `marine_web_view/web/index.html:147,150` (`90454bc`)

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off to a fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 341 --skill review-code
