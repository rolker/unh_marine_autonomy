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
`1.34.46+dfsg-1ubuntu1` on noble). `awscli` has no apt candidate on noble,
which is why it could never be declared. boto3 is **not installed on the dev
host** where this was implemented; the operator installs it via
`rosdep install`. The implementation therefore imports boto3 **lazily** (see
section 1) and every test injects a stub client, so the whole suite runs
without the SDK present — which is also the property the README promises for
a `dry_run` host.

**CORRECTION to the original framing (Plan Review, must-fix):** the `tiles/`
prefix is NOT per-compilation. `--name` (default `bathy4m`) is a FIXED S3 key
prefix; only the CCOM *service name* carries the compilation date. The script
tracks a `rule_hash` over `RAMP`/`MAX_DEPTH`/`STEP` and, when it changes,
deliberately re-renders and re-uploads **into that same prefix**. A size-only
comparison would silently skip a recoloured PNG of identical byte size,
serving stale colours indefinitely. The comparison rule in section 4 is
therefore **content-hash (MD5 vs ETag)**, not size.

## Approach

### 1. Shared boto3 upload helper for the two ROS nodes

New module `marine_web_view/marine_web_view/s3_upload.py`:

```python
def _boto3_client(profile, connect_timeout, read_timeout):
    """Lazy import; returns (client, transport_error_classes)."""
    import boto3
    from botocore.config import Config
    from botocore.exceptions import BotoCoreError, ClientError
    config = Config(connect_timeout=connect_timeout,
                    read_timeout=read_timeout,
                    retries={'mode': 'standard', 'max_attempts': 1})
    return (boto3.Session(profile_name=profile).client('s3', config=config),
            (ClientError, BotoCoreError))


class S3Uploader:
    def __init__(self, bucket, profile=None, connect_timeout=5,
                 read_timeout=25, client=None, transport_errors=None):
        self.bucket = bucket
        if client is None:
            client, transport_errors = _boto3_client(
                profile, connect_timeout, read_timeout)
        elif transport_errors is None:
            transport_errors = (Exception,)   # test seam
        self._client = client
        self._errors = transport_errors

    def put(self, payload, key, content_type, cache_control):
        """Upload bytes to key. Returns (True, None) or (False, exc)."""
        try:
            self._client.put_object(
                Bucket=self.bucket, Key=key, Body=payload,
                ContentType=content_type, CacheControl=cache_control)
        except self._errors as exc:
            return False, exc
        return True, None
```

- **boto3 is imported LAZILY, inside `_boto3_client`**, not at module scope.
  Both renderers import this module unconditionally, but a `dry_run` /
  `local_dir` host never constructs an uploader and needs no AWS SDK at all.
  A module-level import would make boto3 a hard import-time requirement for
  exactly the configuration documented as needing none, and would make the
  package's own test suite unrunnable without it.
- **`client` / `transport_errors` are an injection seam for tests.** Every
  test in this PR passes a stub client, so no test needs the real SDK. An
  injected client with no declared error classes catches `Exception`; the
  real path declares `(ClientError, BotoCoreError)` so a programming error
  is not laundered into a counted upload failure.
- A module-level `describe_error(exc)` renders a `ClientError`'s
  `response['Error']['Code']`/`Message` (so `AccessDenied` vs `NoSuchBucket`
  vs `SlowDown` are distinguishable in the log line) and falls back to
  `type(exc).__name__: exc` for a `BotoCoreError` with no response dict.

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
  `read_timeout=15` and `coverage_renderer` `read_timeout=25`, so one PUT is
  bounded at `connect + read` = **20 s** and **30 s** respectively — exactly
  the old ceilings. A `botocore.exceptions.ReadTimeoutError` /
  `ConnectTimeoutError` (both `BotoCoreError` subclasses) is the typed
  equivalent of today's `subprocess.TimeoutExpired`.
