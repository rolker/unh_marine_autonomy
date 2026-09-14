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
