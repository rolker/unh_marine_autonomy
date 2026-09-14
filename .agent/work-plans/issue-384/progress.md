---
issue: 384
---

# Issue #384 — Create the rolling manifest branch (rolling-primary model; gates workspace p11-rolling instance)

## Issue Review
**Status**: complete
**When**: 2026-09-14 13:26 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #384
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Actions
- [ ] Action needed — Clarify the branch-creation mechanics before implementation. The
      current worktree is `feature/issue-384`, branched from `jazzy` (the default,
      protected branch). The deliverable is a new `rolling` branch that does not yet
      exist on `origin`. A normal PR cannot target a base branch that doesn't exist on
      the remote, and merging `feature/issue-384` into `jazzy` as usual would push
      `distro: rolling` / Rolling-pinned `config/repos/*.repos` onto the branch deployed
      machines read — violating the issue's own "non-interfering with deployed
      machines" constraint. `plan-task` should decide and record the actual mechanics
      (e.g., push the initial `rolling` scaffold directly as a new branch off the
      current `jazzy` tip, then treat subsequent `rolling`-targeted work as ordinary
      worktree/PR work based on `rolling`) rather than letting the default worktree
      workflow silently merge this into `jazzy`.
- [ ] Recommendation — Capture the rolling-primary decision (dev trunk moves to
      `rolling`; `jazzy` becomes a deployment variant) as an ADR. Today it exists only
      as a comment on the external `rolker/agent_workspace#172` issue thread (a
      different, "generalization of ros2_agent_workspace" repo) — not an ADR in this
      repo, and not even an ADR in that one. Per "Capture decisions, not just
      implementations" and ADR-0001, a decision this consequential for
      `unh_marine_autonomy`'s own branch/versioning model should have a durable
      record that survives independently of another repo's issue comment. Doesn't
      need to block this issue, but should be tracked as a near-term follow-up (in
      this repo or cross-referenced from it).
- [ ] Recommendation — Once `rolling` exists, update `.agents/README.md`'s `config/`
      description (currently silent on the jazzy/rolling split and the new `distro:`
      bootstrap key) so future agents reading this repo understand the two-branch
      model, per the "config dir" consequences-style expectation this repo already
      follows for its other package/config documentation.

### Notes (supporting the verdict)

- **Right repo**: Yes. `config/bootstrap.yaml`, `config/layers.txt`, `config/repos/*.repos`
  live in this repo (Pattern B — the manifest is embedded in the core-layer checkout),
  so a manifest-branch change belongs here, not in a separate manifest repo.
