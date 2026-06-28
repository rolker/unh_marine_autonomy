---
issue: 236
---

# Issue #236 — Publish structured TaskFeedback for operator stations (P1 boat-side of camp#123)

## Plan Authored
**Status**: complete
**When**: 2026-06-28 08:20 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-236/plan.md` at `6760606`
**Branch**: feature/issue-236 at `6760606`
**Phases**: single

### Open questions
- [ ] Topic name `marine/status/mission_tasks` — confirm convention in review-plan (vs e.g. `marine/status/tasks`).

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-28 08:51 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved

**Branch**: feature/issue-236 at `b47dd86`
**Mode**: pre-push
**Depth**: Standard (reason: adds a new published topic interface)
**Must-fix**: 0 | **Suggestions**: 4
**Round**: 1 | **Ship**: recommended — no must-fix; cadence suggestions point opposite ways, tune after measuring bridge budget

### Findings
- [ ] (suggestion) Structured TaskFeedback (with poses) on Heartbeat cadence may strain cell link — consider decimate/publish-on-change; measure on bridge — `camp_interface.py:178,202`
- [ ] (suggestion) Lifecycle publishers not destroyed on on_cleanup → reconfigure leak (parity w/ existing status_publisher) — `camp_interface.py:135`
- [ ] (suggestion) navigatorDone terminal message is one-shot; dropped UDP packet leaves CAMP on stale in-progress state (parity w/ Heartbeat-done) — `camp_interface.py:202`
- [ ] (suggestion) task_feedback.tasks = result.tasks aliases result list — benign today, guard if future code mutates before publish — `camp_interface.py:198`
