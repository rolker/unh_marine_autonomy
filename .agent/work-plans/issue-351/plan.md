# Plan: marine_web_view: upload to S3 via boto3 instead of shelling out to the AWS CLI

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/351

## Operator decision (scopes this plan)

The Issue Review found a **third** `aws` shell-out beyond the two the issue
names, in `marine_web_view/scripts/refresh_chart_tiles.py`. The operator has
decided to convert **all three** sites and remove the AWS CLI dependency
entirely, including reimplementing `aws s3 sync`. This plan does not carry a
"keep the CLI for the script" branch — the README's "Runtime prerequisite:
the AWS CLI" section is removed outright, `package.xml` drops the explanatory
comment about the undeclarable `awscli` key, and `<depend>python3-boto3</depend>`
is added.

## Context

Three call sites shell out to `aws`:

1. `marine_web_view/marine_web_view/state_renderer.py:493-518` (`_put`) —
   `aws s3 cp -` reading stdin, always passes `--profile self.profile`
   (default `p11-renderer`), 20 s timeout.
2. `marine_web_view/marine_web_view/coverage_renderer.py:1262-1290`
   (`_publish`) — `aws s3 cp -` reading stdin, passes `--profile` only when
   `self.profile` is truthy (otherwise relies on boto3's/CLI's default
   credential chain — there's an explicit warning log for this case at
   construction), 30 s timeout.
3. `marine_web_view/scripts/refresh_chart_tiles.py` — an offline, cron-safe,
   **standalone** script (no `rclpy`, no `from marine_web_view import ...`)
   that pre-renders a CCOM bathymetry tile pyramid and uploads it. Two call
   sites: `load_manifest()` (`aws s3 cp -` reading `manifest.json`) and
   `main()` (`aws s3 sync <outdir> s3://<bucket>/tiles/<name>/
   --content-type image/png --cache-control public,max-age=604800
   --only-show-errors`, 3600 s timeout, then `aws s3 cp` of the rewritten
   `manifest.json` with `--cache-control no-cache`). Default profile
   `ccom-jhc` (not `p11-renderer` — a different, more privileged profile;
   the script's own `--profile` help text says `p11-renderer` cannot write
   `tiles/`).

`python3-boto3` is confirmed to resolve via rosdep (`apt` candidate
`1.34.46+dfsg-1ubuntu1` on noble) and is not currently installed on this
host — implementers must `sudo apt install python3-boto3` (or
`rosdep install`) before running/testing this locally. `awscli` has no apt
candidate on noble, which is why it could never be declared.

The `tiles/` prefix in the target bucket is currently **empty (0 objects)**:
the CLI's incremental "skip unchanged" comparison inside `sync` has
effectively never been exercised in production, so there is no existing
on-bucket state whose exact comparison semantics we're obligated to match
bit-for-bit — we're free to choose a comparison rule and state it, rather
than reverse-engineer the CLI's.

## Approach

### 1. Shared boto3 upload helper for the two ROS nodes

New module `marine_web_view/marine_web_view/s3_upload.py`:

```python
class S3Uploader:
    def __init__(self, bucket, profile=None, connect_timeout=5, read_timeout=25):
        self.bucket = bucket
        config = botocore.config.Config(
            connect_timeout=connect_timeout, read_timeout=read_timeout,
            retries={'mode': 'standard', 'max_attempts': 4})
        session = boto3.Session(profile_name=profile)
        self._client = session.client('s3', config=config)

    def put(self, payload, key, content_type, cache_control):
        """Upload bytes to key. Returns (True, None) or (False, exc)."""
        try:
            self._client.put_object(
                Bucket=self.bucket, Key=key, Body=payload,
                ContentType=content_type, CacheControl=cache_control)
            return True, None
        except (botocore.exceptions.ClientError,
                botocore.exceptions.BotoCoreError) as exc:
            return False, exc
```

- **Profile pass-through, not coalescing, is the helper's contract** — the
  helper does exactly `boto3.Session(profile_name=profile)` with whatever it
  is given. Each node keeps deciding what to pass, which is what preserves
  today's asymmetry:
  - `state_renderer` constructs `S3Uploader(self.bucket, profile=self.profile)`
    — `self.profile` is never coalesced, so an operator-configured empty
    string is passed through to `boto3.Session(profile_name='')` exactly as
    today's `--profile ''` would be, and fails the same way (`ProfileNotFound`
    instead of `SubprocessError`, but the same operator mistake produces the
    same class of failure).
  - `coverage_renderer` constructs `S3Uploader(self.bucket, profile=self.profile or None)`
    — preserves the existing "empty profile means fall through to the default
    credential chain" behavior and its startup warning log
    (`coverage_renderer.py:382`, unchanged).
  - This is a **deliberate, stated preservation** of the asymmetry, not an
    oversight — unifying it would be a behavior change on top of a
    dependency swap, and nothing in the issue asks for that.
