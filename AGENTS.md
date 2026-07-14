# AGENTS.md — unh_marine_autonomy

Instructions for AI agents working in this repository — including **GitHub
Copilot code review**, which reads this file when reviewing PRs. Coding
agents: the deep guide (packages, layout, pitfalls) is
[`.agents/README.md`](.agents/README.md); read it before making changes.

## Workspace Rules

This repo is developed inside a
[ROS 2 Agent Workspace](https://github.com/rolker/ros2_agent_workspace).
The workspace root `AGENTS.md` carries the full shared rules (worktree
isolation, issue-first policy, commit conventions, AI signatures). This file
**references** those rules and adds repo-specific context only — it must
never restate or fork them.

## Quality Standard

This is software for autonomous robot boats operating on open water.
Robustness is not optional.

- Fix bugs completely: add the test, handle the edge case, check the
  lifecycle transition.
- Concerns about error handling, silent failures, stale data, or missing
  validation are not nits — flag them unless the failure mode genuinely
  cannot occur. "Config is under our control" and "pathological input" are
  not blanket dismissals; field configs change under pressure.
- A change includes its consequences: tests, documentation, and dependent
  references update in the same PR.

## Reviewing PRs

- If the PR carries a work plan (`.agent/work-plans/issue-<N>/plan.md` or a
  plan in the PR body), the plan is kept **in sync with the implementation
  as it evolves** — an implementation that matches the current plan text is
  not "plan drift", even if the plan changed after the PR opened.
- Verify claims against source: parameters, topics, services, and message
  types in docs must match the code.

## Review Context — unh_marine_autonomy

- **Safety-critical helm chain**: `helm_manager`, `command_bridge`, and the
  mission manager sit between Nav2 and the live boat's thrusters; lifecycle
  transitions and failure paths deserve the strictest review.
- **Survey data stores never lose data**: in the store packages
  (`marine_tiled_raster_store`, `marine_bathymetry_store`,
  `marine_mbes_backscatter_store`, …) cache/RAM eviction must persist to the
  durable store *before* dropping from memory — "evict-and-lose" designs are
  rejected on principle, even behind config flags.
- **`marine_interfaces` messages are cross-repo contracts**: consumers live
  in other repos (CAMP, rqt tools, platform configs) — grep the whole
  workspace, not just this repo, before changing any message or public API.
- **`bathymetry_layer` costmap fixes must be scale-independent**: the global
  costmap's size is an operational knob (it can grow for longer transits) —
  shrinking it is not an acceptable performance fix.
- **Repo docs**: `docs/sonar_ecosystem.md` is the living big-picture map of
  the sonar data ecosystem; significant store/pipeline changes should keep
  it current (a change includes its consequences).
