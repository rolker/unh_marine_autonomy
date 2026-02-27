# Plan: Add ben_gazebo to simulation layer

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/91

## Context

`config/repos/simulation.repos` currently contains only `unh_marine_simulation`.
The `ben_gazebo` repo (Gz Harmonic simulation of BEN) needs to be added so it is
cloned into the simulation workspace layer by `vcs import`.

The repo exists at https://github.com/rolker/ben_gazebo with a `jazzy` default
branch. Its dependency `ben_description` is already in `config/repos/platforms.repos`
(lower layer), so build ordering is correct.

## Approach

1. **Add `ben_gazebo` entry to `config/repos/simulation.repos`** — Append the new
   repository entry after the existing `unh_marine_simulation` entry, following the
   same YAML format used in other `.repos` files.

2. **Verify YAML syntax** — Run `python3 -c "import yaml; yaml.safe_load(open('config/repos/simulation.repos'))"` to confirm valid YAML.

## Files to Change

| File | Change |
|------|--------|
| `config/repos/simulation.repos` | Add `ben_gazebo` entry (git, `rolker/ben_gazebo`, `jazzy`) |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Simulation-First Validation (project) | Directly supports this — adds BEN's simulation capability to the layer |
| Only what's needed | Single config entry, nothing else |
| A change includes its consequences | No downstream files reference `simulation.repos` statically — `validate_workspace.py` reads `.repos` files dynamically |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0002 — Worktree isolation | Yes | Working in `feature/issue-91` worktree, PR targets `jazzy` |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `config/repos/simulation.repos` | Nothing — consumed dynamically by `vcs import` and `validate_workspace.py` | N/A |

## Open Questions

None — the change is straightforward.

## Estimated Scope

Single PR, single commit.