- **Timeouts**: `connect_timeout`/`read_timeout` replace the CLI's
  process-level `timeout=20`/`timeout=30`. `state_renderer` passes
  `read_timeout=15` (leaves headroom under its 20 s ceiling once connect
  time is included); `coverage_renderer` passes `read_timeout=25` (under its
  30 s ceiling). A `botocore.exceptions.ReadTimeoutError` /
  `ConnectTimeoutError` (both `BotoCoreError` subclasses) is the typed
  equivalent of today's `subprocess.TimeoutExpired`.
- **Retry/backoff — stating what should exist, not preserving what doesn't**:
  today there is no in-call retry at all — one `subprocess.run` attempt, a
  failure counter increment, and reliance on the *next scheduled pass* (the
  ROS timer / coverage's dirty-tile re-queue) to retry. This plan does
  **not** add a custom in-call retry loop (that would be a scope increase
  and could interact badly with the per-call timeouts above). It does turn
  on botocore's built-in `retries={'mode': 'standard', 'max_attempts': 4}`
  in the `Config` — this is SDK-internal exponential backoff for transient
  errors (throttling, 5xx, connection resets) with zero custom code, and it
  is strictly additive: today's CLI-based calls got no automatic retry of
  any kind, so this is new robustness without new complexity in our code.
  Anything that exhausts the SDK's retries, or is non-retryable
  (`AccessDenied`, `NoSuchBucket`), still falls through to the existing
  failure-counter-and-next-pass behavior — unchanged.
- **Error mapping**: `_put`/`_publish` change from checking
  `result.returncode != 0` / logging `result.stderr` to checking the `exc`
  returned by `S3Uploader.put()`. For a `ClientError`, log
  `exc.response['Error'].get('Code')` and `.get('Message')` (distinguishes
  `AccessDenied` / `NoSuchBucket` / `SlowDown`/`ThrottlingException` in the
  log line, which exit-code parsing of CLI stderr could not do reliably).
  For a `BotoCoreError` (timeout, connection failure), log `str(exc)`. Both
  branches still call the existing failure counter
  (`self._failures += 1` / `self._note_failure()`) — no change to counting
  or to what triggers a retry.

### 2. `state_renderer.py`

- Remove `import subprocess`.
- Add `import botocore.exceptions` and `from marine_web_view.s3_upload import S3Uploader`.
- Construct `self._uploader = S3Uploader(self.bucket, profile=self.profile, read_timeout=15)`
  once, alongside the other parameter reads in `__init__` (skip entirely when
  `dry_run` — matches "no AWS access at all" requirement).
- Rewrite `_put(payload, key, max_age)` to call
  `ok, exc = self._uploader.put(payload.encode(), key, 'application/geo+json', 'max-age={}'.format(max(1, int(max_age))))`
  and log per the error-mapping rule above on failure.

### 3. `coverage_renderer.py`

- Remove `import subprocess`.
- Add `import botocore.exceptions` and `from marine_web_view.s3_upload import S3Uploader`.
- Construct `self._uploader = S3Uploader(self.bucket, profile=self.profile or None, read_timeout=25)`
  once in `__init__`, only when not `dry_run` (mirrors the existing
  `if not self.dry_run and not _is_usable_bucket(...)` guard placement).
- Rewrite `_publish(payload, key, content_type='image/png', max_age=None)`:
  keep the `if self.dry_run: return self._write_local(...)` branch verbatim,
  then call `self._uploader.put(payload, key, content_type, 'max-age={}'.format(max_age))`
  and map errors per the rule above.
- Update the stale docstring at `test_an_unusable_bucket_is_rejected...`'s
  target, `_is_usable_bucket`'s caller context in `coverage_renderer.py`
  (the comment mentioning "a 30 s-capped subprocess in a retry loop" at
  ~line 374) to describe the boto3 call instead of "subprocess" — see
  Documentation Impact.

### 4. `refresh_chart_tiles.py` — reimplementing `sync`, with concurrency

This is the one place a naive rewrite regresses badly, so spelling it out:

- **Object count** (default `--bbox`/`--zmin 10`/`--zmax 16`): computed from
  the script's own `tile_list()` logic against `DEFAULT_BBOX = (-70.90,
  42.92, -70.50, 43.15)` — **z10: 4, z11: 9, z12: 25, z13: 80, z14: 304,
  z15: 1110, z16: 4307, total ≈ 5,839 tiles** for one `--name` pyramid. A
  serial `put_object`-per-file loop at a realistic ~150 ms/object (TLS
  handshake reuse aside, S3 PUT latency) would take **~15 minutes** just to
  upload; `aws s3 sync`'s default 10-way concurrency brings that to
  **~1-2 minutes**. This must not regress.
- **Concurrency**: a `concurrent.futures.ThreadPoolExecutor` (boto3 clients
  are documented thread-safe for concurrent calls; one shared `S3Uploader`-style
  client, many worker threads). New `--concurrency` CLI arg, default `16`
  (CLI's default is 10; small PNGs and a single bucket mean headroom to go a
  little higher, but this stays operator-tunable without a code change).
  Each worker calls `client.upload_file(local_path, BUCKET, key,
  ExtraArgs={'ContentType': 'image/png', 'CacheControl':
  'public,max-age=604800'})` — `upload_file` is used (not `put_object`) so a
  future oversized tile transparently gets multipart handling from
  `boto3.s3.transfer.TransferConfig`, though at ~256x256 PNG sizes every
  object today is a single-part PUT regardless.
- **"Already uploaded" comparison rule** (new code — no existing on-bucket
  data to match, per the empty-`tiles/`-prefix fact above): one
  `list_objects_v2` paginator call over `tiles/<name>/` up front builds a
  `{key: size}` dict (one cheap list call instead of one `head_object` per
  file). A local file is **skipped** if the remote key exists **and its
  size matches exactly**; otherwise it is uploaded. This is deliberately
  size-only, not size+mtime like the CLI: local mtimes are always "now"
  (freshly downloaded in this same run), so an mtime comparison would never
  skip anything and buys nothing; size is what actually distinguishes "same
  render" from "compilation changed, different band boundaries." State this
  choice in the code comment — it is a simplification enabled by the
  never-yet-exercised incremental path, not an attempt at CLI parity.
- **Failure handling**: preserve today's all-or-nothing semantics — any
  per-object upload exception (after the SDK's own `standard`-mode retries
  are exhausted) counts as a sync failure; if any object failed, print
  `'upload failed'` to stderr and `return 1` **without** publishing the
  rewritten `manifest.json` (mirrors `if up.returncode != 0: return 1`
  exactly — `aws s3 sync` likewise returns nonzero on any object failure).
  Print running counts (`written`/`skipped(unchanged)`/`failed`) in the same
  style as the existing tile-fetch loop's progress prints.
- **No `--delete` today, none added**: verified the current `sync` call
  carries no `--delete` flag, so it is upload/update-only; the reimplementation
  makes zero deletion calls, matching that.
- **`load_manifest()` and the manifest `cp`**: both become single
  `put_object`/`get_object` calls (`get_object` wrapped in try/except
  returning `{}` on any error, matching today's blanket
  `except Exception: pass`; `put_object` with `CacheControl='no-cache'`,
  `ContentType='application/json'`).
- **Shared helper, or standalone?** — this script deliberately imports
  nothing from `marine_web_view` (no `rclpy`, no package imports at all) so
  it can run from cron without the ROS overlay sourced; importing
  `marine_web_view.s3_upload` would silently make that no longer true
  (it would only resolve when `install/setup.bash` has been sourced,
  putting the package on `sys.path`). **Decision: the script gets its own
  small, self-contained boto3 sync implementation** (~40-60 lines:
  `_sync_dir(client, local_dir, bucket, prefix, extra_args, concurrency)`),
  not a shared import, to preserve the "runs standalone" property. This is
  intentional duplication of a small amount of upload glue, not the
  `S3Uploader` class itself, which has different concerns (per-node
  parameter wiring, dry-run gating) than this script needs anyway.
- `import boto3` and `import botocore.exceptions` replace `import subprocess`
  at the top; drop the `subprocess` import once the two call sites are gone.

### 5. `package.xml`

- Add `<depend>python3-boto3</depend>` (botocore comes in transitively via
  apt; not declared separately, matching how `python3-numpy`/`python3-pil`
  are declared without their own transitive deps).
- Delete the multi-line comment block explaining why `awscli` cannot be
  declared (lines currently preceding `<test_depend>`).

### 6. `README.md`

- Delete the "Runtime prerequisite: the AWS CLI" section (lines 14-34)
  outright — no CLI dependency remains anywhere in the package, including
  `refresh_chart_tiles.py`.
- The "Nothing else needs it. With `dry_run:=true` ..." sentence about
  `dry_run`/`local_dir` needing no AWS access is preserved (moved into
  wherever the node docs currently sit, or dropped if redundant with
  existing per-node parameter docs — implementer's call, verify against the
  current README structure before deciding).

### 7. Tests

- **New `test/test_s3_upload.py`** — unit tests for `S3Uploader` against a
  stubbed `boto3.Session`/client (use `unittest.mock.patch` on
  `s3_upload.boto3.Session`, or `botocore.stub.Stubber` — implementer's
  choice, but must exercise: (a) success path returns `(True, None)` and
  calls `put_object` with the exact `Bucket`/`Key`/`Body`/`ContentType`/
  `CacheControl` given; (b) a `ClientError` with response code
  `AccessDenied` is returned, not raised; (c) a `ClientError` with response
  code `SlowDown`/`ThrottlingException` is returned, not raised; (d) a
  `profile=None` vs `profile=''` vs `profile='p11-renderer'` each reach
  `boto3.Session(profile_name=...)` unchanged — pins the pass-through
  contract in section 1.
- **Rewrite `test/test_render_pass.py::test_the_upload_stamps_the_max_age_it_is_given`**
  to stub `CoverageRenderer._uploader.put` (or patch
  `coverage_renderer.S3Uploader`) instead of `coverage_renderer.subprocess.run`,
  asserting the `cache_control` string reaching the stub for both the
  manifest (`max_age=5`) and a tile call (default `cache_control`) — same
  assertions, different seam.
- **New `test/test_chart_tile_sync.py`** — the issue explicitly calls out
  the sync comparison logic as new, untested code. Extract `_sync_dir` (and
  the size-comparison helper) so it's callable with an injected fake client
  (a small stand-in exposing `get_paginator('list_objects_v2')` returning
  scripted pages, and `upload_file` recording calls). Cover: (a) a file
  whose remote size matches is skipped; (b) a file with no remote
  counterpart, or a mismatched size, is uploaded with the given
  `ExtraArgs`; (c) concurrency actually parallelizes — assert more than one
  thread name appears in a recorded-caller list when uploading >1 file with
  `concurrency=4` and an artificial per-call `time.sleep`, the same pattern
  `test_render_pass.py::_Threaded` already uses to prove the render pass
  runs off the executor thread; (d) one failing upload (raise from the fake
  client) surfaces as a nonzero failure count and the caller does not then
  attempt the manifest publish (test at the `main()`-adjacent level, or
  by checking `_sync_dir`'s return contract that `main()` gates on).
- Existing `test_tile_ingest.py::test_an_unusable_bucket_is_rejected_rather_than_retried`
  needs no change (tests `_is_usable_bucket` directly, independent of
  transport).

## Files to Change

| File | Change |
|------|--------|
| `marine_web_view/marine_web_view/s3_upload.py` | New — shared `S3Uploader` helper for the two ROS nodes. |
| `marine_web_view/marine_web_view/state_renderer.py` | Replace `subprocess`-based `_put` with `S3Uploader`; construct uploader in `__init__`. |
| `marine_web_view/marine_web_view/coverage_renderer.py` | Replace `subprocess`-based `_publish` upload branch with `S3Uploader`; construct uploader in `__init__`; fix stale "30 s-capped subprocess" comment. |
| `marine_web_view/scripts/refresh_chart_tiles.py` | Replace `load_manifest`'s `aws s3 cp` and `main`'s `aws s3 sync` + `aws s3 cp` with a self-contained boto3 implementation, incl. a concurrent, size-compared sync helper. |
| `marine_web_view/package.xml` | Add `<depend>python3-boto3</depend>`; remove the `awscli`-undeclarable comment block. |
| `marine_web_view/README.md` | Remove the "Runtime prerequisite: the AWS CLI" section. |
| `marine_web_view/test/test_s3_upload.py` | New — `S3Uploader` unit tests against a stubbed client. |
| `marine_web_view/test/test_render_pass.py` | Rewrite the CLI-arg-inspecting max-age test to inspect the stubbed boto3 call instead. |
| `marine_web_view/test/test_chart_tile_sync.py` | New — sync comparison + concurrency + failure-gating tests. |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Enforcement over documentation | This is the whole point of the issue: `<depend>python3-boto3</depend>` makes the manifest declare a dependency that actually resolves, instead of a README section standing in for an undeclarable one. |
| A change includes its consequences | All three shell-out sites converted in one PR (per operator decision), so the README's CLI section can be deleted truthfully rather than "revised to scope to the script" as the Issue Review's fallback suggested. |
| Test what breaks | New tests target the actual failure modes (`AccessDenied`, throttling, timeout, sync comparison) that were previously unreachable behind a subprocess mock. |
| Only what's needed | No behavior changes beyond the transport swap and the two explicitly-decided items (profile asymmetry preserved as-is; retry gets SDK-level backoff, not a new custom loop). Concurrency in the sync reimplementation is not scope creep — it is required to avoid a regression, not an enhancement. |
| Improve incrementally | Single PR; three call sites are mechanically the same shape (build args, call, check exception/returncode) so reviewing them together is reasonable rather than three PRs for the same swap. |
| Capture decisions | This plan documents the profile-asymmetry preservation, the retry/backoff scope, the sync comparison rule, and the standalone-script decision inline — durable in the PR/plan without needing a dedicated ADR (implementation-detail swap, not an architectural decision, matching the Issue Review's assessment). |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0009 — Python package management policy | Yes | `<depend>python3-boto3</depend>` via rosdep/apt is Tier 1 (ROS runtime dependency, apt-resolvable) — exactly the prescribed pattern. Verified: `rosdep resolve python3-boto3` → apt, candidate `1.34.46+dfsg-1ubuntu1` on noble. |
| 0008 — ROS 2 conventions | Yes (lightly) | New module `s3_upload.py` follows the existing flat package layout (sibling to `gggs.py`, `tiles.py`, `reconciler.py`); no new node, no new topic/param surface. |
| 0013 — progress.md entry vocabulary | Yes | This plan and its progress.md entry use the ADR-0013 vocabulary. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| Upload transport (subprocess → boto3) in both nodes | `package.xml` dependency declaration | Yes |
| Upload transport in `refresh_chart_tiles.py` | README's AWS CLI section (now removable) | Yes |
| `_put`/`_publish` error handling (returncode → typed exception) | Log message format operators may grep for in the field | Yes — new log lines documented above; not a breaking change to log *destinations*, just content. Flag to operators is out of scope for this plan (a one-line dev-log note is enough, not a separate comms task). |
| `refresh_chart_tiles.py`'s sync step | Its own printed progress-stats format | Yes — kept in the same style (`written`/`skipped`/`failed` counts) so cron log-watchers don't need to change what they grep for. |
| Removing the `awscli`-undeclarable comment from `package.xml` | Nothing else references that comment | Yes — verified via grep, no other file quotes it. |
| boto3 not yet installed on this dev host | Implementer must install before testing | Yes — called out explicitly in Context; not silently assumed. |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `marine_web_view/README.md`'s
  "Runtime prerequisite: the AWS CLI" section (deleted); the
  `package.xml` comment block explaining the undeclarable `awscli` rosdep
  key (deleted); the in-code comment near `coverage_renderer.py`'s bucket
  guard that currently says "a 30 s-capped subprocess in a retry loop"
  (rephrase to describe the boto3 call, since the retry loop and 30 s
  ceiling both still exist conceptually but the mechanism named is wrong
  once this lands).
- **Agent-instruction candidates** (proposals only — operator decides):
  None. This is a single-package dependency/transport swap; it doesn't
  surface a new cross-project pattern beyond what ADR-0009 already
  documents (which already names `python3-boto3` as exactly this kind of
  Tier 1 case in spirit, even though it predates this specific package).

## Open Questions

- **README structure for the `dry_run`/no-AWS-access sentence**: once the
  "Runtime prerequisite" section is deleted wholesale, should the one
  sentence about `dry_run` needing no AWS access move under each node's own
  parameter docs, or is it already redundant with per-node `dry_run`
  parameter descriptions elsewhere in the README? Implementer should check
  the current README's node sections before landing the deletion and use
  judgment; flagging rather than guessing since it's a documentation
  completeness call, not a behavior one.
- **`--concurrency` default of 16 for the sync reimplementation**: reasonable
  given ~256x256 PNGs and S3's per-prefix request-rate headroom, but if the
  operator wants to match the CLI's default of 10 exactly for parity during
  the cutover, that's a one-line default change — flagging the choice
  rather than assuming 16 is uncontroversial.

## Estimated Scope

Single PR. Three call sites, one new shared helper module, one
self-contained script rewrite, three touched/new test files, one
`package.xml` line, one README section removal. No new ROS interfaces
(topics/services/actions/params) — pure transport-layer swap plus the
sync-comparison logic the issue itself asks to be tested.
