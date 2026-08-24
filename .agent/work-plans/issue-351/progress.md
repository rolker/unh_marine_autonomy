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

## Implementation
**Status**: complete
**When**: 2026-08-24 14:35 -04:00
**By**: Claude Opus

**Plan**: `.agent/work-plans/issue-351/plan.md` at `3d44436` (updated to match what was built)
**Branch**: feature/issue-351 at `3d44436`
**Commits**: `2e5e720` (renderers), `03f0ade` (chart tile sync), `d8028a7` (package.xml + README), `3d44436` (plan sync)
**PR**: none opened (not pushed, per instruction)

### What was built

All three `aws` shell-out sites are gone; `grep -rn "subprocess\|'aws'"` over the
package finds no invocation, only prose references to the CLI that was replaced.

1. New `marine_web_view/marine_web_view/s3_upload.py` — `S3Uploader.put()` returns
   `(ok, exception_or_None)` so a transport error lands on each node's existing
   failure counter rather than raising into a ROS timer or the render worker.
   `describe_error()` renders a `ClientError`'s S3 code/message, so `AccessDenied`,
   `NoSuchBucket` and `SlowDown` are distinguishable in the log line.
2. `state_renderer._put` and `coverage_renderer._publish` call it. Neither
   constructs a client on a dry run. The profile asymmetry is preserved with the
   reason stated at each construction site (`profile or None` in coverage_renderer,
   uncoalesced in state_renderer).
3. `scripts/refresh_chart_tiles.py` gets its own standalone implementation:
   `get_object`/`put_object` for the manifest, and `sync_dir()` — a
   `list_objects_v2` paginator pass plus a `ThreadPoolExecutor` of `put_object`
   calls — for the pyramid. Still stdlib plus a lazily imported boto3, nothing from
   `marine_web_view`, so it still runs from cron without the ROS overlay.
4. `package.xml` declares `<depend>python3-boto3</depend>`; the `awscli`-undeclarable
   comment is gone. The README's "Runtime prerequisite: the AWS CLI" section is
   replaced by a short "AWS credentials" section (credentials remain a real operator
   prerequisite; the CLI does not).

### Operator-mandated correction: content hash, not size

Implemented as directed. `sync_dir` compares the local file's MD5 against the S3
object's unquoted `ETag` and uploads when they differ or the key is absent.
`is_content_hash()` requires 32 lowercase hex characters, which rejects the
multipart `<hash>-<partcount>` shape an older `aws s3 sync` could have left; an
uncomparable ETag falls back to **uploading**, never to skipping. `put_object` (not
`upload_file`) keeps everything this script writes single-part, and both the
guarantee and its fragility are stated in comments. SSE-KMS ETags are noted as
shape-indistinguishable and failing safe toward uploading.

### Concurrency default: 10

