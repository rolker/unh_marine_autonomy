# Plan: Create the rolling manifest branch (rolling-primary model)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/384

## Context

This repo doubles as the workspace **manifest repo** (Pattern B): `config/bootstrap.yaml`,
`config/layers.txt`, `config/optional_layers.txt`, and `config/repos/*.repos` live here and
are read by `ros2_agent_workspace`'s `setup_layers.sh` (jazzy consumer, unaffected — it
`grep`s specific keys and ignores unknown ones) and, going forward, by the distro-aware
`rolker/agent_workspace` adapter (rolling consumer, gated on `distro:` support). Today only
`jazzy` exists; `bootstrap.yaml` has `git_url`, `branch: jazzy`, `layer: core` — no `distro`
key. Deployed machines pin `configs/project_bootstrap.url` at `branch: jazzy` and never read
other branches, so `jazzy`'s manifest layout must not change.

**Branch-creation mechanics (resolves the Issue Review's open action item):**

1. The host — not this agent — pushes a new `rolling` branch to `origin`, created from the
   **current tip of `jazzy`**, as a plain branch-creation push with no content change. This
   step happens outside this worktree/PR entirely; it is a prerequisite, not a plan step this
   agent executes (agents don't push, per the sub-agent handoff contract).
2. This worktree (`feature/issue-384`) was already forked from that same `jazzy` tip (see
   `git status`: 1 commit ahead of `origin/jazzy`, no other divergence). Once `rolling` exists,
   its merge-base with `feature/issue-384` is identical to `feature/issue-384`'s merge-base
   with `jazzy` today — **no new worktree and no rebase is needed**, provided `jazzy` has not
   moved since `rolling` was cut. Verify this explicitly before the PR is opened:
   `git merge-base feature/issue-384 origin/rolling` must equal
   `git merge-base feature/issue-384 origin/jazzy`. If `jazzy` moved in the interim (someone
   merged another PR to `jazzy` after `rolling` was cut but before this one), rebase
   `feature/issue-384` onto `origin/rolling` before opening the PR so the diff doesn't carry
   unrelated `jazzy` commits into `rolling`.
3. The PR from `feature/issue-384` must target **`rolling`**, not `jazzy` — `gh pr create`
   defaults to the repo's default branch (`jazzy`), so the base must be passed explicitly:
   `gh pr create --base rolling ...`. This is a step for the PR-opening phase (later in the
   lifecycle, not part of this local-first plan-task run), but it's recorded here so the
   implementer/orchestrator doesn't default into a `jazzy`-targeted PR by omission.
4. `jazzy` itself is never touched by this work — no commits, no merges, no manifest edits.

## Approach

1. **`config/bootstrap.yaml`** — add `distro: rolling` (new key) and change `branch: jazzy` →
   `branch: rolling`. `git_url` and `layer: core` are unchanged.
2. **`config/repos/core.repos`** — pin `unh_marine_autonomy`'s own entry at `version: rolling`
   (required: Pattern B means the manifest branch and this repo's own package source must
   agree, per the issue's explicit "Done when"). Every other entry in every `.repos` file
   keeps its current (jazzy-branch) `version:` pin unchanged — see the per-entry table below
   for the evidence behind that default and the two exceptions.
3. **`config/layers.txt`, `config/optional_layers.txt`** — copied unchanged; the layer
   structure and the `site` optional-layer marking are distro-independent.
4. **`.agents/README.md`** — update the "Manifest Repo Role" section (currently silent on
   branches) to state the jazzy/rolling split, the new `distro:` bootstrap key, and that
   per-repo `.repos` pins on `rolling` default to the same ref as `jazzy` until a real port
   lands (stale-docs item, not a candidate — this section becomes inaccurate the moment
   `rolling` exists with a different `branch:`/`distro:` than what it currently documents).
5. **Verify branch-protection / CI setup on `rolling`** (check only — no changes; both CI
   config and branch-protection changes are Ask-First per `AGENTS.md`):
   - The repo's push/PR-required ruleset (`require_pr`, id `11881731`) targets
     `ref_name.include: ["~DEFAULT_BRANCH"]` — GitHub's token for "whichever branch is
     currently the repository default," which is `jazzy` today. **This means `rolling` will
     have *no* branch-protection ruleset applied to it** (direct pushes, force-push, and
     deletion all permitted) until either `rolling` becomes the default branch or the ruleset
     is edited to name `rolling` explicitly. Confirmed today: `jazzy`'s legacy
     branches/protection API returns 404 ("not protected") because protection lives in this
     ruleset, not the legacy API — and the ruleset's condition is default-branch-relative, not
     a fixed name.
   - `.github/workflows/ros-base-docker.yml` triggers only on
     `push: branches: [jazzy]` / `pull_request: branches: [jazzy]`. **PRs targeting `rolling`
     will not run this workflow at all** until its trigger list includes `rolling` (or is
     widened).
   - Both gaps are recorded as **Open Questions** below rather than fixed in this plan — see
     AGENTS.md's Ask-First list ("Changing CI or branch protection configuration").

### Per-entry `.repos` pin decisions

Evidence gathered via `gh api repos/<owner>/<repo>/branches/rolling` (existence check) and
`gh api repos/<owner>/<repo>` (`default_branch`) against all 44 entries across the six
dependent `.repos` files, 2026-09-14. Result: **no dependency repo has a `rolling` branch
intended for this initiative** (see `ros2launch_gui` exception below), so the default
proposal for every entry except `unh_marine_autonomy` itself is **unchanged from jazzy** —
"same ref as jazzy where no port is needed," per the issue's own third option. This defers
all real Rolling porting work to the separate per-repo issues the Issue Review confirmed are
out of scope here; it only makes every entry *resolvable* (a valid ref exists), which is all
the issue's "Done when" requires.

| `.repos` file | Repo | jazzy pin | rolling proposal | Rationale |
|---|---|---|---|---|
| core | manda_coverage | jazzy | jazzy (unchanged) | No `rolling` branch on origin; no known jazzy-specific code |
| core | marine_ais | jazzy | jazzy (unchanged) | " |
| core | marine_control | jazzy | jazzy (unchanged) | " |
| core | udp_bridge | jazzy | jazzy (unchanged) | " |
| core | unh_marine_navigation | jazzy | jazzy (unchanged) | " |
| core | s57_tools | jazzy | jazzy (unchanged) | " |
| core | **unh_marine_autonomy** | jazzy | **rolling** | Required — Pattern B, this branch's own source must match |
| platforms | ben_project11 | jazzy | jazzy (unchanged) | No `rolling` branch |
| platforms | ben_description | jazzy | jazzy (unchanged) | No `rolling` branch (has a `main`, unused here) |
| platforms | mru_transform | jazzy | jazzy (unchanged) | No `rolling` branch |
| platforms | seafloor_echoboat_project11 | jazzy | jazzy (unchanged) | No `rolling` branch |
| platforms | unh_echoboats_project11 | jazzy | jazzy (unchanged) | No `rolling` branch |
| platforms | lr30_project11 | jazzy | jazzy (unchanged) | No `rolling` branch |
| platforms | drix_description | jazzy | jazzy (unchanged) | No `rolling` branch |
| platforms | mobile_lab | jazzy | jazzy (unchanged) | No `rolling` branch |
| sensors | unh_marine_radar | jazzy | jazzy (unchanged) | No `rolling` branch |
| sensors | cube_bathymetry | jazzy | jazzy (unchanged) | No `rolling` branch |
| sensors | unh_marine_perception | jazzy | jazzy (unchanged) | No `rolling` branch |
| sensors | marine_tools | jazzy | jazzy (unchanged) | No `rolling` branch |
| sensors | imagenex_deltat | jazzy | jazzy (unchanged) | No `rolling` branch |
| sensors | edgetech_sonar | jazzy | jazzy (unchanged) | No `rolling` branch |
| sensors | starlink_stats_ros | jazzy | jazzy (unchanged) | No `rolling` branch (has a `main`, unused here) |
| sensors | ros2_network_monitor | jazzy | jazzy (unchanged) | No `rolling` branch |
| simulation | ben_gazebo | jazzy | jazzy (unchanged) | No `rolling` branch (has a `main`, unused here) |
| simulation | drix_gazebo | jazzy | jazzy (unchanged) | No `rolling` branch |
| simulation | unh_marine_simulation | jazzy | jazzy (unchanged) | No `rolling` branch |
| site | ccomjhc_project11 | jazzy | jazzy (unchanged) | No `rolling` branch; field-mode/optional layer, lowest priority to port |
| ui | camp | jazzy | jazzy (unchanged) | No `rolling` branch |
| ui | rqt_operator_tools | jazzy | jazzy (unchanged) | No `rolling` branch |
| ui | rqt_marine_radar | jazzy | jazzy (unchanged) | No `rolling` branch (default branch is `noetic`; `jazzy` ref exists and is what's pinned today) |
| ui | rqt_udp_bridge | jazzy | jazzy (unchanged) | No `rolling` branch |
| ui | rviz_sonar_image | jazzy | jazzy (unchanged) | No `rolling` branch |
| ui | marine_perception_tools | jazzy | jazzy (unchanged) | No `rolling` branch |
| ui | marine_colormap | jazzy | jazzy (unchanged) | No `rolling` branch |
| ui | marine_sonar_widgets | jazzy | jazzy (unchanged) | No `rolling` branch |
| underlay | ros2launch_gui | jazzy | jazzy (unchanged) — **see open question** | A branch literally named `rolling` exists, but its tip (`6e316f2`, 2026-02-17) predates the `jazzy` branch's tip (`6afbc8f`, 2026-08-24) by ~6 months and many merged PRs — it reads as stale/unrelated to this initiative, not a real Rolling port. Proposing NOT to use it. |
| underlay | ros2launch_session | jazzy | jazzy (unchanged) | No `rolling` branch |
| underlay | geographic_info | ros2 | ros2 (unchanged) | Upstream org (`ros-geographic-info`), no `rolling` branch; `ros2` is already distro-generic, not jazzy-specific |
| underlay | audio_common | port_sound_play_h_to_ros2 | unchanged | Custom feature branch, unrelated to distro; no `rolling` branch |
| underlay | detection_visualizer | support_rotation | unchanged | Custom feature branch, unrelated to distro; no `rolling` branch |
| underlay | nmea_navsat_driver | add_pashr | unchanged | Custom feature branch, unrelated to distro; no `rolling` branch |
| underlay | norbit | jazzy | jazzy (unchanged) | No `rolling` branch |
| underlay | ros2sonic | jazzy | jazzy (unchanged) | No `rolling` branch |
| underlay | vrx | add-santorini-seafloor | unchanged | Custom feature branch, unrelated to distro; no `rolling` branch |

### `bootstrap.yaml` keys the adapter is expected to read

| Key | jazzy value | rolling value | Consumer |
|---|---|---|---|
| `git_url` | `https://github.com/rolker/unh_marine_autonomy.git` | unchanged | Both `setup_layers.sh` (jazzy) and the distro-aware adapter (rolling) |
| `branch` | `jazzy` | `rolling` | Both — tells the consumer which branch of this repo to clone as the manifest/core-layer checkout |
| `layer` | `core` | unchanged | Both |
| `distro` | *(absent)* | `rolling` | **New.** `setup_layers.sh` ignores it (confirmed: it parses `bootstrap.yaml` via per-key `grep '^git_url:'` / `'^branch:'` / `'^layer:'`, so an unrecognized `distro:` line is silently skipped). The **only** consumer is the not-yet-built distro-aware `rolker/agent_workspace` adapter. This plan cannot verify that adapter's parser from this worktree — see Open Questions. |

## Files to Change

| File | Change |
|------|--------|
| `config/bootstrap.yaml` | Add `distro: rolling`; change `branch: jazzy` → `branch: rolling` |
| `config/repos/core.repos` | Change `unh_marine_autonomy` entry's `version:` from `jazzy` to `rolling`; all other entries unchanged |
| `config/repos/platforms.repos` | No content change (copied as-is onto the `rolling` branch) |
| `config/repos/sensors.repos` | No content change |
| `config/repos/simulation.repos` | No content change |
| `config/repos/site.repos` | No content change |
| `config/repos/ui.repos` | No content change |
| `config/repos/underlay.repos` | No content change |
| `config/layers.txt` | No content change |
| `config/optional_layers.txt` | No content change |
| `.agents/README.md` | Update "Manifest Repo Role" section: document the jazzy/rolling split and the `distro:` key |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Standards Compliance (ROS 2 community standards) | Directly implements the workspace's "conventions target Rolling" posture (workspace ADR-0008); this is the manifest-side enablement, not a code port. |
| Modularity and Decoupling | No package code touched — this is manifest/config only, isolated from the packages it references. |
| Iterative, Validated Evolution | Deliberately incremental: creates the manifest scaffold and defers per-repo Rolling ports to their own future issues, matching the issue's explicit "Done when" (manifest resolvable; the workspace-side build issue validates it later). |
| Safety First / Simulation-First | Not implicated — no vehicle-facing code changes. Deployed (jazzy) machines are explicitly unaffected (see branch-creation mechanics). |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0002 (worktree isolation, workspace repo) | Yes, with a resolved gap | Branch-creation mechanics section above records the host-side `rolling` creation and the `--base rolling` PR requirement, closing the Issue Review's open action item. |
| ADR-0008 (ROS 2 conventions target Rolling, workspace repo) | Yes | This issue is exactly the follow-through ADR-0008 anticipated ("See #261 for multi-distro support plans"); no conflict. |
| workspace ADR-0001 (Adopt ADRs) | Indirectly | The rolling-primary decision itself still lacks a durable ADR (see Open Questions / Consequences) — this plan does not write that ADR (operator decision: propose as follow-up issue, not in-scope here). |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `config/bootstrap.yaml` `branch:`/`distro:` on `rolling` | `.agents/README.md` Manifest Repo Role section | Yes — step 4 |
| Create `rolling` with no branch-protection ruleset coverage | Branch-protection ruleset (`require_pr`) to explicitly include `rolling` | No — Ask-First (CI/protection config); recorded as Open Question |
| Create `rolling` with no CI trigger coverage | `.github/workflows/ros-base-docker.yml` `branches:` lists | No — Ask-First (CI config); recorded as Open Question |
| rolling-primary model adopted | An ADR capturing the decision (currently only a comment on `rolker/agent_workspace#172`) | No — operator decision: propose as a follow-up issue, not part of this change |
| `rolling`'s `.repos` entries defaulting to jazzy refs | Per-repo Rolling port issues, once each maintainer decides a `version:` pin that's more than "same as jazzy" | No — explicitly deferred by the issue itself to separate future issues |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `.agents/README.md`'s "Manifest Repo Role" section
  is silent on branches entirely today; once `rolling` exists with a different `branch:` and a
  new `distro:` key, that silence becomes actively misleading (a reader would assume one
  manifest, one branch). Update it in this PR (see Files to Change).
- **Agent-instruction candidates** (proposals only): the "check branch-protection/CI coverage
  before targeting a new base branch for PRs" pattern discovered in this plan (a
  `~DEFAULT_BRANCH`-scoped ruleset silently does not cover a new non-default branch) may be
  worth a line in this workspace's `.agent/knowledge/` or `AGENTS.md` Worktree Workflow
  section — it will recur for any future non-default long-lived branch (role variants, per the
  issue's "Role variants... are a separate axis"). Not applied here — operator decides.

## Open Questions

1. **Branch-protection coverage for `rolling`**: the `require_pr` ruleset only targets
   `~DEFAULT_BRANCH` (currently `jazzy`), so `rolling` will be created with **no** push
   protection. Should the ruleset be edited to explicitly add `rolling` to its `ref_name`
   include list now, or is an unprotected interim acceptable (relying on the
   agent/workspace-convention worktree discipline rather than GitHub enforcement) until
   `rolling` becomes the default branch as part of a later rolling-primary migration step?
   This plan does not change the ruleset (Ask-First).
2. **CI trigger coverage for `rolling`**: `.github/workflows/ros-base-docker.yml` only
   triggers on `branches: [jazzy]`. Should its trigger list be widened to include `rolling`
   now (so PRs into `rolling` get CI), and if so, in this PR or a fast-follow? This plan does
   not change the workflow (Ask-First).
3. **`ros2launch_gui`'s pre-existing `rolling` branch**: confirm it is unrelated/stale (last
   commit 2026-02-17, well behind `jazzy`'s 2026-08-24 tip) rather than an intentional prior
   Rolling-port attempt this plan should be reusing or reconciling with.
4. **`distro:` key parsing in the consumer adapter**: this plan names the expected key/value
   (`distro: rolling`) based on the issue text alone — it cannot verify the not-yet-built
   `rolker/agent_workspace` adapter's actual parser (key name, nesting, accepted values) from
   this worktree. The plan-review phase for this issue runs in that consumer repo per the
   operator's routing decision; it should confirm the key contract matches what that adapter
   will actually read before implementation, or flag a mismatch now while it's still a
   one-line change.
5. **ADR for the rolling-primary decision**: tracked as a recommendation (Consequences table)
   to file as a follow-up issue — not filed as part of this plan-task run. Should it be filed
   in `unh_marine_autonomy` (whose branch/versioning model it governs) or cross-referenced
   from `rolker/agent_workspace#172` where the decision itself was made?

## Estimated Scope

Single PR (`feature/issue-384` → `rolling`, once `rolling` exists on origin), config-only:
10 files touched (`bootstrap.yaml`, `core.repos`, `.agents/README.md` content changes; the
other six `.repos` files plus `layers.txt`/`optional_layers.txt` copied over unchanged as
part of populating the new branch). No package code, no build/test changes — `make validate`
against the new branch is the workspace-side follow-up issue's job, not this one.
