---
issue: 351
---

# Issue #351 — marine_web_view: upload to S3 via boto3 instead of shelling out to the AWS CLI

## Issue Review
**Status**: complete
**When**: 2026-08-24 13:05 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #351 — marine_web_view: upload to S3 via boto3 instead of shelling out to the AWS CLI
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Scope Assessment

**Well-scoped?** Yes — replacing two near-identical `subprocess.run(['aws', 's3',
'cp', ...])` call sites with a shared boto3 helper fits one PR, with tests. The
issue correctly identifies both renderer sites (`state_renderer.py:501`,
`coverage_renderer.py:1274`) and the declarability problem (`awscli` has no
apt candidate on noble; `python3-boto3` does — verified: `apt-cache policy
python3-boto3` → `1.34.46+dfsg-1ubuntu1` on this host).

**Right repo?** Yes — `marine_web_view` is domain-specific project content in
`rolker/unh_marine_autonomy`; no workspace-vs-project separation concern.

**Dependencies**: none blocking. The issue itself notes this is prerequisite
housekeeping ahead of the EC2 relocation once `udp_bridge` can relay
([udp_bridge#51](https://github.com/rolker/udp_bridge/issues/51)) — on EC2,
boto3 picks up the instance IAM role automatically, whereas the current
`aws s3 cp --profile` shell-out is the pattern that should *not* travel to a
cloud host with long-lived keys. That's a real scope-timing rationale, not
just a nice-to-have: it argues for doing this now rather than after the move.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Enforcement over documentation | OK | Directly fixes an enforcement gap: the manifest currently can't declare its own runtime dependency, so a README section is the only thing standing between a new operator and a first-upload failure. `<depend>python3-boto3</depend>` makes the manifest tell the truth. |
| A change includes its consequences | Watch | Issue's own consequence list (package.xml comment, README section, tests) is good, but misses one: `marine_web_view/scripts/refresh_chart_tiles.py` also shells out to `aws s3 cp` / `aws s3 sync` (lines ~292, 408, 428) under a *different* profile (`ccom-jhc`, not `p11-renderer`). It is not one of "both nodes," so the acceptance criterion "neither renderer invokes the `aws` binary" is technically satisfiable without touching it — but the Scope section also says "remove the README's 'Runtime prerequisite: the AWS CLI' section" outright, and that section will still be true for this script. Deleting it wholesale would leave inaccurate documentation the same day it's fixed. |
| Test what breaks | OK | Acceptance criteria list success/permission-denied/throttling upload-path tests, which is the right target now that a boto3 client can be stubbed (current tests monkeypatch `coverage_renderer.subprocess.run`/`state_renderer.subprocess.run` module-level, confirmed in `test/test_render_pass.py`). |
| Only what's needed | OK | Scope stays to the upload path; doesn't drag in unrelated renderer changes. |
| Improve incrementally | OK | Single PR, reviewable. |
| Capture decisions | OK | The issue itself records the rationale (declarability + cloud IAM posture) inline — durable even without a dedicated ADR, since this is an implementation-detail swap, not an architectural decision. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| 0009 — Python package management policy | Yes | `<depend>python3-boto3</depend>` via rosdep/apt is exactly the prescribed pattern (apt/rosdep for runtime deps, never bare pip). Issue already verified the rosdep key resolves. |
| 0008 — Follow ROS 2 conventions | Yes (lightly) | Touches `package.xml` and node source; no deviation expected, but the shared-helper module (see below) should follow existing package layout conventions. |
| 0013 — progress.md vocabulary | Yes | This review and downstream phases must use the ADR-0013 entry types; no issue here. |

### Consequences

- README's "Runtime prerequisite: the AWS CLI" section should be **revised**,
  not deleted outright, if `refresh_chart_tiles.py` keeps shelling out to
  `aws` — otherwise the docs would go from "workaround, documented" to "silently
  wrong" for that script's operators.
- `package.xml`'s explanatory comment about the undeclarable `awscli` key
  already names #351 for removal — consistent with the issue, no drift there.
- Existing tests that monkeypatch `subprocess.run` on both renderer modules
  will need to change to stub the boto3 client instead — already anticipated
  by the issue's "add tests" scope item, but worth calling out explicitly so
  it isn't treated as new/unplanned test churn during review.

### Recommendations (open questions for plan-task)

- **`refresh_chart_tiles.py` is a third shell-out site, unmentioned in the
  issue.** It runs `aws s3 sync` (no single boto3 API call equivalent — sync
  is a client-side diff+multi-op the CLI implements, not one SDK call) and
  `aws s3 cp` for manifest read/write, under the `ccom-jhc` profile, invoked
  manually/out-of-band rather than as a ROS node. Recommend the plan
  explicitly scope this **out** (reasonable, given `sync` has no cheap boto3
  equivalent) but say so, and revise rather than delete the README's AWS CLI
  section to note the script is the one remaining consumer.
- **Where should the shared boto3 helper live?** The issue says "a shared
  boto3 helper" but not a module name/location. Both renderers are in
  `marine_web_view/marine_web_view/`; a natural home is a new sibling module
  (e.g. `s3_upload.py`), but this is unspecified and worth settling in the
  plan.
- **"Keep the retry/backoff semantics" overstates what exists today.**
  Reading both `_put`/`_publish`, there is no in-call retry or backoff at
  all — one `subprocess.run` attempt, a failure counter increment, and
  reliance on the *next scheduled pass* (the ROS timer, or coverage's
  dirty-tile re-queue) to retry. The plan should say explicitly that boto3's
  typed exceptions replace exit-code branching for *logging/counting*
  purposes, without introducing new in-call retry/backoff loops that don't
  exist in the current behavior (which would be a scope increase and could
  interact with the existing 20 s/30 s per-call timeouts).
- **Profile-handling asymmetry between the two current call sites**:
  `state_renderer._put` always passes `--profile self.profile`;
  `coverage_renderer._publish` passes `--profile` only when `self.profile` is
  truthy, otherwise falls through to boto3's default credential chain (see
  the comment at `coverage_renderer.py:364` about accommodating a plain
  `~/.aws/credentials`). The shared helper needs to preserve this
  distinction (or the plan should call out unifying it as a deliberate,
  reviewed behavior change rather than an incidental one).

### Actions
- [ ] Revise (don't delete) the README "Runtime prerequisite: the AWS CLI"
      section to scope it to `refresh_chart_tiles.py` once the two renderers
      no longer need it — or explicitly fold the script into scope if the
      plan decides to convert it too.
- [ ] Settle the shared-helper module name/location in the plan.
- [ ] State explicitly that "keep retry/backoff semantics" means preserving
      single-attempt-plus-next-pass-retry behavior with typed-exception
      logging, not adding new in-call retry/backoff.
- [ ] Preserve (or deliberately and visibly change) the profile-handling
      asymmetry between the two current call sites in the shared helper.

## Plan Authored
**Status**: complete
**When**: 2026-08-24 13:30 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-351/plan.md` at `3361681`
**Branch**: feature/issue-351 at `3361681`
**Phases**: single

### Open questions
- [ ] README structure for the `dry_run`/no-AWS-access sentence once the "Runtime prerequisite: the AWS CLI" section is deleted wholesale — move it under per-node docs or drop as redundant (implementer's call, flagged not guessed).
- [ ] `--concurrency` default of 16 for the `refresh_chart_tiles.py` sync reimplementation vs. matching the CLI's default of 10 for parity during cutover.

## Plan Review
**Status**: complete
**When**: 2026-08-24 13:33 -04:00
**By**: Claude Code Agent (Claude Sonnet) (in-context — author self-review)

**Plan**: `.agent/work-plans/issue-351/plan.md` at `3361681`
**PR**: PR-less
**Verdict**: changes-requested

### Findings
- [ ] (must-fix) The size-only "already uploaded" comparison rule's safety premise does not hold against the source it governs. `refresh_chart_tiles.py`'s `--name` (default `bathy4m`) is a FIXED S3 key prefix under `tiles/`, not a per-compilation-versioned one — the plan's Context section frames "a new compilation lands under a new prefix" but that is true only of the CCOM *service name*, not of the script's own tile prefix. The script tracks a `rule_hash = sha256(chart_rule())` (a hash of exactly `RAMP`/`MAX_DEPTH`/`STEP`) in its manifest and, when it changes, explicitly forces a full re-render **and re-upload to the same name/prefix** ("banding changed ... re-rendering regardless of age", `refresh_chart_tiles.py` around the `main()` staleness check). On every non-early-exit run, the fetch loop re-downloads and rewrites every local tile unconditionally (no existing-local-file check) — so the size-only skip decision is evaluated precisely in the one scenario it needs to survive: same z/x/y key, changed pixel colors, and no guarantee the re-rendered PNG's byte count differs from the old object's. Indexed/paletted PNGs of similar structural complexity can very plausibly match byte-for-byte in size despite different colors. A same-size match would be silently skipped, leaving that tile showing stale colors indefinitely (until the next chance rule change or `--force`). This is worse than a wash: today's CLI `sync` compares size **and** local-mtime-vs-remote-LastModified, and since local files are always freshly downloaded ("now") they are essentially always newer than the remote object, so **today's behavior effectively always re-uploads on every render pass** — there is no existing silent-staleness exposure to preserve. The plan's boto3 replacement introduces a new failure mode, not a risk-neutral simplification. — `plan.md` "Already uploaded comparison rule" section (~line 187) and Context section's prefix-versioning framing (~line 30). Resolve before implementation: either drop the skip optimization entirely (unconditional upload matches today's effective behavior and the plan's own concurrency math already gets ~5,839 objects done in ~1-2 min, so the optimization buys little), compare content hash/ETag instead of size, or gate the comparison so any run where `rule_hash` changed this pass performs an unconditional upload for that `--name`.
- [ ] (suggestion) `--concurrency` default (16 vs. the CLI's 10) is left as an open question in the plan; recommend settling it explicitly (e.g., default to 10 for parity at cutover, tunable via the flag) rather than leaving it for the implementer to guess at merge time.
- [ ] (suggestion) In the `test_render_pass.py` rewrite of `test_the_upload_stamps_the_max_age_it_is_given`, when stubbing `self._uploader.put`, also assert the `Bucket`/`Key`/`Body`/`ContentType` the stub receives, not only `cache_control` — the workspace's stated concern (tests that look correct but bind nothing) argues for asserting the full call shape on the new seam, not just carrying over the old test's narrower assertion.

### Verified — no finding
- `refresh_chart_tiles.py` imports only stdlib (`argparse`, `hashlib`, `json`, `math`, `os`, `re`, `subprocess`, `sys`, `time`, `urllib.*`) — no `rclpy`, no `marine_web_view` import. The plan's stated justification for giving the script its own standalone boto3 sync implementation instead of importing the shared `s3_upload.py` (so it can run from cron without the ROS overlay sourced) is accurate against the current source; the duplication is justified as written.
- All four cache-control policies check out against source and the plan preserves each: tile `cache_control` param in `state_renderer._put`/`coverage_renderer._publish`; the shorter `meta_cache_control` (≤`META_MAX_AGE_SECONDS`=5s via `meta_max_age()`) for `meta.json`; `refresh_chart_tiles.py`'s `public,max-age=604800` for chart tiles; and its `no-cache` for the chart manifest.
- `dry_run`/`local_dir` needs no AWS access: both `state_renderer._upload` and `coverage_renderer._publish`'s upload path are already gated behind `if not self.dry_run` before reaching the transport call, and the plan's uploader construction repeats the same gate — no client construction reaches for credentials on a dry run.

### Summary
The plan is well-researched and traces most of the source faithfully (cache-control mapping, dry-run gating, profile asymmetry, the standalone-script justification all check out), but its central new-code safety argument — that a size-only sync comparison is safe because tiles are immutable per compilation and a new compilation gets a new prefix — is contradicted by the script's own `rule_hash`/banding-change re-render path, which reuses the same `--name` prefix and is exactly the scenario the plan needs to be safe against. This is the class of silent-staleness failure this codebase has been repeatedly hardened against, so it should be resolved in the plan before implementation starts, not discovered in review-code.

### Recommended Actions
- [ ] Revise the "Already uploaded comparison rule" in `plan.md` section 4 to close the rule-hash/same-prefix gap (see must-fix finding above) before implementation begins.
- [ ] Settle the `--concurrency` default explicitly rather than leaving it open.
- [ ] When rewriting the max-age test, assert the full stubbed call shape, not just `cache_control`.