Settled at **10** (`DEFAULT_CONCURRENCY`), matching the AWS CLI's own default —
i.e. the concurrency this pyramid has actually been published at. Parity at the
cutover is worth more than a guess at a faster number, and `--concurrency` raises
it without a code change if a run proves too slow. Justified in a comment beside
the constant, documented as governing the S3 upload only (distinct from `--rate`,
which governs politeness toward CCOM's server), and pinned by
`test_the_default_concurrency_matches_the_cli_it_replaces` so a silent change to
the load put on one S3 prefix fails a test.

### Verification

- Build: `./core_ws/build.sh marine_web_view` — clean (marine_interfaces had to be
  built first in this fresh worktree).
- Tests: `./core_ws/test.sh marine_web_view` — **161 tests, 0 errors, 0 failures,
  0 skipped**. That count includes `ament_flake8`, `ament_pep257` and
  `ament_copyright`, all green (flake8 was run through the ament wrapper against
  the package's own config, not bare). Up from 145 before this work; 26 of the 161
  are new or rewritten upload tests.
- The suite ran **without boto3 installed** — see the next section.

### Mutation evidence — 19 mutations, 19 caught

Each mutation was applied one at a time to the source, the upload-relevant test
files re-run, and the source restored. Re-run with `PYTHONDONTWRITEBYTECODE=1`
after discovering that a stale `__pycache__` entry (same file size, same mtime
second) had made one earlier result untrustworthy; the numbers below are from the
clean pass. Every one failed at least one test:

| Mutation | Caught by |
|---|---|
| dry run reaches the transport anyway | `test_a_dry_run_never_reaches_the_s3_transport` |
| **sync skips anything already present (existence/size only)** | `test_a_recoloured_tile_of_the_same_size_is_uploaded` |
| sync trusts every ETag as a content hash | `test_a_multipart_etag_is_never_trusted_as_a_content_hash` |
| sync swallows a failed upload | `test_a_failed_upload_is_counted_so_the_manifest_is_withheld` |
| sync uploads serially (concurrency ignored) | `test_the_upload_actually_runs_in_parallel` |
| default concurrency changed 10 -> 16 | `test_the_default_concurrency_matches_the_cli_it_replaces` |
| **POLICY: chart tiles lose `public,max-age=604800`** | `test_a_missing_tile_is_uploaded_with_the_cache_policy` + `test_chart_tiles_are_cached_for_a_week_as_pngs` |
| **POLICY: chart manifest loses `no-cache`** | `test_the_manifest_is_published_with_no_cache` |
| manifest read error aborts instead of re-rendering | `test_an_unreadable_manifest_reads_as_empty` |
| **POLICY: coverage `meta.json` max-age override ignored** | `test_the_upload_stamps_the_whole_object_shape_it_is_given` |
| **POLICY: coverage content type hardcoded to `image/png`** | same |
| coverage failed upload not counted | `test_a_failed_upload_is_counted_and_not_raised` |
| **POLICY: state content type not `application/geo+json`** | `test_the_position_upload_carries_geojson_and_the_interval_max_age` |
| **POLICY: state max-age dropped** | same |
| state failed upload not counted | `test_a_failed_position_upload_is_counted_not_raised` |
| uploader drops `CacheControl` from the PUT | all three call-shape tests |
| uploader coalesces the profile | `test_the_profile_reaches_the_client_factory_exactly_as_given` |
| uploader swallows the exception and reports success | `test_a_client_error_comes_back_as_a_value_not_a_raise` |
| `describe_error` drops the S3 error code | same |

Both mutations the operator named specifically are covered: a sync that skips a
changed tile fails a test, and each of the four cache policies fails a test when
dropped. To make the chart-tile policy bindable rather than a copy of itself in
the test, the literal was lifted out of `main()` into a module constant
`TILE_EXTRA_ARGS`.

### boto3's absence blocked nothing in the suite — one gap remains

boto3 is **not installed on this host** and nothing was installed (no `sudo apt`,
no pip, no `--break-system-packages`). It blocked nothing: `import boto3` happens
lazily inside `_boto3_client()` / `s3_client()`, and every test injects a stub
client, so the full 161-test suite ran green without the SDK. That is not a
contortion for this host's sake — it is what makes the README's `dry_run` promise
true, and it is pinned by `test_importing_the_module_does_not_import_boto3`.

**What could not be verified at the time — SINCE VERIFIED; corrected
2026-08-24 during the round-1 fix pass.** The list below was accurate when this
entry was written (boto3 was not installed on the host) and stale by the time
review ran, where it contradicted the PR body. The operator has since installed
the dependency and exercised the real paths on this host:

- `rosdep install` was run for real (not `-s`) and resolved cleanly to
  `python3-boto3`; boto3/botocore **1.34.46** are installed here now.
- The real `_boto3_client()` / `s3_client()` bodies have executed, and the real
  client was exercised against S3 on **both the success and the error paths**.
  (`p11-renderer` is prefix-scoped, so `AccessDenied` outside `live/*` is
  correct least privilege, not a defect.)
- The suite passes both with and without the SDK present;
  `test_importing_the_module_does_not_import_boto3` still binds with boto3
  installed — mutated to a module-scope `import boto3`, it fails.

Nothing about the running production renderers was touched; they are on the old
merged code out of `layers/main/core_ws` and are unaffected until redeployed.

### Command for the operator

```bash
cd /home/roland/project11/layers/worktrees/issue-unh_marine_autonomy-351/core_ws
rosdep install --from-paths src/unh_marine_autonomy/marine_web_view --ignore-src -y
```

Then, to confirm the real SDK path:

```bash
cd /home/roland/project11/layers/worktrees/issue-unh_marine_autonomy-351
source setup.bash && ./core_ws/build.sh marine_web_view && ./core_ws/test.sh marine_web_view
```

The suite should still report 161/161. `test_importing_the_module_does_not_import_boto3`
is the one to watch: it must keep passing **with** boto3 installed, since it
asserts nothing pulls the SDK in at import time.

### Follow-ups / notes for review

- Not pushed, no PR opened, per instruction.
- The `Closes #351` keyword is in commit `d8028a7` only.
- `test_tile_ingest.py`'s docstring and two in-code comments that named
  "a 30 s-capped subprocess" / "an `aws s3 cp`" were rephrased; they described a
  mechanism that no longer exists.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-24 14:27 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**PR**: #353 at `a75dec3`
**Branch**: feature/issue-351 at `a75dec3`
**Mode**: post-PR diff (`jazzy...HEAD`), reported on the pre-push timeline
**Depth**: Deep (reason: 13 files / +1800-127, credential-handling and cross-cutting transport swap)
**Specialists**: Static Analysis (flake8 clean, xmllint clean), Governance, Plan Drift, Claude Adversarial Lens A + Lens B. Copilot off (reviewing PR #353 in parallel). Local model off (models removed from this host).
**Must-fix**: 4 | **Suggestions**: 13
**Round**: 1 | **Ship**: continue — must-fix 1 is a genuine correctness regression (single-threaded executor stall and a shutdown flush that can no longer win its join), cross-confirmed by both adversarial lenses and verified arithmetically against botocore 1.34.46 on this host.

Nothing raised at issue review or plan review was silently dropped; the
`3d44436` plan amendment is an honest record and the as-built content-hash rule
does fix what the size-only rule was rejected for. The four cache policies all
reach `put_object` and all four are bound by tests. 19 mutations were applied to
a scratch copy (pycache purged first): 11 were caught, 8 survived and are listed
below.

### Findings
- [x] (must-fix) boto3 retries multiply the per-upload ceiling ~4x (state_renderer ~87 s vs the old hard 20 s; coverage_renderer ~127 s vs 30 s) and three comments assert the opposite; one stalled PUT blocks `rclpy.spin`'s single-threaded executor and drops nav fixes, and `join(timeout=45.0)` can no longer win, skipping the documented final flush — `marine_web_view/s3_upload.py:69`, `state_renderer.py:238`, `coverage_renderer.py:446`, `coverage_renderer.py:1035`
- [x] (must-fix) every process-level wall-clock cap on the cron script was removed (`aws s3 sync` had `timeout=3600`, the manifest `cp` 120 s, the manifest read 60 s); worst case is now ~365 s per PUT across ~5,839 objects with no aggregate deadline and no lockfile, so an overrun run doubles the request rate against CCOM's server — `scripts/refresh_chart_tiles.py:328` and `sync_dir`
- [x] (must-fix) `sync_dir`'s "strictly better than the status quo, not just equivalent" is false: `list_objects_v2` does not return `CacheControl`/`ContentType`, so identical bytes skip and a change to `TILE_EXTRA_ARGS` propagates only to tiles whose pixels also changed — permanently mixed cache policy, and `--force` does not help — `scripts/refresh_chart_tiles.py` (`sync_dir` docstring, `TILE_EXTRA_ARGS`)
- [x] (must-fix) CLI-era mechanics survive in three places the PR otherwise scrubbed: a `--profile` flag that no longer exists — `README.md:210`, `coverage_renderer.py:363`, `test/test_s3_upload.py` (`test_the_profile_reaches_the_client_factory_exactly_as_given` docstring)
- [x] (suggestion) test gap: `list_objects_v2` pagination is unbound — no test gives `_FakeS3` more than one existing key, so `for page in ...[:1]` survives the suite; broken, ~4,800 unchanged tiles would be re-uploaded every run — `test/test_chart_tile_sync.py:85`
- [x] (suggestion) test gap: the whole `Config` is unbound — replacing it with `Config(retries={'mode': 'legacy', 'max_attempts': 99})` survives; this is exactly the arithmetic must-fix 1 turns on, so the guard should land with the fix — `marine_web_view/s3_upload.py:66`
- [x] (suggestion) test gap: the profile passthrough is bound only up to the monkeypatched factory — `boto3.Session(profile_name=profile or None)` survives, and that one word is what state_renderer's documented fail-loudly contract rests on — `marine_web_view/s3_upload.py:71`
- [x] (suggestion) test gap: "no client is constructed on a dry run" is asserted in three places and tested nowhere — dropping the `None if self.dry_run else` gate survives in BOTH nodes; sharpest for state_renderer, whose uncoalesced default profile would raise `ProfileNotFound` out of `__init__` on a credential-free simulator host — `coverage_renderer.py:449`, `state_renderer.py:240`
- [ ] (suggestion) `load_manifest`'s blanket `except` reads `AccessDenied`/expired-token as first-run, so `--profile p11-renderer` (which its own `--profile` help warns cannot write `tiles/`) costs ~49 min of requests against CCOM before every PUT fails, every cron run; and a transient read failure plus a successful upload rewrites `tiles/manifest.json` with only this `--name`'s entry. Pre-existing (verified against `jazzy`) — but boto3 is what makes `NoSuchKey` distinguishable, which `_boto3_client`'s own docstring advertises. Worth a follow-up issue — `scripts/refresh_chart_tiles.py:334`
- [ ] (suggestion) a stale `--workdir` re-publishes last run's PNGs for tiles that failed or turned blank this run while the manifest asserts the new `rule_hash`; pre-existing under size+mtime too, but `sync_dir`'s premise "the fetch loop rewrites every local tile unconditionally" is false for exactly those tiles — `scripts/refresh_chart_tiles.py` (`outdir`, `sync_dir` docstring)
- [ ] (suggestion) `save_manifest` is unguarded: a raise escapes `main()` as a traceback after a successful pyramid upload, leaving tiles published but unrecorded so the next run re-fetches everything — `scripts/refresh_chart_tiles.py:349`
- [ ] (suggestion) `state_renderer.main()` constructs the node outside the `try`, unlike `coverage_renderer.main()` which moved it inside for exactly this reason; this PR adds a new constructor failure mode (`ProfileNotFound`, `NoRegionError`) — `state_renderer.py:531`
- [ ] (suggestion) the EC2 instance-role path — a stated motivation for this PR — is documented only for coverage_renderer; state_renderer needs a named profile with `credential_source = Ec2InstanceMetadata`. The new section also names only `~/.aws/credentials` (`~/.aws/config` also applies) and dropped all timeout/retry documentation — `README.md:19`
- [ ] (suggestion) the chart-tile section mentions cron without mentioning credentials, so a reader can land on `--profile p11-renderer` and get `AccessDenied` on every PUT plus a silently-empty manifest — `README.md:502`
- [ ] (suggestion) startup-failure semantics changed (a typo'd profile now aborts node startup rather than failing per upload) and the README covers only the blank case — `state_renderer.py:240`, `coverage_renderer.py:449`
- [ ] (suggestion) `refresh_chart_tiles.main()` has no test coverage at all: the `written == 0`, 5%-failure and `if up_failed: return 1` gates are verified only by reading — `scripts/refresh_chart_tiles.py:588`
- [x] (suggestion) the `## Implementation` entry's "What could NOT be verified here" now contradicts the PR body and the operator's actual verification (real client exercised against S3, clean `rosdep install`, 161 tests with and without boto3); update the timeline so the record matches — `.agent/work-plans/issue-351/progress.md`
- [ ] (suggestion) instruction candidate, proposal only: `s3_upload.py`'s lazy-import-plus-injectable-client pattern is a reusable answer to "declare a runtime SDK via rosdep but keep the tests runnable without it" and generalises beyond boto3 — `.agent/knowledge/ros2_development_patterns.md`

## Implementation
**Status**: complete
**When**: 2026-08-24 14:47 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-351 at `48f0afb`
**PR**: #353
**Addressed**: `## Local Review (Pre-Push)` of 2026-08-24 14:27 -04:00 at `a75dec3` — all 4 must-fixes and the 4 test gaps it verified as surviving mutants, plus the stale-record suggestion. The remaining 8 suggestions stay open and unchecked.
**Commits**: `816d0d2`, `8f25a3b`, `7b43d7f`, `95c8029`, `c6a953e`, `80488ed`, `535826c`, `46e3ad2`, `48f0afb`

### The retry decision, with its arithmetic

**`max_attempts: 1` in the two nodes** — the review's preferred option, taken.

botocore counts `max_attempts` as TOTAL attempts (verified in the installed
botocore 1.34.46: `MaxAttemptsChecker.is_retryable` returns
`attempt_number < self._max_attempts`, so `1` never retries), and both connect
and read timeouts are retryable. The worst case for one PUT is therefore
`max_attempts * (connect_timeout + read_timeout)`:

| Node | connect | read | at 4 attempts | at 1 attempt | old CLI cap |
|---|---|---|---|---|---|
| `state_renderer` | 5 | 15 | 80 s | **20 s** | 20 s (`subprocess.run(timeout=20)`) |
| `coverage_renderer` | 5 | 25 | 120 s | **30 s** | 30 s (`subprocess.run(timeout=30)`) |

One attempt restores each node's old ceiling *exactly*, which is what
`state_renderer._put` (on `rclpy.spin`'s single-threaded executor) and
`coverage_renderer.stop()`'s `join(timeout=45.0)` were both built around.
Nothing is lost: both nodes already retry on their own schedule — the next
timer tick, or the tile staying dirty for the next render pass — which is what
both docstrings say and what the CLI shell-out did. The three comments that
asserted the opposite now state this arithmetic, and
`coverage_renderer.stop()`'s comment now names the 30 s figure its 45 s join is
sized against.

**The cron script is the opposite case and keeps SDK retries** — a run gets no
second chance for hours, and one failed PUT withholds the whole manifest. The
budget is sized instead: `s3_client(profile, max_seconds, attempts)` solves
`read_timeout` from the ceiling it is handed, so
`attempts * (connect + read)` lands on the old cap and cannot drift from the
number written beside it. `main()` builds one client per cap — 120 s for the
uploads and the manifest write, 60 s (2 attempts) for the manifest read.

### Wall-clock bounds restored on the cron script

| Operation | Old cap | Restored as |
|---|---|---|
| `aws s3 sync` | `timeout=3600` | `sync_dir(deadline_seconds=3600)` — an aggregate deadline; jobs not started by it fail rather than run, so the manifest is withheld and the next run finishes the job |
| manifest `cp` (write) | `timeout=120` | `s3_client(profile, MANIFEST_WRITE_SECONDS)` → 3 × (10 + 30) |
| manifest `cp -` (read) | `timeout=60` | `s3_client(profile, MANIFEST_READ_SECONDS, attempts=2)` → 2 × (10 + 20) |

**A lockfile does belong here** — `acquire_run_lock()` takes a per-`--name`
`flock` before the first request to CCOM. This is not belt and braces: a run
takes most of an hour at the default `--rate`, so cron re-entry is the realistic
case, and two overlapping runs double the request rate against the server this
script's own docstring says to ask before loading harder — while both write the
same `--workdir`. Per `--name`, because that is the unit that shares a workdir
subtree and an S3 prefix; two different layers are a deliberate operator choice
and may run together. flock is released by the kernel on exit, so a killed run
strands nothing.

### `sync_dir`'s honesty, and the metadata gap

The docstring now says **better on skipping, worse on metadata** — not
"strictly better". `list_objects_v2` returns no `CacheControl`/`ContentType`,
so a change to `TILE_EXTRA_ARGS` reaches only tiles whose pixels also changed.
`sync_dir(force=True)` is new and is wired to `--force`, which previously only
re-rendered: that is the remedy an operator has today for propagating a policy
change. A metadata-only path that avoids the ~hour of CCOM requests a re-render
costs (a re-upload-from-workdir mode, or a `head_object` per key) is named in
the docstring as an explicit follow-up rather than silently implied.

### Mutation evidence — 12 mutations applied, 12 caught

Each applied one at a time with `PYTHONDONTWRITEBYTECODE=1`, the relevant tests
re-run, and the source restored and diffed against the original.

| Mutation | Caught by |
|---|---|
| `max_attempts` 1 → 4 | `test_the_client_config_is_the_per_put_time_budget` + both `test_one_put_stays_under_the_ceiling_each_node_was_built_around` cases |
| retry mode `standard` → `legacy` | `test_the_client_config_is_the_per_put_time_budget` |
| `profile_name=profile` → `profile or None` | `test_the_profile_reaches_boto3_session_uncoalesced` |
| module-scope `import boto3` (with boto3 INSTALLED) | `test_importing_the_module_does_not_import_boto3` |
| paginator truncated to its first page | `test_every_page_of_the_listing_is_read` |
| aggregate deadline check removed | `test_a_sync_past_its_deadline_stops_instead_of_overrunning` |
| `SYNC_DEADLINE_SECONDS` 3600 → 86400 | same |
| `force` ignored (always compare) | `test_force_reuploads_unchanged_tiles` |
| `flock` call removed from the run lock | `test_a_second_run_for_the_same_name_is_locked_out` |
| request budget blown (3 attempts → 5, fixed 60 s read) | `test_one_s3_request_stays_under_the_cap_the_shell_out_enforced` |
| dry-run gate deleted in `state_renderer` | `test_state_renderer_builds_no_client_on_a_dry_run` |
| dry-run gate deleted in `coverage_renderer` | `test_coverage_renderer_builds_no_client_on_a_dry_run` |

The dry-run gate is the one the review called most important, and binding it
needed the only test module here that builds the **real** ROS nodes — the gate
lives in `__init__` and nothing short of running it binds anything. Discovery is
turned OFF and the domain moved to 101 so a test run cannot reach the renderers
running on this host; both constructions take about a tenth of a second.

### Verification

- Build: `./core_ws/build.sh marine_web_view` — clean.
- Tests: `./core_ws/test.sh marine_web_view` — **172 tests, 0 errors,
  0 failures, 0 skipped** (up from 161; 11 new tests). `ament_flake8`,
  `ament_pep257` and `ament_copyright` are in that count and green — flake8 via
  the ament wrapper against the package's own config, which caught two CNL100s
  and an I100 that bare flake8 did not.
- boto3/botocore **1.34.46 are installed on this host** and the suite passes
  with them present; `test_importing_the_module_does_not_import_boto3` still
  binds under that condition (mutation above). The new Config tests execute the
  real `_boto3_client` body against stand-in `boto3`/`botocore` modules
  installed with `monkeypatch.setitem`, so the SDK is still never imported.
- Nothing was uploaded to or deleted from the bucket; no S3 access at all was
  made from this pass. The production renderers running out of
  `layers/main/core_ws` were not touched.
- `plan.md` is synced: the three claims the review falsified are corrected in
  place and marked as corrections rather than quietly rewritten.

### Actions
- [x] (must-fix) retry ceiling — `max_attempts: 1`; three comments corrected — `marine_web_view/s3_upload.py:71`, `state_renderer.py:238`, `coverage_renderer.py:446`, `coverage_renderer.py:1035`
- [x] (must-fix) process-level caps on the cron script, aggregate sync deadline, and a run lock — `scripts/refresh_chart_tiles.py`
- [x] (must-fix) `sync_dir` docstring corrected; `--force` now re-uploads; metadata path named as a follow-up — `scripts/refresh_chart_tiles.py`
- [x] (must-fix) CLI-era `--profile` prose removed — `README.md:210`, `coverage_renderer.py:365`, `coverage_renderer.py:445`, `test/test_s3_upload.py`
- [x] (suggestion) test gap: `list_objects_v2` pagination — `test/test_chart_tile_sync.py`
- [x] (suggestion) test gap: the whole `Config` — `test/test_s3_upload.py`
- [x] (suggestion) test gap: profile passthrough past the factory — `test/test_s3_upload.py`
- [x] (suggestion) test gap: no client on a dry run, BOTH nodes — `test/test_dry_run_needs_no_aws.py`
- [x] (suggestion) the stale "What could NOT be verified here" record — `.agent/work-plans/issue-351/progress.md`

### Still open (out of scope this round)
The other 8 suggestions from the source review are left unchecked, including
the pre-existing `load_manifest` blanket-`except` that reads `AccessDenied` as
first-run.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-24 15:25 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**PR**: #353 at `ce77d90`
**Branch**: feature/issue-351 at `ce77d90`
**Mode**: post-PR diff (`jazzy...HEAD`), concentrated on `28eb151..HEAD`, reported on the pre-push timeline
**Depth**: Deep (reason: +759-77 in round 2 on top of +2528-132; credential handling, wall-clock bounds, threading and a new lock file)
**Specialists**: Static Analysis (ament profile clean), Governance, Plan Drift, Claude Adversarial Lens A + Lens B. Copilot off (reviewing PR #353 in parallel). Local model off (models removed from this host).
**Must-fix**: 6 | **Suggestions**: 10
**Round**: 2 | **Ship**: continue — the round-1 must-fix-1 remedy does not achieve what it claims: `max_attempts: 1` does remove SDK retry, but `connect_timeout` is a PER-ADDRESS bound in urllib3 and the S3 endpoint resolves to 8 A records here, so the real per-PUT worst case is ~55 s (state_renderer, asserted 20 s) and ~65 s (coverage_renderer, asserted 30 s). `stop()`'s 45 s join still cannot win. Must-fix count is up 4 -> 6 and includes a correctness concern, not mechanical fixes.

The round-2 pass is honest work: the plan corrections are marked as corrections
rather than quietly rewritten, `--force` genuinely cannot skip via the
content-hash path, the sync's deadline abort genuinely withholds the manifest
and returns 1, the lock file and `manifest.json` both sit outside `outdir` so
neither is swept into the bucket, and `boto3.Session(profile_name='')` really
does raise `ProfileNotFound` so state_renderer's fail-loudly contract holds
(all verified on this host). My initial suspicion that botocore's retry backoff
blew the cron budget was WRONG — `ExponentialBackoff` adds only ~3 s over 3
attempts. But 7 mutations survive the green 172-test suite, all of them at the
call sites that supply the numbers the fix pass argues from.

### Findings
- [x] (must-fix) the per-PUT ceiling the whole round-2 argument rests on does not exist: urllib3's `create_connection` applies `connect_timeout` per `getaddrinfo` result and the S3 endpoint has 8 A records, so worst case is ~8x(connect)+read = ~55 s / ~65 s, not 20 s / 30 s; separately botocore builds the STS client for a role-assuming profile with a config carrying only `signature_version`, so credential resolution runs under botocore defaults outside the caller's `Config` — `marine_web_view/s3_upload.py:59-69,84`, `state_renderer.py:238-247`, `coverage_renderer.py:450-457,1040-1046`
- [x] (must-fix) `stop()`'s new comment asserts a safety property the code lacks: `_render_pending` checks `deadline` only when one is passed (a scheduled pass passes none) and never checks `self._stop`, so a pass issues one PUT per dirty tile plus `_publish_meta` and two stalled tiles already blow the 45 s join, taking the early return that skips the final flush — `coverage_renderer.py:1037-1046`, `1189-1211`
- [x] (must-fix) round 2's fixes are bound at the helper level and unbound at every call site — 7 mutations survive the full suite: both nodes' `read_timeout` (25->250, 15->150, and ->600), `join(timeout=45.0)`->1.0, `sync_dir`'s `deadline_seconds` default ->86400, `main()` dropping `force=a.force`, `main()`'s `if lock is None:`->`if False:`, and `acquire_run_lock(a.workdir, a.name)`->`'run'` — `test/test_s3_upload.py:272-322`, `refresh_chart_tiles.py:630-636,698-700`
- [x] (must-fix) the per-`--name` lock scope blesses a lost-update race on the shared manifest: `tiles/manifest.json` is one key, `mf = workdir/manifest.json` is one local path (not under `a.name`), and `man[a.name] = {...}` is a whole-dict read-modify-write, so two concurrent names lose the loser's entry and the next cron run re-crawls CCOM for ~5,839 tiles — `refresh_chart_tiles.py:377`, `645`, `709-719`
- [x] (must-fix) the new lock file lives in an unowned `/tmp` tree: `makedirs(exist_ok=True)` accepts a foreign or symlinked dir and `open(...,'w')` follows a planted symlink and truncates it (demonstrated), a squatted lock silently stops every run at exit 0, and because the same tree is `outdir` anything placed there is PUT to the public `tiles/` prefix under the admin profile — `refresh_chart_tiles.py:386-387`
- [x] (must-fix) doc consequence: the README documents none of round 2's operator-visible changes — `--force` now re-uploads and is per `sync_dir`'s own docstring the ONLY remedy for a `TILE_EXTRA_ARGS` change, a held lock makes a run exit 0 doing nothing, and the sync gained a 3600 s aggregate deadline — `README.md:489-508`
- [x] (suggestion) `test_one_s3_request_stays_under_the_cap_the_shell_out_enforced` is algebraically tautological — `attempts*(CONNECT_TIMEOUT+read_timeout)==max_seconds` holds identically for any `CONNECT_TIMEOUT`; mutating it 10->25 passes — `test/test_chart_tile_sync.py:441-462`
- [x] (suggestion) `s3_client`'s guard covers one of three bad inputs: `attempts=0` raises `ZeroDivisionError`, `attempts=-1` gives nonsense text, and a tiny positive budget passes silently (`s3_client('p', 31, attempts=3)` -> 0.333 s read) — `refresh_chart_tiles.py:356-360`
- [ ] (suggestion) `remote_etags` is unguarded, a parity regression: on `jazzy` a sync/listing failure became `return 1` with "upload failed"; now `ListBucket` AccessDenied escapes `main()` as a traceback after the ~hour of fetching, and `load_manifest` swallows the same denial as "first run" — `refresh_chart_tiles.py:519`
- [x] (suggestion) the shutdown flush can overrun `SHUTDOWN_FLUSH_SECONDS` by one full PUT: `_render_dirty` calls `_publish_meta` unconditionally after the deadline-honouring loop, so ~60 s not 30 s — `coverage_renderer.py:1048-1056`, `1174-1186`
- [ ] (suggestion) the aggregate deadline's clock starts after `remote_etags` and the ~5,839-file MD5 pass and is checked only at job start, so the real bound is listing+hashing+3600 s+one request; and "leaves the next run to finish the job" overstates it — there is no upload-only path to the workdir tiles — `refresh_chart_tiles.py:513-517`, `534`
- [x] (suggestion) a held lock exits 0, indistinguishable from success in exit status; the stderr message does reach cron mail, but a wedged run makes every later invocation exit 0 forever — `refresh_chart_tiles.py:630-636`
- [ ] (suggestion) `max_workers` is unbounded while the client carries no `max_pool_connections` (botocore default 10); `DEFAULT_CONCURRENCY = 10` matches today but the help text invites `--concurrency 32`, which silently exceeds the pool and pays a TLS handshake per PUT — `refresh_chart_tiles.py:88-89`, `555`
- [ ] (suggestion) a partial pyramid can still get a completeness manifest via the FETCH side: the 5 % gate permits ~290 missing tiles, `up_failed == 0`, and the manifest records `tiles`/`blank` but not `failed`, so the next run reads "unchanged and fresh" for up to 30 days — `refresh_chart_tiles.py:692-720`
- [ ] (suggestion) `test_dry_run_needs_no_aws.py` writes `ROS_DOMAIN_ID` / `ROS_AUTOMATIC_DISCOVERY_RANGE` into `os.environ` at module import with no restore, so every later-collected module inherits domain 101 — safer, but hidden session-wide coupling — `test/test_dry_run_needs_no_aws.py:44-46`
- [ ] (suggestion) `public,max-age=604800` on a prefix not versioned per compilation means a `rule_hash` re-render is invisible for up to a week and the map is the old/new patchwork `chart_rule()` exists to prevent; pre-existing, but versioning the prefix by `rule_hash` is the clean fix — `refresh_chart_tiles.py:111`

### The 8 round-1 suggestions still open
None was invalidated or made wrong by this pass. Four anchors drifted:
`load_manifest` 334 -> 396, `save_manifest` 349 -> 410, `main()` 588 -> 573,
`state_renderer.main()` 531 -> 533; the two node-construction anchors moved
~5 lines (240 -> 245, 449 -> 454). Two were independently re-derived this
round and are now cross-confirmed: the `load_manifest` blanket-`except`
(Lens B reached it from the "no credential preflight anywhere" direction) and
the EC2 route for state_renderer, where I verified that NO parameter value
reaches the default chain — both `''` and `'   '` raise `ProfileNotFound` — so
an EC2 operator needs an explicit `credential_source = Ec2InstanceMetadata`
stanza that neither the code nor the README names. The `main()`-has-no-test
suggestion is what must-fix 3 above escalates.

### Checked and clean, so it is not re-litigated
`max_attempts=1` really is one attempt (`MaxAttemptsChecker`, botocore
1.34.46). `standard`-mode backoff adds only ~3 s over 3 attempts, so the cron
`read_timeout` arithmetic is not meaningfully off for that reason, and it uses
true division with a positive-budget guard — no truncation, and no zero or
negative value reachable from either current call site. `--force` cannot skip
via the content-hash path. A deadline abort counts as a failure, so `main()`
returns 1 and the manifest is withheld — no partial pyramid gets a
completeness claim by that route. The lock file and `manifest.json` both live
in `workdir`, outside `outdir`, so `sync_dir`'s `os.walk` cannot sweep them
into the bucket. The `deadline` closure is race-free and `sent`/`failed`/`log`
are touched only from the main thread's `as_completed` loop; the shared client
is built in the main thread and the paginator completes before any worker
starts. flock is released on every exit path. The `--name` regex blocks
traversal and prefix escape. Plan drift: none — corrections are marked as
corrections. Static analysis: `ament_flake8` / `pep257` / `copyright` are in
the green 172; ad-hoc B902/D103/I201 hits are suppressed by the package's own
ament profile. `.agents/README.md` does not mention `marine_web_view`, so
there is no parameter-table consequence; no `review-context.yaml` exists.

## Implementation
**Status**: complete
**When**: 2026-08-24 16:41 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-351 at `f0f3022`
**PR**: #353
**Addressed**: the `## Local Review (Pre-Push)` of 2026-08-24 15:25 -04:00 at `ce77d90` (round 2, changes-requested, 6 must-fix), under the operator decision that replaced must-fix 1's arithmetic with a structural change
**Commits**: c034839, a81569e, 8ce7acb, efbd7c3, baf0604, f0f3022

### What changed, and why it is not a third round of arithmetic

Rounds 1 and 2 both tried to guarantee an exact per-upload wall-clock
ceiling: round 1 was wrong by 4x (SDK retry), round 2 by a further ~2x
(`connect_timeout` is applied per DNS address; the S3 endpoint has 8 A
records here). The operator's decision was to stop depending on a ceiling.
So **no per-PUT ceiling is claimed anywhere in the package any more** — the
comments and the two tests that pinned the arithmetic are deleted, not
re-derived — and both nodes were restructured so the number does not matter.

**state_renderer — uploads are off the spin thread.** `AsyncUploader`
(`marine_web_view/s3_upload.py`) runs PUTs on one background daemon thread.

*Why it cannot grow unbounded*, two independent ways:
- **One slot per key.** `submit` REPLACES a pending payload for the same key
  rather than queueing behind it, so the pending map's size is at most the
  number of distinct keys the node publishes (two: position and track).
  Dropping a superseded payload is not data loss: the position object is the
  latest fix and the track is rebuilt from the whole `_history` window, so
  both are complete snapshots of the present. A queue would grow for as long
  as the endpoint was slow and then publish a march of stale positions.
- **A hard `max_slots` cap (default 4).** A submission for a NEW key when the
  map is full is refused and counted, so the bound is a property of the class
  and not of its caller — a future third artifact cannot turn a stall into
  unbounded memory.

*One worker for both keys, deliberately.* Per-key slots make the only
ordering that matters (within a key) free, and FIFO across keys — dict
insertion order, which re-assignment does not disturb — keeps the 1 s
position from starving the 30 s track. A second thread would not buy
liveness: a stall is a property of the endpoint, not of the object, so the
two would stall together; it would buy a second shutdown path and a second
pooled connection.

*Shutdown.* `stop()` sets the stop event, wakes the worker, joins with a
timeout and returns whether the thread ended. Pending payloads are abandoned
— at a 1 s cadence the unsent last position is worth less than a clean exit.
The worker is a daemon thread, so neither `stop()` nor interpreter exit can
hang on a request already in the socket.

*Retry contract preserved.* Acceptance is not publication: both ticks now
compare the fix stamp against what the worker CONFIRMED, so a failed upload
is offered again on the next tick exactly as `_put`'s return value used to
arrange.

**coverage_renderer — every pass takes an abort predicate.** Round 2's
`deadline` was checked only when one was passed and a scheduled pass passed
none, so a scheduled pass was as long as the endpoint made it and never
looked at `self._stop`. `_render_dirty(abort)` / `_render_pending(abort)`
now consult a predicate before every upload on every path: the stop event
for a scheduled pass (the default), a wall-clock deadline for the shutdown
flush. The `_publish_meta` PUT that used to follow the loop unconditionally
is inside the budget too. The 45 s join — sized against the ceiling that
does not exist — is now `WORKER_JOIN_SECONDS = 10.0`, which only has to
cover the request the worker is already inside, because it checks the stop
event between tiles. Worst-case shutdown is therefore
`WORKER_JOIN_SECONDS + SHUTDOWN_FLUSH_SECONDS` plus one in-flight request,
by construction; the flush is skipped only when the worker is genuinely
wedged, and that is a warning, never a hang.

### Actions
- [x] (must-fix) the per-PUT ceiling does not exist — restructured rather than re-derived: `s3_upload.py` `AsyncUploader`, `state_renderer.py:245-262`, `coverage_renderer.py:1069-1096`, and every comment asserting `connect + read` deleted (`s3_upload.py:59-84`, `state_renderer.py:238-250`, `coverage_renderer.py:449-462`, `refresh_chart_tiles.py:90-115,341-375`). The STS-client half of the finding is what remains untestable from here — see "Not verified".
- [x] (must-fix) `stop()` no longer depends on the join winning — abort predicate on every path, short honest join, meta PUT inside the budget — `coverage_renderer.py:1046-1096`, `1204-1232`, `1249-1258`
- [x] (must-fix) the wiring is bound, not just the helpers — new `test/test_node_upload_wiring.py` runs both real constructors against a recording transport; `main()` gained its first tests (`--force` pass-through, per-`--name` lock, held-lock short-circuit, sync-deadline default)
- [x] (must-fix) lost-update race on the shared manifest — `update_manifest()` re-reads inside a lock and touches only its own key, staged per-name — `refresh_chart_tiles.py:504-550`, `main()` call site
- [x] (must-fix) the lock file left the unowned `/tmp` tree — `lock_dir()` uses `$XDG_RUNTIME_DIR`/`~/.cache`, 0700, ownership and group/other-write checked, `O_NOFOLLOW` on the file; a held lock names its holder and a holder older than `LOCK_STALE_SECONDS` exits non-zero — `refresh_chart_tiles.py:367-470`
- [x] (must-fix) README documents round 2's and round 3's operator-visible changes — "What happens when S3 is slow" and "Operating it from cron" (`--force` as the only cache-policy remedy, the lock's location and both exit codes, the aggregate deadline and what it does not cover, the shared manifest)
- [x] (suggestion) the tautological per-request cap test is gone with the arithmetic it pinned — `test/test_chart_tile_sync.py` now pins the Config that reaches botocore, and rejects `attempts` 0/-1 and non-positive `read_timeout` (the second suggestion's three bad inputs) — `refresh_chart_tiles.py:s3_client`
- [x] (suggestion) the shutdown flush no longer overruns by a manifest PUT — same abort check — `coverage_renderer.py:1214-1220`
- [x] (suggestion) a held lock is no longer indistinguishable from success forever — `LOCK_STALE_SECONDS`, exit 1 past it — `refresh_chart_tiles.py:LOCK_STALE_SECONDS`, `main()`

### Deferred — the operator scoped these out of this pass
Left **unchecked** in the round-2 entry, deliberately, per the operator's
"leave them open and unchecked": the 8 round-1 suggestions still open
(including the pre-existing `load_manifest` blanket-`except`), and the
round-2 suggestions not entangled with must-fix 1-3: `remote_etags` being
unguarded, the aggregate deadline's clock start, `max_workers` vs
`max_pool_connections`, the FETCH-side partial-pyramid manifest, the
`ROS_DOMAIN_ID` module-import side effect in `test_dry_run_needs_no_aws.py`,
and versioning the tile prefix by `rule_hash`.

### Mutation evidence

22 mutants, applied one at a time, `__pycache__` cleared and the package
rebuilt before each run (the install is a copy, not a symlink, on this
worktree), full suite each time. **All 22 fail at least one test.** Three of
them survived the first attempt and are the reason three tests exist:
mutants 4, 10 and 21 below. Numbers are the suite size at the time of the run
(195 at the end).

| # | mutation | result |
|---|---|---|
| 1 | `state_renderer._queue` uploads synchronously through `self._uploader.put` again | 4 failures |
| 2 | `_render_pending`'s `if abort():` -> `if False:` | 1 failure |
| 3 | `_render_dirty`'s default abort becomes `lambda: False` | 2 failures |
| 4 | the post-pass `if abort(): return` before `_publish_meta` -> `if False:` | **survived at first**; caught (1 failure) after the mid-pass test asserted no manifest is published for a truncated pass |
| 5 | `WORKER_JOIN_SECONDS` 10.0 -> 45.0 | 1 failure |
| 6 | `coverage_renderer.UPLOAD_READ_TIMEOUT` 25 -> 250 | 1 failure |
| 7 | `state_renderer.UPLOAD_READ_TIMEOUT` 15 -> 150 | 1 failure |
| 8 | `AsyncUploader`'s slot cap check -> `full = False` | 1 failure |
| 9 | a FAILED put confirms the payload anyway | 1 failure |
| 10 | the worker's inner drain loop stops checking `_stop` (`while True`) | **survived at first**; caught (1 failure) after a test queued a second payload behind a stalled PUT and stopped mid-drain |
| 11 | `update_manifest` stops re-reading the manifest inside the lock | 1 failure |
| 12 | the manifest staging file goes back to one shared path | 1 failure |
| 13 | `lock_dir`'s group/other-writable check -> `if False:` | 1 failure |
| 14 | `_lock_opener` drops `O_NOFOLLOW` | 1 failure |
| 15 | `lock_dir` ignores `$XDG_RUNTIME_DIR` | 1 failure |
| 16 | `main()` drops `force=a.force` from the `sync_dir` call | 1 failure |
| 17 | `main()` locks `'run'` instead of `a.name` | 2 failures |
| 18 | a wedged held lock returns 0 instead of 1 | 1 failure |
| 19 | `sync_dir`'s `deadline_seconds` default -> 86400 | 1 failure |
| 20 | `s3_client`'s argument guard -> `if False:` | 1 failure |
| 21 | `LOCK_STALE_SECONDS` x1000 | **survived at first**; caught (1 failure) after the constant was pinned |
| 22 | `update_manifest`'s lock wait never times out | 1 failure |

All seven mutants the round-2 review listed as surviving are in this table
(6, 7, 5, 19, 16, 18, 17) and all seven now fail.

### Verification

- `./core_ws/build.sh marine_web_view && ./core_ws/test.sh marine_web_view`:
  **195 tests, 0 errors, 0 failures** (172 before this pass). Ran with
  `ROS_DOMAIN_ID=101` / `ROS_AUTOMATIC_DISCOVERY_RANGE=OFF`, as the previous
  pass did, so the live renderers and the sim on this host were untouched.
  `marine_interfaces` had to be built in this worktree first.
- Static analysis via the ament wrappers, individually: `test_flake8`,
  `test_pep257`, `test_copyright` all pass (they are also inside the 195).
- **Concurrency was tested for real, not only by mutation.** Every claim
  about the worker is exercised against a transport that blocks inside `put`
  until released: submitting behind a stalled PUT returns in well under a
  second, twenty offers behind a stall produce exactly two sends (the one in
  flight plus the newest), a new key past the cap is refused, `stop()`
  returns `False` promptly with a request in flight, the worker starts no
  further request after a stop, and a busy position key does not overtake the
  track. `coverage_renderer`'s shutdown is tested the same way, with a
  `_publish` that blocks until released.
- No S3 access of any kind: every test injects a stub client or a recording
  transport. Nothing was uploaded to or deleted from the bucket.

### What I could NOT verify here

- **The per-address `connect_timeout` behaviour itself.** I took the round-2
  review's host-verified finding (8 A records; per-`getaddrinfo`-result
  connect timeout) as given rather than re-measuring it. That direction is
  safe: the restructure removes the dependence on any ceiling, so being
  wrong about the exact number changes nothing structural.
- **The STS half of must-fix 1** — that botocore builds the STS client for a
  role-assuming profile with a config carrying only `signature_version`, so
  credential resolution runs under botocore defaults outside the caller's
  `Config`. Nothing in the restructure addresses it and nothing here can
  test it without a role-assuming profile and network. It is now harmless in
  `state_renderer` (credential resolution happens on the worker thread with
  the rest of the request) but it does still mean the first request of a
  process can take longer than the `Config` suggests. Left as-is
  deliberately, not fixed and not claimed fixed.
- **Behaviour under real S3 latency and real load.** The threading is
  exercised against a deterministic stalling stub, which proves the
  structure; it does not prove the node's behaviour over a flaky cellular
  link on the water. What the design guarantees regardless is that no
  network wait happens on the executor thread and no queue can grow.
- **The lock's cross-host behaviour.** `update_manifest`'s lock is local by
  construction; two hosts publishing the same prefix concurrently is
  documented as unsupported rather than tested.
- **`lock_dir` on a host with no `$XDG_RUNTIME_DIR` and no writable
  `~/.cache`.** The `~/.cache` fallback path is exercised only via a
  monkeypatched `$XDG_RUNTIME_DIR`; a cron host where `$HOME` is unwritable
  would raise from `makedirs`, which is loud but untested.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-24 17:02 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-351 at `196dac2`
**PR**: #353 at `196dac2`
**Mode**: pre-push (PR #353 open, base `jazzy`)
**Depth**: Deep (reason: new concurrency in a node that publishes during operations; 4229/182 lines across 15 files; credentials and a public bucket in play)
**Must-fix**: 5 | **Suggestions**: 7
**Round**: 3 | **Ship**: continue — three of the five are new correctness holes created by the round-3 restructure itself (a silently dying upload worker, a flush whose tiles no viewer ever sees, a manifest merge that erases its peers on one transient GET); each is local and small, but they are design/correctness concerns rather than mechanical fixes, so they warrant one more independent read.

### Findings
- [x] (must-fix) upload worker dies silently on any exception outside `transport_errors` or from `_log_error`; `submit` then returns True forever, `confirmed` never advances, `counts()` reports 0 failures and `stop()` reports clean — reproduced on this host — `marine_web_view/s3_upload.py:255-284`
- [x] (must-fix) a truncated shutdown flush skips `_publish_meta`, so tiles it did publish are never announced; `index.html:568` refreshes only when `rendered_tiles` changes, and the process is exiting — regression vs round 2's unconditional meta PUT — `marine_web_view/coverage_renderer.py:1211-1217`
- [x] (must-fix) `update_manifest` merges into `load_manifest`'s blanket-`except` `{}`, so one transient GET inside the lock PUTs `{name: entry}` and erases every other `--name` — the exact lost update the lock was added to prevent — `scripts/refresh_chart_tiles.py:553`
- [x] (must-fix) README tells the operator to clear a wedged lock by "deleting the lock file"; that does not release the `flock`, and the next run creates a new inode and acquires immediately — two concurrent crawls of CCOM — `marine_web_view/README.md:553-554`
- [x] (must-fix) the manifest staging file still lives in the unowned `--workdir` (`/tmp`) that `lock_dir()`'s own docstring refuses to keep a lock in; `open(staged,'w')` follows a planted symlink and `save_manifest` re-reads the path before PUTting it to the public `tiles/manifest.json` — `scripts/refresh_chart_tiles.py:555-558`
- [x] (suggestion) `AsyncUploader`'s worker starts before `create_subscription`/`create_timer`, and `main()` builds the node outside its `try` — the invariant `test_the_render_worker_is_started_after_everything_that_can_raise` codifies for the sibling node, unguarded here — `marine_web_view/state_renderer.py:264-266,604`   (deferred: out of scope for this pass -- the must-fixes were actioned alone; carried to the next round)
- [x] (suggestion) `daemon=True` is load-bearing but unpinned: flipping it hangs the suite indefinitely instead of failing a test — pin it beside the `UPLOAD_STOP_SECONDS == 5.0` assertion — `marine_web_view/s3_upload.py:210-212`   (deferred: out of scope for this pass -- the must-fixes were actioned alone; carried to the next round)
- [x] (suggestion) a manifest-lock timeout raises out of `main()` uncaught after every tile is already uploaded, costing the next run a full ~5,839-tile re-crawl — catch, report the consequence, retry — `scripts/refresh_chart_tiles.py:544-552`   (deferred: out of scope for this pass -- the must-fixes were actioned alone; carried to the next round)
- [x] (suggestion) "All-or-nothing, as `aws s3 sync` was" overstates it: tiles PUT before the failure are already live behind CloudFront; only the manifest is withheld — `scripts/refresh_chart_tiles.py:862`   (deferred: out of scope for this pass -- the must-fixes were actioned alone; carried to the next round)
- [x] (suggestion) `_confirmed` is never pruned, so the "no call pattern can grow this unboundedly" claim covers `_pending` only — `marine_web_view/s3_upload.py:173-176,280`   (deferred: out of scope for this pass -- the must-fixes were actioned alone; carried to the next round)
- [x] (suggestion) `stop()`'s stated worst case omits the unbudgeted `_publish_meta` PUT that follows a flush which did not abort — `marine_web_view/coverage_renderer.py:1055-1060`   (deferred: out of scope for this pass -- the must-fixes were actioned alone; carried to the next round)
- [x] (suggestion) `_pending`'s comment says `key -> (payload, content_type, cache)`; the stored value is a 4-tuple including `tag` — `marine_web_view/s3_upload.py:200`   (deferred: out of scope for this pass -- the must-fixes were actioned alone; carried to the next round)

### Verified, not findings
- **Mutation sample re-run independently** (scratch copy, `__pycache__` purged, full suite each time): mutants 2, 4, 5, 8, 9, 10 from the fix pass's table all fail exactly as reported. Three further mutants of my own — `stop()` not waking the worker, `confirmed` reporting the pending tag, and `submit` accepting past the cap — are also caught. The guards bind.
- **Ordering.** The 1 s position stream cannot starve the 30 s track: a popped key leaves the map, so its re-submission re-enters at the tail and the two alternate. The claim in the `_run` comment holds.
- **Logging after shutdown.** Probed directly on Jazzy: `get_logger().error()` and `get_clock().now()` on a destroyed node, and after `rclpy.shutdown()`, both succeed. The abandoned-thread-logs-after-teardown hazard does not fire here.
- **Static analysis** clean: `test_flake8`, `test_pep257`, `test_copyright` pass; the eight `B902` blind-except hits are the documented containment backstops.
- **Round-2 must-fix 1, STS half** — acceptable as left. A role-assuming profile would build its STS client under botocore's own defaults outside the caller's `Config`, but nothing now depends on a per-request ceiling: state_renderer's PUTs are off the executor thread, coverage_renderer aborts between tiles, and the cron script is bounded by the run lock. The shipped profile is an IAM user, so the path is not even taken. Worth one sentence in `_boto3_client` so round 4 does not rediscover it.
- **`$XDG_RUNTIME_DIR` / `~/.cache` fallbacks** behave: a symlinked lock dir is refused by the `lstat` check; an unwritable `$HOME` or `$XDG_RUNTIME_DIR` exits non-zero with an unhandled `OSError` traceback (loud, if untidy). `LOCK_STALE_SECONDS` (6 h) clears `SYNC_DEADLINE_SECONDS` (1 h) with room; a legitimately long run past 6 h would false-alarm, which checking the recorded pid with `os.kill(pid, 0)` would settle.

### Out of scope for this PR
`coverage_renderer` has no "no catalog received" warning, and the shipped `coverage_namespace` default (`/ben/sensors/mbes/cube_bathymetry`) does not match what BizzyBoat publishes (`/bizzy/sensors/m3/cube_bathymetry`), so a wrong namespace is indistinguishable from an idle sonar — and `meta.json` reports `status: ok` throughout. This will be hit on first deployment to the operator station, but it is unrelated to the transport swap: file it as a follow-up rather than widening this PR. The same applies to the broader `--workdir` exposure (tile files are read with symlink-following `open()` and PUT to the public prefix) — pre-existing, and larger than the one-line staging-file fix above.

## Implementation
**Status**: complete
**When**: 2026-08-24 19:38 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-351 at `5535b38`
**PR**: #353 at `5535b38`
**Addressed**: `## Local Review (Pre-Push)` of 2026-08-24 17:02 -04:00 (round 3, `196dac2`) — its five must-fixes
**Commits**: `c6c97b8`, `760b810`, `8d42b50`, `5535b38`
**Scope**: must-fixes only; the seven suggestions in the same entry were placed out of scope for this pass by the dispatching orchestrator and are checked as deferred, not addressed.

### Actions
- [x] a dying upload worker is now visible — `marine_web_view/s3_upload.py:255-330`, `marine_web_view/state_renderer.py:559-580,596-612`
- [x] a truncated pass publishes its manifest as `truncated_render` — `marine_web_view/coverage_renderer.py:1214-1227`
- [x] the manifest merge reads strictly and never erases its peers — `scripts/refresh_chart_tiles.py:494-560`
- [x] README's wedged-lock remedy is "kill the holder", not "delete the file" — `marine_web_view/README.md:554-569`
- [x] the manifest is PUT from memory; no staging file under `--workdir` — `scripts/refresh_chart_tiles.py:545-560,588-592`
- [x] (deferred: out of scope for this pass — orchestrator scoped it to the must-fixes) the seven suggestions from the round-3 entry, checked in place with the same annotation

### Finding 1 — direct-execution proof

The reviewer's reproduction, re-run here as a standalone script against a
transport that raises OUTSIDE `transport_errors`
(`S3Uploader(..., transport_errors=(ValueError,))` over a client raising a
custom exception), one submit, half a second, a second submit:

**Before** (`dd78bac`):

```
Exception in thread s3-upload:  ... _Boom: endpoint returned something botocore does not model
worker alive        : False
first submit        : True
later submit        : True
confirmed(position) : None
counts (w, f, d)    : (0, 0, 0)
log_error calls     : 0 []
stop() reports clean: True
```

**After**:

```
worker alive        : True
first submit        : True
later submit        : True
confirmed(position) : None
counts (w, f, d)    : (0, 2, 0)
log_error calls     : 2 [('live/position.geojson', "_Boom(...)")]
dead()              : None
stop() reports clean: True
```

The failure is now counted and logged, and the worker survives to serve the
next tick. A second probe drove a failure the per-send guard cannot see (a
`MemoryError` from the bookkeeping under the worker's own lock):

```
worker alive        : False
dead()              : MemoryError: out of memory in the worker loop
submit after death  : False
counts (w, f, d)    : (0, 1, 1)
stop() reports clean: False
```

So a death that does happen is reported on every channel a caller reads,
and `state_renderer` turns that into a throttled error on every tick plus
one at shutdown (`test_a_dead_upload_worker_reaches_the_operator`).

### Verification

- **Build + test**: `./core_ws/build.sh marine_web_view` clean;
  `./core_ws/test.sh marine_web_view` → **203 tests, 0 errors, 0 failures,
  0 skipped** (195 before this pass; 8 added). Run with
  `ROS_DOMAIN_ID=99`, `ROS_LOCALHOST_ONLY=1`,
  `ROS_AUTOMATIC_DISCOVERY_RANGE=OFF` so nothing touched the live renderers.
- **Static analysis**: `test_flake8` (ament_flake8 against the ament
  config), `test_pep257`, `test_copyright` all pass. Two `E116` hits
  introduced by this pass were fixed, not suppressed.
- **Mutation-checked — 20 mutants, all caught.** Applied one at a time to a
  scratch copy with `__pycache__` purged, full suite re-run each time
  (baseline 200 non-lint tests green):

| # | Mutation | Result |
|---|---|---|
| 1 | per-send guard removed (worker dies on a raising put) | 2 failures |
| 2 | per-send guard stops counting the failure | 2 failures |
| 3 | per-send guard stops reporting the failure | 1 failure |
| 4 | outer death guard removed | 1 failure |
| 5 | the death is not counted as a failure | 1 failure |
| 6 | `submit` accepts payloads a dead worker will never send | 2 failures |
| 7 | `stop()` calls a dead worker a clean shutdown | 2 failures |
| 8 | a raising `log_error` kills the worker again | 1 failure |
| 9 | truncated pass publishes no manifest (the round-3 regression) | 1 failure |
| 10 | a truncated pass reports `status: ok` | 1 failure |
| 11 | strict read falls back to `{}` like the forgiving one | 2 failures |
| 12 | `update_manifest` reads forgivingly again | 2 failures |
| 13 | every error counts as "no manifest yet" | 1 failure |
| 14 | a non-object manifest is merged into anyway | 1 failure |
| 15 | malformed JSON swallowed under strict too | 1 failure |
| 16 | manifest staged to a hardcoded `/tmp` path | SURVIVED → bound (see below) |
| 16b | `save_manifest` re-reads a staged path again (full revert) | 3 failures |
| 16c | the merged manifest is staged through a file first | 1 failure |
| 17 | the node stops reporting a dead worker on every tick | 1 failure |
| 18 | shutdown stops distinguishing a dead worker from a slow one | 1 failure |

  Mutant 16 survived on the first pass: it staged to a hardcoded `/tmp` path
  rather than to the test's `--workdir`, which the existing assertion could
  not see. `test_two_names_do_not_erase_each_other_from_the_manifest` now
  also asserts the lock directory holds nothing but `manifest.lock`, which
  kills 16c (staging through *any* file) — and 16b shows the bytes-only
  `save_manifest` contract is bound. The unowned-workdir path itself is gone
  structurally: `update_manifest` no longer takes a workdir.
- **No test weakened.** Three existing assertions changed, all in the
  direction the review asked for: `test_a_stop_part_way_through_a_pass_returns_the_rest`
  now requires the manifest it used to forbid (and requires it to say
  `truncated_render`); `test_the_manifest_is_published_with_no_cache` passes
  bytes because `save_manifest` no longer takes a path; the shared-manifest
  double now raises a *shaped* `NoSuchKey` for a first run, so a transient
  failure is distinguishable from an absent object.
- **No test hangs.** `daemon=True` was left unpinned (that suggestion is out
  of scope), so nothing in this pass can wedge the harness on it. Every new
  thread assertion polls through the existing `_settle` helper with a
  timeout.
- **Not verified**: nothing was read from or written to the S3 bucket —
  every S3 interaction in this pass is against injected doubles. The
  `truncated_render` string is asserted against `index.html`'s renderer by
  reading the page source (it prints `meta.status` verbatim with underscores
  replaced), not by loading the page in a browser.

### Notes for the re-review

- `_render_dirty`'s *entry* check still returns without publishing anything:
  a pass that aborts before rendering has nothing to announce. Only the
  mid-pass truncation publishes. Two existing tests pin the first case.
- The strict manifest read fails the run loudly on a read error, which costs
  *this* `--name` a re-crawl next run. That is the deliberate trade (one
  name's re-crawl vs. every other name's), and it is the same uncaught-raise
  exit the round-3 entry already flagged as a suggestion at
  `refresh_chart_tiles.py:544-552`.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-24 19:52 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-351 at `b7d3beb`
**Mode**: pre-push
**Depth**: Deep (reason: concurrency/lifecycle containment on a public-facing upload path; round-4 delta `dd78bac..b7d3beb`)
**Must-fix**: 1 | **Suggestions**: 6
**Round**: 4 | **Ship**: recommended — the five round-3 must-fixes are all fixed and independently mutation-bound (21/21 mutants killed, 203 tests pass); the single remaining must-fix is a two-number comment correction, and nothing new in the containment code is a defect.

### Findings
- [ ] (must-fix) stale per-PUT ceiling claim survives next to a constant this PR introduced: the comment says "The 45 s join above it" (the join is now `WORKER_JOIN_SECONDS = 10.0`, defined *below*) and "one 30 s-capped upload per dirty tile" (the 30 s subprocess cap is gone and `_boto3_client` deliberately asserts no ceiling exists) — same class as round-1 must-fix 1 and round-2 must-fix 1, recurring, not new — `marine_web_view/marine_web_view/coverage_renderer.py:149-155`
- [ ] (suggestion) `_log_upload_failure` is the node's only unthrottled error path; a persistent AccessDenied/NoSuchBucket logs one ERROR per second forever, which is the steady state round 4's containment now makes expected — add `throttle_duration_sec=30.0` to match `_queue`/`stop` — `marine_web_view/marine_web_view/state_renderer.py:617`
- [ ] (suggestion) `coverageText` returns before the `age > COVERAGE_DEAD_S` branch whenever `status !== 'ok'`, so a renderer that exited after a truncated flush shows "truncated render" forever with no age — and this PR makes `truncated_render` the common last word of a run; only layer opacity says it is dead — `marine_web_view/web/index.html:523-531`
- [ ] (suggestion) `WORKER_JOIN_SECONDS`'s comment still says the join "only has to cover the request it is already inside"; since round 4 an aborted scheduled pass issues one more unbudgeted manifest PUT first, and losing the join skips the final flush — `stop()`'s docstring was updated for this, the constant's was not — `marine_web_view/marine_web_view/coverage_renderer.py:164-168`
- [ ] (suggestion) an abort during `_update_datum_offset()` or on `_render_pending`'s first check publishes `truncated_render` having rendered nothing (`rendered_tiles` unmoved); gate the publish on progress this pass, or narrow the test docstring that claims such a pass "has nothing to announce" — `marine_web_view/marine_web_view/coverage_renderer.py:1215-1226`, `test/test_render_pass.py:437-443`
- [ ] (suggestion) `update_manifest`'s new raise leaves `main()` as a bare traceback, unlike every other failure path there (`print(..., file=sys.stderr); return 1`); exit status is still non-zero, so this is cron-mail presentation only — `marine_web_view/scripts/refresh_chart_tiles.py:927`
- [ ] (suggestion) `.agents/README.md`'s package inventory does not list `marine_web_view` at all, so the new `python3-boto3` dependency lands in a package the repo's agent guide does not describe — pre-existing gap, follow-up issue rather than this PR
