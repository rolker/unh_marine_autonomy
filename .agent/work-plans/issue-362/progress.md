---
issue: 362
---

# Issue #362 — Field import: unh_marine_autonomy (2026-08-26..27)

## Integrated Review
**Status**: complete
**When**: 2026-09-03 15:04 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))

**PR**: #363 at `cba4d0c`
**Sources**: 3 (Copilot R1 @ `a4fafed`, Copilot R2 @ `cba4d0c`, CI rollup)
**Cross-source confirmations**: 0
**CI**: all-pass

### Findings
- [ ] (must-fix, Copilot R2) The PR contradicts itself in operator-facing text: `20f7b0a` wrote "Keep it STRICTLY below render_interval", `a4fafed` (same PR) states that reasoning "could not have worked" and replaced it with URL versioning. Superseded claim still stands in the launch-arg description, visible via `ros2 launch --show-args` — `marine_web_view/launch/web_view_launch.py:183-191`
- [ ] (must-fix, Copilot R1) A malformed manifest can leave a stale history layer on the map: `buildHistory()` returns false on bad zooms before any teardown, so `!buildHistory(meta) && historyLayer === null` is false when a layer exists, the catch never runs, and the readout updates from the broken manifest. Contradicts the catch block's own stated intent — `marine_web_view/web/index.html:826`
- [ ] (valid, Copilot R2) Catalog QoS: code is VOLATILE but `README.md:514` documents RELIABLE/TRANSIENT_LOCAL for this topic, and the comment's stated root cause ("the real fix is in the relay") is now known wrong — udp_bridge supports per-topic transient_local; the boat's bridge config never set it (rolker/unh_echoboats_project11#484). Copilot suggested a durability parameter; recommended instead to correct the comment, add the revert condition, and reconcile the README, matching camp#220. Operator decision on shape — `marine_web_view/marine_web_view/coverage_renderer.py:533`
- [ ] (valid, Copilot R1) `ThreadPoolExecutor(concurrency)` unclamped; `--concurrency 0` aborts with ValueError. Sibling `refresh_chart_tiles.py:771` clamps with `max(1, int(...))`. `--processes` has the same shape, unflagged — `marine_web_view/scripts/publish_history_tiles.py:586`
- [ ] (minor, Copilot R2) `historyText()` returns "0 days · " with a trailing separator when labels are missing/empty/scrubbed; reachable because `buildHistory` gates on zooms only — `marine_web_view/web/index.html:813`
- [ ] (minor, Copilot R2) `lock_dir()` lacks the `S_ISDIR` guard its sibling has. Tested: a symlink is currently rejected anyway because its mode is 0o777 and the group/world-writable check trips, so this is defense-in-depth plus a legible error rather than an open hole — `marine_web_view/scripts/publish_history_tiles.py:488`

### False positives
(none — all six verified against the code; F1 and F6 both correctly cite an in-repo precedent)