- **Retry/backoff — `max_attempts: 1` in the nodes** (corrected in round 1
  of review; this plan originally specified `max_attempts: 4` and called it
  "strictly additive", which was wrong). botocore counts `max_attempts` as
  TOTAL attempts and retries connect and read timeouts alike, so 4 attempts
  multiply the per-PUT ceiling above by four — ~87 s in `state_renderer`
  against its 20 s cap, ~127 s in `coverage_renderer` against 30 s. Neither
  node can afford that: `_put` runs on `rclpy.spin`'s single-threaded
  executor, so a stalled PUT blocks `_on_fix` and leaves a permanent hole in
  the track, and `stop()`'s `join(timeout=45.0)` could no longer win, taking
  the early return that skips the final flush it exists for. So the SDK gets
  ONE attempt, and the retry stays where it already was and where both
  docstrings say it is: the next timer tick, or the tile staying dirty for
  the next render pass. `AccessDenied` / `NoSuchBucket` fall through to the
  failure counter exactly as before. (The cron script is the opposite case
  and keeps SDK retries — see section 3.)
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
  are documented thread-safe for concurrent calls; one shared client, many
  worker threads). New `--concurrency` CLI arg, **default 10** —
  `DEFAULT_CONCURRENCY`, settled rather than left open (Plan Review
  suggestion). 10 matches the AWS CLI's own default, i.e. the concurrency
  this pyramid has actually been published at; parity at the cutover is worth
  more than a guess at a faster number, and the flag raises it without a code
  change if a run proves too slow. It is documented as governing the S3
  upload only, distinct from `--rate`, which governs politeness toward CCOM.
  Each worker calls **`put_object`** (not `upload_file`): a single-part PUT is
  what makes the ETag comparison rule below hold for everything this script
  writes. Tiles are 256x256 PNGs, tens of kB, far under the 5 GB PUT limit.
- **"Already uploaded" comparison rule — CONTENT HASH, not size**
  (operator-mandated correction; supersedes this plan's original size-only
  rule): one `list_objects_v2` paginator pass over `tiles/<name>/` builds a
  `{key: etag}` dict (one cheap list call instead of a `head_object` per
  file). The ETag comes back quoted and is unquoted. A local file is
  **skipped** only if the remote key exists, its ETag is comparable, and it
  equals the local file's MD5; otherwise it is uploaded.
  - An S3 ETag equals the object's MD5 **only for a single-part, non-KMS
    upload**. `is_content_hash(etag)` requires 32 lowercase hex characters,
    which rejects a multipart `<hash>-<partcount>` ETag left by the old
    `aws s3 sync` (which switches to multipart above 8 MB). An
    uncomparable ETag **falls back to uploading**, never to skipping. The
    single-part guarantee and its fragility are stated in a comment, because
    the rule silently stops holding if a future change introduces multipart.
  - SSE-KMS ETags are also non-MD5 and are **not** distinguishable by shape.
    That direction fails safe: a mismatch means upload, so a KMS-encrypted
    bucket loses the skip optimisation but never serves a stale tile.
  - **Better on skipping, worse on metadata** (corrected in round 1; the
    plan originally claimed "strictly better"). Better on skipping: the
    fetch loop rewrites every local tile it renders, so local mtimes are
    always "now" and `aws s3 sync`'s size+mtime rule re-uploaded all ~5,839
    objects every run, where content hashing genuinely skips the unchanged
    ones. Worse on metadata: `list_objects_v2` returns no `CacheControl` or
    `ContentType`, so a change to `TILE_EXTRA_ARGS` reaches only tiles whose
    pixels also changed, leaving the prefix on a permanently mixed cache
    policy. `sync_dir(force=True)` — wired to `--force` — skips the
    comparison and re-PUTs everything, which is the remedy available today.
    A metadata-only path that avoids the ~hour of CCOM requests a re-render
    costs (a re-upload-from-workdir mode, or a `head_object` per key) is an
    explicit follow-up, named in the docstring.
- **Wall-clock bounds and re-entry** (added in round 1 of review): the CLI
  shell-outs were capped at the process level — 3600 s on the sync, 120 s on
  the manifest put, 60 s on the manifest read — and nothing replaced them.
  `s3_client(profile, max_seconds, attempts)` now derives `read_timeout`
  from the ceiling it is handed, so `attempts * (connect + read)` lands on
  the old cap and cannot drift from the number beside it; `main()` builds
  one client per cap. Retries stay ON here, unlike the nodes: a cron run
  gets no second chance for hours and one failed PUT withholds the whole
  manifest. `sync_dir` takes an aggregate `deadline_seconds` (3600) after
  which unstarted jobs fail rather than run. `acquire_run_lock()` takes a
  per-`--name` `flock` before the first request to CCOM: a run takes most of
  an hour at the default `--rate`, and an overrunning run that meets the
  next cron slot doubles the request rate against CCOM's server while both
  runs write the same `--workdir`.