- **Dependencies**: `rolker/agent_workspace#172` (OPEN, the umbrella workspace-redesign
  issue) and `rolker/agent_workspace#237` (CLOSED — confirms `p11-jazzy` bootstrapping
  already works end-to-end from this repo's `jazzy` branch, so the pattern this issue
  extends is proven). `rolker/ros2_agent_workspace#261` ("Support multiple ROS 2
  distros", OPEN) is the sibling workspace-side tracking issue; no blocking dependency
  on it — ADR-0008 already anticipates this work ("Distro-specific differences should
  be noted... See #261 for multi-distro support plans").
- **Constraint verified**: confirmed no `rolling` branch exists yet locally or on
  `origin`/`gitcloud` (only `jazzy` plus feature branches) — the issue's premise is
  accurate.
- **Constraint verified**: `.agent/scripts/setup_layers.sh` (in the `ros2_agent_workspace`
  workspace repo this project is checked out under) parses `bootstrap.yaml` with
  per-key `grep '^git_url:'` / `'^branch:'` / `'^layer:'` — a new `distro:` line is
  silently ignored by that parser today, matching the issue's claim that
  `ros2_agent_workspace` "ignores unknown keys." The actual consumer of `distro:` is
  the separate `rolker/agent_workspace` adapter, not this workspace.
- **Principle alignment**: no other Action-needed findings. Standards Compliance
  (this repo's own PRINCIPLES.md) and workspace ADR-0008 ("Conventions target
  Rolling... See #261") directly support doing this work; "Only what's needed" and
  "Improve incrementally" are satisfied — the issue is scoped to manifest-only
  changes, deferring the actual per-sibling-repo Rolling ports to later, separate
  issues once each repo decides its own `version:` pin.
- **ADR applicability**: ADR-0002 (worktree isolation) triggered but see the
  branch-mechanics Action item above. ADR-0008 (ROS 2 conventions target Rolling)
  directly relevant, no conflict.

---
**Authored-By**: `Claude Code Agent`
**Model**: `Claude Sonnet`

## Plan Authored
**Status**: complete
**When**: 2026-09-14 13:45 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-384/plan.md` at `3f96ed2`
**Branch**: feature/issue-384 at `3f96ed2`
**Phases**: single

### Open questions
- [ ] Branch-protection: `require_pr` ruleset only targets `~DEFAULT_BRANCH` (jazzy today) — `rolling` will have no push protection until it becomes default or the ruleset is edited (not changed here, Ask-First)
- [ ] CI: `.github/workflows/ros-base-docker.yml` triggers only on `branches: [jazzy]` — PRs into `rolling` get no CI run unless the trigger list is widened (not changed here, Ask-First)
- [ ] `ros2launch_gui`'s pre-existing `rolling` branch (tip 2026-02-17, well behind jazzy) — confirm it's stale/unrelated, not an intentional prior port to reuse
- [ ] `distro: rolling` key contract — plan names it per the issue text only; needs confirmation from the consumer-repo (`rolker/agent_workspace`) reviewer that the adapter's parser expects exactly this key/value
- [ ] ADR for the rolling-primary decision — recommended as a follow-up issue (repo TBD: here vs. `rolker/agent_workspace#172`), not filed as part of this plan-task run

## Plan Review
**Status**: complete
**When**: 2026-09-14 13:38 -04:00
**By**: Claude Sonnet (dispatched fresh-context sub-agent — handoff header present, independent of plan authorship)

**Plan**: `.agent/work-plans/issue-384/plan.md` at `3f96ed2`
**PR**: PR-less (local-first; worktree `feature/issue-384`, `gh` verifications run against `rolker/unh_marine_autonomy`)
**Verdict**: approve

### Findings
- [ ] (suggestion) "Estimated Scope" says "10 files touched" but the Files to Change table itself lists 11 (`bootstrap.yaml`, `core.repos`, 6 other `.repos` files, `layers.txt`, `optional_layers.txt`, `.agents/README.md`) — cosmetic miscount, not a scope problem (6 of the 11 are no-content-change copies).
- [ ] (suggestion) The "`ros2_agent_workspace` ignores unknown keys" claim was verified only against `setup_layers.sh`'s parser; `.agent/scripts/manifest_fallback.sh` also greps `bootstrap.yaml` (`git_url:`/`branch:`/`config_path:`) and likewise ignores `distro:` — re-verified during this review, strengthens rather than undermines the plan's claim, but citing both consumers in the plan would make the evidence airtight for the next reader.

### Verification performed
- Confirmed no `rolling` branch exists yet on `rolker/unh_marine_autonomy` (404) and `default_branch` is still `jazzy`.
- Confirmed the `require_pr` ruleset (id `11881731`) targets `ref_name.include: ["~DEFAULT_BRANCH"]` only — `rolling` will indeed have no push protection, matching the plan's Open Question 1.
- Confirmed `.github/workflows/ros-base-docker.yml` triggers only on `branches: [jazzy]` for both `push` and `pull_request` — matching Open Question 2.
- Confirmed `ros2launch_gui`'s `rolling` branch tip (2026-02-17) predates its `jazzy` tip (2026-08-24) — supports treating it as stale, matching Open Question 3.
- Spot-checked several `core`/`platforms`/`sensors`/`ui`/`underlay` entries (including `geographic_info`, an upstream org repo) for `rolling` branches: all 404, consistent with the plan's "no dependency repo has a rolling branch" conclusion.
- Counted `.repos` entries across all 6 dependent files: 44 total (7+8+8+3+1+8+9), exactly matching the plan's per-entry evidence table row count — the table is a complete enumeration, not a sample.
- Verified `re-verified manifest_fallback.sh` also grep-parses `bootstrap.yaml` and ignores unknown keys the same way `setup_layers.sh` does (see suggestion above).
- Verified `feature/issue-384`'s merge-base with `origin/jazzy` is `jazzy`'s current tip (`6f89cda`) — the branch-creation mechanics in the plan's Context section are mechanically sound as described.

### Independence note
This review ran in a fresh-context sub-agent dispatched with the standard handoff header ("You are a fresh-context sub-agent dispatched for issue #384") — no `## Plan Authored` entry was written earlier in this context, so no self-review annotation applies per the skill's detection rule.