- **`TILE_EXTRA_ARGS`** (`image/png` + `public,max-age=604800`) is a module
  constant rather than a literal inside `main()`, so a test can pin the
  values the upload actually uses instead of a copy of them — a mutation that
  drops the 7-day policy now fails a test.
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
- **`test/test_render_pass.py::test_the_upload_stamps_the_whole_object_shape_it_is_given`**
  (renamed from `..._the_max_age_it_is_given`) injects a recording stub client
  into `S3Uploader` and asserts the **full** `put_object` call shape —
  `Bucket`, `Key`, `Body`, `ContentType`, `CacheControl` — for both the
  manifest (`max_age=5`) and a tile (default `cache_control`), per the Plan
  Review suggestion, rather than carrying over the old test's cache-control-only
  assertion. A sibling test pins that a failed publish is counted and returns
  False rather than raising into the render worker.
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
- **`test/test_local_output.py`** gains
  `test_a_dry_run_never_reaches_the_s3_transport`, which sets `_uploader` to
  an object that raises on any access and asserts the dry-run publish still
  writes locally — binding the "no AWS access at all" guarantee.
- **`test/test_s3_upload.py`** also pins that importing the module does not
  import boto3, and that `state_renderer._put` carries
  `application/geo+json` plus the interval-derived max-age.
- Existing `test_tile_ingest.py::test_an_unusable_bucket_is_rejected_rather_than_retried`
  needs no change (tests `_is_usable_bucket` directly, independent of
  transport) — only its docstring's "30 s-capped subprocess" wording.
- **Mutation-checked**: 19 mutations were applied one at a time and the suite
  re-run with bytecode caching disabled; all 19 failed a test, including "skip
  a changed tile", "trust every ETag", "serial uploads", each of the four
  cache policies, and each failure counter. Recorded in the progress.md
  `## Implementation` entry.

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
| `marine_web_view/test/test_chart_tile_sync.py` | New — content-hash comparison, multipart-ETag fallback, concurrency, failure-gating, and the chart tile/manifest cache policies. |
| `marine_web_view/test/test_local_output.py` | New test: a dry run never reaches the S3 transport. |
| `marine_web_view/test/test_tile_ingest.py` | Docstring only: stale "30 s-capped subprocess" wording. |

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

## Open Questions — resolved during implementation

- **README structure for the `dry_run`/no-AWS-access sentence**: RESOLVED.
  The "Runtime prerequisite: the AWS CLI" section is replaced by a short
  "AWS credentials" section, because credentials are still a real operator
  prerequisite even with no CLI. It keeps the `dry_run` sentence (now
  stronger: no client is constructed at all) and states the two nodes'
  profile asymmetry explicitly, which the deleted section did not.
- **`--concurrency` default**: RESOLVED to **10**, matching the AWS CLI's
  default — parity at the cutover, justified in a comment beside
  `DEFAULT_CONCURRENCY`, and pinned by a test so a silent change to the load
  put on one S3 prefix fails.
- **Dependency tag**: `<depend>python3-boto3</depend>`, not `<exec_depend>`.
  boto3 is a runtime import only, so `exec_depend` is the narrower-correct
  tag, but `python3-numpy` and `python3-pil` — runtime imports in exactly the
  same way — are declared with `<depend>` in this same manifest; declaring
  the third differently would read as a distinction that does not exist, and
  `rosdep install` resolves both identically. Stated in the manifest comment.

## Estimated Scope

Single PR. Three call sites, one new shared helper module, one
self-contained script rewrite, three touched/new test files, one
`package.xml` line, one README section removal. No new ROS interfaces
(topics/services/actions/params) — pure transport-layer swap plus the
sync-comparison logic the issue itself asks to be tested.
