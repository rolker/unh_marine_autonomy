#!/usr/bin/env python3

# Copyright 2026 Roland Arsenault
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#
#    * Neither the name of the Roland Arsenault nor the names of its
#      contributors may be used to endorse or promote products derived from
#      this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

"""
Pre-render CCOM/JHC bathymetry into a static tile pyramid in S3.

WHY: the CCOM services are dynamic ArcGIS Image/Map servers with no tile cache,
so every pan and zoom by every viewer renders on gis.ccom.unh.edu. Pre-rendering
once turns that into plain static tiles behind CloudFront: zero runtime load on
CCOM, no third-party dependency at view time, viewer-count-indifferent.

WHEN TO RE-RUN: the compilation is versioned in the SERVICE NAME
(WGOM_LI_SNE_BTY_4m_20230922_...). A new compilation appears as a NEW service,
so this script enumerates the folder, picks the newest dated service, and
compares it against a manifest stored beside the tiles. No change and not stale
-> it does nothing and exits 0, which makes it safe to run from cron.

POLITENESS: this hits Paul Johnson's server a few thousand times. Default rate
is deliberately gentle and concurrency is 1. Talk to him before raising either.
(--concurrency governs the S3 UPLOAD only; the fetch loop stays serial and
rate-limited.)

DEPENDENCIES: stdlib plus boto3, and NOTHING from marine_web_view -- this runs
from cron without the ROS overlay sourced, so it must not need install/
setup.bash on sys.path. boto3 is imported lazily inside s3_client() so the
pure-computation entry points (and the package's tests) work without it.

  Estimate only (no requests to CCOM, no uploads):
      ./refresh_chart_tiles.py --dry-run

  Render + upload if the service changed or tiles are older than 30 days:
      ./refresh_chart_tiles.py

  Force a re-render:
      ./refresh_chart_tiles.py --force
"""

import argparse
import concurrent.futures
import fcntl
import hashlib
import json
import math
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

CCOM = 'https://gis.ccom.unh.edu/server/rest/services'
FOLDER = 'WGOM-LI-SNE'          # hyphens -- the folder the CCOM viewer uses
BUCKET = 'unh-ccom-p11-live'

# S3 upload fan-out. 10 matches the AWS CLI's own default, which is the
# concurrency this pyramid has actually been published at -- parity at the
# cutover is worth more than a guess at a faster number, and --concurrency
# raises it without a code change if a run ever proves too slow. This has
# nothing to do with --rate, which governs politeness toward CCOM's server.
DEFAULT_CONCURRENCY = 10

# WALL-CLOCK BOUNDS. The `aws s3` shell-outs this script replaced were capped
# at the process level -- subprocess.run(timeout=...) killed them -- and those
# caps are restored here, because there is nothing else standing between a
# stuck S3 endpoint and a cron run that overlaps the next one. Overlap is a
# real harm, not a tidiness point: two runs double the request rate against
# CCOM's server (see POLITENESS above) and interleave writes into the same
# --workdir.
#
# A single request is bounded by ATTEMPTS * (CONNECT_TIMEOUT + read_timeout);
# s3_client() derives read_timeout from the ceiling it is handed so the
# arithmetic cannot drift from the number beside it.
CONNECT_TIMEOUT = 10
REQUEST_ATTEMPTS = 3            # 1 try + 2 retries, for SlowDown on a big fan-out
MANIFEST_READ_SECONDS = 60      # was: subprocess timeout on `aws s3 cp ... -`
MANIFEST_WRITE_SECONDS = 120    # was: subprocess timeout on `aws s3 cp mf ...`
SYNC_DEADLINE_SECONDS = 3600    # was: subprocess timeout on `aws s3 sync`

# Tiles are immutable for the life of a compilation, so cache them hard: a
# viewer that already holds one should not ask again for a week. Kept as a
# module constant so test/test_chart_tile_sync.py can pin the values the
# upload actually uses, rather than a copy of them.
TILE_EXTRA_ARGS = {'ContentType': 'image/png',
                   'CacheControl': 'public,max-age=604800'}   # 7 days

# Piscataqua approaches + Isles of Shoals.
DEFAULT_BBOX = (-70.90, 42.92, -70.50, 43.15)     # W, S, E, N
UA = 'unh-ccom-p11-tilecache/0.1 (+ROS2 agent workspace; contact CCOM/JHC)'

# ---------------------------------------------------------------------------
# Colour ramp. MUST STAY IN SYNC WITH `RAMP` / `MAX_DEPTH` / `STEP` IN THE WEB
# PAGE'S index.html -- pre-rendered tiles and the live-render fallback have to
# look identical or the swap is visible. There is a THIRD copy of RAMP and
# MAX_DEPTH in marine_web_view/coverage_renderer.py, which the live coverage
# layer is coloured from. All three are compared by test/test_ramp_sync.py, so
# a drift fails the package tests rather than only this comment.
#
# Lifted from CCOM's own BTY_4m_HighRes_BlueGreen_DRA (the service Paul
# Johnson's compilation viewer uses) so this matches what people already see
# there. It is not published via REST, so it was extracted empirically: export
# the same area as raw float elevations AND as the styled PNG (layer 1 only,
# excluding the hillshade on layer 0), pair the pixels, and median the colour
# per normalised-depth bin over three areas (~170k pixels).
#
# Deliberately NOT DRA: CCOM's service auto-adjusts to the depths in view, so
# the same depth changes colour as you pan. Here the scale is PINNED.
# ---------------------------------------------------------------------------
RAMP = [
    (0, 38, 115),   # deepest
    (1, 40, 116),
    (6, 57, 125),
    (11, 68, 130),
    (16, 81, 137),
    (20, 92, 142),
    (21, 94, 143),
    (31, 116, 153),
    (41, 136, 162),
    (51, 155, 171),
    (62, 175, 180),
    (74, 189, 183),
    (88, 196, 178),
    (104, 204, 175),
    (107, 205, 175),
    (118, 211, 176),
    (127, 215, 176),
    (138, 219, 175),
    (150, 224, 177),
    (162, 229, 181),
    (174, 234, 186),
    (168, 232, 184),
    (182, 237, 190),
    (197, 243, 199),   # shallowest
]
MAX_DEPTH = 40      # metres; deeper clamps to the deepest colour
STEP = 0.5     # metre classes


def ramp_color(t):
    """Interpolate the ramp; t is 0 at the deepest and 1 at the surface."""
    i = max(0.0, min(1.0, t)) * (len(RAMP) - 1)
    lo = int(math.floor(i))
    hi = min(int(math.ceil(i)), len(RAMP) - 1)
    f = i - lo
    return [int(round(RAMP[lo][k] * (1 - f) + RAMP[hi][k] * f)) for k in range(3)]


def depth_classes():
    """Return discrete depth classes at STEP metres.

    ArcGIS Remap needs explicit ranges; at 0.5 m the banding is not visible.

    """
    out = []
    d = 0.0
    while d < MAX_DEPTH:
        out.append((-d, -(d + STEP), ramp_color(1 - (d + STEP / 2) / MAX_DEPTH)))
        d += STEP
    out.append((-float(MAX_DEPTH), -400.0, ramp_color(0)))
    return out


def chart_rule():
    """Explicit FIXED-range rendering rule.

    NEVER render these tiles from the pre-styled *_DRA services. "DRA" is
    Dynamic Range Adjustment: it stretches to each request's own extent, so
    every tile gets its own contrast curve and the pyramid comes out a visible
    patchwork -- measured seam delta 23.1 vs 1.2 with a fixed rule. Baked into
    a cache that is permanent.

    Remap bins elevations into class indices; Colormap paints them.
    AllowUnmatched=False leaves land as NoData -> transparent, so the imagery
    basemap shows through above the waterline.
    """
    cls = depth_classes()
    ranges, outs = [], []
    for i, (frm, to, _) in enumerate(cls, start=1):
        ranges += [to, frm]
        outs.append(i)
    return json.dumps({
        'rasterFunction': 'Colormap',
        'rasterFunctionArguments': {
            'Colormap': [[i] + c for i, (_, _, c) in enumerate(cls, start=1)],
            'Raster': {'rasterFunction': 'Remap',
                       'rasterFunctionArguments': {
                           'InputRanges': ranges, 'OutputValues': outs,
                           'AllowUnmatched': False, 'Raster': '$$'}}}})


def deg2tile(lat, lon, z):
    n = 2 ** z
    x = int((lon + 180.0) / 360.0 * n)
    y = int((1.0 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2.0 * n)
    return x, y


def tile_bbox_3857(x, y, z):
    S = 20037508.342789244
    res = (2 * S) / (256 * 2 ** z)
    return (-S + x * 256 * res, S - (y + 1) * 256 * res,
            -S + (x + 1) * 256 * res, S - y * 256 * res)


def tile_list(bbox, zmin, zmax):
    w, s, e, n = bbox
    out = []
    for z in range(zmin, zmax + 1):
        x0, y0 = deg2tile(n, w, z)          # north-west
        x1, y1 = deg2tile(s, e, z)          # south-east
        for x in range(min(x0, x1), max(x0, x1) + 1):
            for y in range(min(y0, y1), max(y0, y1) + 1):
                out.append((z, x, y))
    return out


def http_get(url, timeout=60):
    req = urllib.request.Request(url, headers={'User-Agent': UA})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def candidate_services(layer_kind):
    """All services matching a kind, newest-dated first.

    Service names carry the compilation date, so date order answers 'has the
    compilation been updated?'. It does NOT answer 'does this service actually
    have data' -- see pick_service.
    """
    body = http_get(f'{CCOM}/{FOLDER}?f=pjson').decode()
    names = [s['name'].split('/')[-1] for s in json.loads(body).get('services', [])]
    cands = [n for n in names if layer_kind in n and n.endswith('_IS')]
    if not cands:
        raise SystemExit(f'no ImageServer matching {layer_kind!r} in {FOLDER}: {names}')

    def key(n):
        digits = [q for q in n.replace('-', '_').split('_')
                  if q.isdigit() and len(q) == 8]
        return (max(digits) if digits else '', n)
    return sorted(cands, key=key, reverse=True)


def pick_service(layer_kind, bbox, timeout, blank_bytes):
    """Newest service that actually RETURNS DATA over the target area.

    Learned the hard way: WGOM_LI_SNE_BTY_4m_20231005_IS is the newest-dated
    4 m service and declares the same extent as its siblings, but renders
    completely empty. Selecting on the name alone would produce a full pyramid
    of blank tiles -- an hour of requests against CCOM's server for nothing.
    So probe before committing.

    A probe tile counts as data by the same rule the render loop uses to keep
    one (``blank_bytes``), so the two decisions cannot disagree.
    """
    w, s_, e, n = bbox
    clat, clon = (s_ + n) / 2.0, (w + e) / 2.0
    probes = [(13,) + deg2tile(clat, clon, 13), (14,) + deg2tile(clat, clon, 14)]
    for svc in candidate_services(layer_kind):
        ep = endpoint_for(svc)
        try:
            hits = 0
            for z, x, y in probes:
                if len(fetch_tile(ep, z, x, y, timeout)) > blank_bytes:
                    hits += 1
            if hits:
                print(f'  {svc}: {hits}/{len(probes)} probe tiles have data -- using it')
                return svc, ep
            print(f'  {svc}: EMPTY at all probe tiles -- skipping')
        except Exception as exc:
            print(f'  {svc}: probe failed ({exc}) -- skipping')
    raise SystemExit(f'no service matching {layer_kind!r} returned data over {bbox}')


def endpoint_for(service):
    """Return the export endpoint for a service.

    Always the raw ImageServer: we supply our own rendering rule, so the
    pre-styled (and DRA-afflicted) MapServers are never appropriate here.
    """
    if not service.endswith('_IS'):
        raise SystemExit(f'{service!r} is not an ImageServer -- refusing. Pre-styled '
                         f'*_DRA MapServers apply per-request range adjustment and '
                         f'would bake tile seams into the cache permanently.')
    return f'{CCOM}/{FOLDER}/{service}/ImageServer/exportImage'


def fetch_tile(endpoint, z, x, y, timeout):
    minx, miny, maxx, maxy = tile_bbox_3857(x, y, z)
    params = {
        'bbox': f'{minx},{miny},{maxx},{maxy}',
        'bboxSR': '3857', 'imageSR': '3857', 'size': '256,256',
        'format': 'png32', 'transparent': 'true', 'f': 'image',
        # REQUIRED. With bilinear the server blends adjacent band colours while
        # resampling -- 499 distinct colours instead of 10, which destroys the
        # banding and triples the bytes.
        'interpolation': 'RSP_NearestNeighbor',
        'renderingRule': chart_rule(),
    }
    return http_get(endpoint + '?' + urllib.parse.urlencode(params), timeout=timeout)


def s3_client(profile, max_seconds=MANIFEST_WRITE_SECONDS,
              attempts=REQUEST_ATTEMPTS):
    """Build an S3 client for `profile`, bounded at `max_seconds` per request.

    boto3 is imported HERE and not at module scope so that everything above --
    the ramp, the rendering rule, the tile maths, and the sync helpers below
    when handed a client -- stays importable on a host with no AWS SDK. That is
    what lets --dry-run and the package's tests run without boto3, and it keeps
    this script's stdlib-only-plus-boto3 property honest.

    `profile` is passed through as given: cron runs this with an explicit
    admin profile, and None falls through to the default credential chain
    (an EC2 instance role, or a plain ~/.aws/credentials).
    """
    import boto3
    from botocore.config import Config

    # Retries: botocore's own exponential backoff for throttling and 5xx.
    # A few thousand PUTs against one prefix is exactly where SlowDown shows
    # up. Unlike the renderer nodes -- which retry on their next tick and so
    # ask botocore for a single attempt -- a cron run gets no second chance
    # for hours, and one failed PUT withholds the whole manifest. So retries
    # stay on, and the budget is sized instead: botocore counts max_attempts
    # as TOTAL attempts and retries connect and read timeouts alike, so the
    # worst case for one request is attempts * (connect + read). read_timeout
    # is solved from the ceiling rather than written beside it, so the two
    # cannot drift apart.
    read_timeout = float(max_seconds) / attempts - CONNECT_TIMEOUT
    if read_timeout <= 0:
        raise ValueError(
            '{} s over {} attempts leaves no read budget past the {} s '
            'connect timeout'.format(max_seconds, attempts, CONNECT_TIMEOUT))
    return boto3.Session(profile_name=profile).client(
        's3', config=Config(
            retries={'mode': 'standard', 'max_attempts': attempts},
            connect_timeout=CONNECT_TIMEOUT, read_timeout=read_timeout))


def acquire_run_lock(workdir, name):
    """Take the exclusive run lock for `name`, or return None if held.

    A lockfile IS warranted here, not just belt and braces: this script is
    driven by cron, one run takes the better part of an hour at the default
    --rate, and an overrunning run that meets the next one doubles the
    request rate against CCOM's server -- the one thing this script's
    docstring says to ask before doing -- while both runs write the same
    tiles into the same --workdir.

    Per --name, because that is the unit that shares a workdir subtree and an
    S3 prefix; two different layers are a deliberate operator choice and can
    legitimately run together.

    The returned handle must stay referenced for the life of the run: closing
    it (or letting it be garbage collected) releases the lock. flock is
    released by the kernel when the process exits, so a killed run does not
    strand it.
    """
    os.makedirs(workdir, exist_ok=True)
    handle = open(os.path.join(workdir, '.{}.lock'.format(name)), 'w')
    try:
        fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        handle.close()
        return None
    return handle


def load_manifest(client):
    """Return the tile manifest, or {} if it cannot be read.

    Blanket except, deliberately: a missing manifest is the first-run case and
    an unreadable one is handled identically -- re-render. Nothing here should
    stop the script before it has even chosen a service.
    """
    try:
        body = client.get_object(Bucket=BUCKET, Key='tiles/manifest.json')
        return json.loads(body['Body'].read())
    except Exception:
        return {}


def save_manifest(client, path):
    """Upload the rewritten manifest with no-cache."""
    with open(path, 'rb') as handle:
        client.put_object(Bucket=BUCKET, Key='tiles/manifest.json',
                          Body=handle.read(),
                          ContentType='application/json',
                          CacheControl='no-cache')


def file_md5(path, chunk=1 << 20):
    """Hex MD5 of a file's bytes.

    A content fingerprint compared against S3's ETag, not a security digest.
    """
    digest = hashlib.md5()
    with open(path, 'rb') as handle:
        for block in iter(lambda: handle.read(chunk), b''):
            digest.update(block)
    return digest.hexdigest()


def remote_etags(client, bucket, prefix):
    """Map every key already under `prefix` to its unquoted ETag.

    One paginated list instead of a head_object per file: ~5,800 tiles would
    otherwise be ~5,800 extra round trips before a single byte is uploaded.
    """
    found = {}
    paginator = client.get_paginator('list_objects_v2')
    for page in paginator.paginate(Bucket=bucket, Prefix=prefix):
        for obj in page.get('Contents') or []:
            # S3 returns the ETag quoted -- '"d41d8..."'.
            found[obj['Key']] = str(obj.get('ETag', '')).strip('"')
    return found


def is_content_hash(etag):
    """Return True if `etag` can be compared against a local file's MD5.

    An S3 ETag equals the object's MD5 only for a SINGLE-PART, non-KMS upload.
    A multipart upload's ETag is '<md5-of-part-md5s>-<partcount>', which this
    rejects on the trailing '-<n>'. THIS SCRIPT ONLY EVER USES put_object, so
    everything it writes is single-part by construction -- but the bucket may
    still hold objects put there by `aws s3 sync` (which switches to multipart
    above 8 MB) or by hand, so the shape is checked rather than assumed. If a
    future change introduces multipart uploads here, this rule stops holding
    and the comparison must change with it.

    Objects encrypted with SSE-KMS also carry a non-MD5 ETag that is NOT
    distinguishable by shape. That direction fails safe: a mismatch means
    upload, so a KMS-encrypted bucket loses the skip optimisation but never
    serves a stale tile.
    """
    return len(etag) == 32 and all(c in '0123456789abcdef' for c in etag)


def local_files(local_dir):
    """Sorted (key suffix, absolute path) for every file under `local_dir`."""
    found = []
    for root, _dirs, names in os.walk(local_dir):
        for name in names:
            full = os.path.join(root, name)
            rel = os.path.relpath(full, local_dir).replace(os.sep, '/')
            found.append((rel, full))
    return sorted(found)


def sync_dir(client, local_dir, bucket, prefix, extra_args,
             concurrency=DEFAULT_CONCURRENCY, log=print, force=False,
             deadline_seconds=SYNC_DEADLINE_SECONDS):
    """Upload `local_dir` to `bucket`/`prefix`; return (sent, skipped, failed).

    Upload-only, like the `aws s3 sync` it replaces: no --delete flag was ever
    passed, so nothing here deletes.

    WHAT COUNTS AS ALREADY UPLOADED -- by CONTENT HASH, not size. The tile
    prefix is FIXED (--name, default 'bathy4m'), not versioned per
    compilation, and main() deliberately re-renders into that same prefix when
    the colour ramp changes (the rule_hash check). A recoloured PNG of
    identical byte size is entirely plausible, and a size-only comparison
    would silently skip it -- leaving stale colours served until the next rule
    change or a --force. So the local MD5 is compared against the object's
    ETag, and anything that differs, is absent, or has an ETag that cannot be
    compared (see is_content_hash) is uploaded.

    BETTER ON SKIPPING, WORSE ON METADATA than the `aws s3 sync` it replaces
    -- not strictly better. Better on skipping: the fetch loop rewrites every
    local tile it renders, so local mtimes are always 'now' and sync's
    size+mtime rule re-uploaded all ~5,839 objects every run, where content
    hashing genuinely skips the unchanged ones. Worse on metadata:
    `list_objects_v2` returns no CacheControl or ContentType, so a comparison
    on bytes alone cannot see that an object's cache policy is stale. Change
    TILE_EXTRA_ARGS and the new policy reaches only tiles whose pixels also
    changed, leaving the prefix on a permanently mixed policy.

    `force` is the remedy available today: it skips the comparison entirely
    and re-PUTs everything, which is the only way a TILE_EXTRA_ARGS change
    reaches unchanged tiles. `--force` passes it, so a re-render after a
    policy change does propagate. Propagating a metadata-only change WITHOUT
    a full re-render (~an hour of requests against CCOM for pixels that did
    not move) would need a re-upload-from-workdir mode, or a head_object per
    key; that is a follow-up, not something this function silently does.

    `deadline_seconds` is an aggregate wall-clock bound on the whole fan-out,
    restoring what `subprocess.run(timeout=3600)` used to enforce on the
    shell-out. Jobs that have not started by the deadline fail rather than
    run, which withholds the manifest and leaves the next run to finish the
    job -- far better than a cron run that overlaps its successor.
    """
    remote = {} if force else remote_etags(client, bucket, prefix)
    pending = []
    skipped = 0
    for rel, path in local_files(local_dir):
        key = prefix + rel
        etag = remote.get(key)
        if etag and is_content_hash(etag) and etag == file_md5(path):
            skipped += 1
            continue
        pending.append((key, path))

    sent = failed = 0
    if not pending:
        return sent, skipped, failed

    deadline = time.monotonic() + float(deadline_seconds)

    def _upload(job):
        key, path = job
        if time.monotonic() > deadline:
            # Counted as a failure, deliberately: main() withholds the
            # manifest on any failure, so an abandoned run does not get
            # recorded as current.
            raise TimeoutError(
                'sync deadline of {:g} s exceeded before {}'.format(
                    float(deadline_seconds), key))
        with open(path, 'rb') as handle:
            body = handle.read()
        # put_object, NOT upload_file: a single-part PUT is what makes the
        # ETag == MD5 rule above hold for everything this script writes.
        # Tiles are 256x256 PNGs (tens of kB), far under the 5 GB PUT limit.
        client.put_object(Bucket=bucket, Key=key, Body=body, **extra_args)

    # Threads, because boto3 clients are documented safe for concurrent calls
    # and ~5,839 serial PUTs at S3 round-trip latency is a quarter hour.
    with concurrent.futures.ThreadPoolExecutor(
            max_workers=max(1, int(concurrency))) as pool:
        futures = {pool.submit(_upload, job): job for job in pending}
        done = 0
        for future in concurrent.futures.as_completed(futures):
            done += 1
            try:
                future.result()
                sent += 1
            except Exception as exc:
                failed += 1
                if failed <= 10:
                    log('  ! {}: {}'.format(futures[future][0], exc))
            if done % 500 == 0 or done == len(pending):
                log('  {}/{} uploaded={} skipped(unchanged)={} failed={}'
                    .format(done, len(pending), sent, skipped, failed))
    return sent, skipped, failed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--layer', default='BTY_4m',
                    help='substring identifying the service')
    ap.add_argument('--name', default='bathy4m', help='tile prefix under tiles/')
    ap.add_argument('--bbox', type=float, nargs=4, default=list(DEFAULT_BBOX),
                    metavar=('W', 'S', 'E', 'N'))
    ap.add_argument('--zmin', type=int, default=10)
    ap.add_argument('--zmax', type=int, default=16)
    ap.add_argument('--rate', type=float, default=2.0,
                    help='max requests/sec to CCOM (default 2 -- be polite)')
    ap.add_argument('--blank-bytes', type=int, default=1000,
                    help='PNGs at or below this size are treated as empty and '
                         'skipped (a fully transparent 256x256 png32 is tiny)')
    ap.add_argument('--max-age-days', type=int, default=30)
    ap.add_argument('--profile', default='ccom-jhc',
                    help='AWS profile. NOTE: p11-renderer is scoped to live/* '
                         'only and cannot write tiles/ -- use an admin profile '
                         'or extend the policy.')
    ap.add_argument('--concurrency', type=int, default=DEFAULT_CONCURRENCY,
                    help='parallel S3 uploads (default %(default)s, matching '
                         'the AWS CLI; does NOT affect the request rate to '
                         'CCOM, see --rate)')
    ap.add_argument('--workdir', default='/tmp/p11-tiles')
    ap.add_argument('--timeout', type=float, default=60)
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--force', action='store_true',
                    help='re-render regardless of age, and re-upload every '
                         'tile rather than skipping unchanged bytes (the way '
                         'to propagate a TILE_EXTRA_ARGS cache-policy change)')
    a = ap.parse_args()

    # --name becomes both a local directory under --workdir and an S3 key prefix
    # under tiles/. Constrain it to a safe charset so a '../', an absolute path,
    # or a slash cannot escape the workdir or rewrite an unrelated tiles/ prefix.
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', a.name):
        ap.error('--name must start alphanumeric and contain only '
                 '[A-Za-z0-9._-] (it is used as a path and S3 key prefix)')

    tiles = tile_list(tuple(a.bbox), a.zmin, a.zmax)
    per_z = {}
    for z, _, _ in tiles:
        per_z[z] = per_z.get(z, 0) + 1
    est_s = len(tiles) / max(a.rate, 0.01)
    print(f'bbox {a.bbox}  zoom {a.zmin}-{a.zmax}')
    for z in sorted(per_z):
        print(f'  z{z:<3} {per_z[z]:>7} tiles')
    print(f'  TOTAL {len(tiles)} tiles  ~{est_s/60:.0f} min at {a.rate}/s'
          f'  ~{len(tiles)*30/1024:.0f} MB at 30 KB/tile')

    if a.dry_run:
        print('\ndry run: no requests to CCOM, no uploads.')
        return 0

    # Before a single request to CCOM: an overrunning previous run is the
    # case this exists for, and probing the folder first would already have
    # doubled the request rate.
    lock = acquire_run_lock(a.workdir, a.name)
    if lock is None:
        print('another run for {!r} holds {}/.{}.lock -- exiting so the two '
              'do not both crawl CCOM.'.format(a.name, a.workdir, a.name),
              file=sys.stderr)
        return 0

    print('\nselecting a service that actually has data over the target area:')
    service, endpoint = pick_service(a.layer, tuple(a.bbox), a.timeout,
                                     a.blank_bytes)
    print(f'service: {service}\nendpoint: {endpoint}')

    # Two clients, because the three operations had three different
    # process-level caps under the CLI and each is restored on its own.
    client = s3_client(a.profile, MANIFEST_WRITE_SECONDS)
    man = load_manifest(s3_client(a.profile, MANIFEST_READ_SECONDS,
                                  attempts=2))
    prev = man.get(a.name, {})
    age_d = (time.time() - prev.get('rendered_at', 0)) / 86400
    rule_hash = hashlib.sha256(chart_rule().encode()).hexdigest()[:12]
    if prev.get('rule_hash') not in (None, rule_hash):
        print(f"banding changed ({prev.get('rule_hash')} -> {rule_hash}) "
              f'-- re-rendering regardless of age.')
        prev = {}
    if not a.force and prev.get('service') == service and age_d < a.max_age_days:
        print(f'unchanged ({service}) and {age_d:.1f}d old '
              f'(< {a.max_age_days}d) -- nothing to do.')
        return 0
    if prev:
        print(f"previous: {prev.get('service')} rendered {age_d:.1f}d ago")

    outdir = os.path.join(a.workdir, a.name)
    os.makedirs(outdir, exist_ok=True)
    interval = 1.0 / max(a.rate, 0.01)
    written = skipped = failed = 0
    t0 = time.time()

    for i, (z, x, y) in enumerate(tiles, 1):
        start = time.time()
        try:
            data = fetch_tile(endpoint, z, x, y, a.timeout)
            if len(data) <= a.blank_bytes:
                skipped += 1                      # no data here; don't store it
            else:
                d = os.path.join(outdir, str(z), str(x))
                os.makedirs(d, exist_ok=True)
                with open(os.path.join(d, f'{y}.png'), 'wb') as f:
                    f.write(data)
                written += 1
        except Exception as e:
            failed += 1
            if failed <= 10:
                print(f'  ! z{z}/{x}/{y}: {e}', file=sys.stderr)
        if i % 200 == 0 or i == len(tiles):
            el = time.time() - t0
            print(f'  {i}/{len(tiles)}  written={written} blank={skipped} '
                  f'failed={failed}  {el/60:.1f} min')
        time.sleep(max(0.0, interval - (time.time() - start)))

    if written == 0:
        print('nothing rendered -- NOT touching S3.', file=sys.stderr)
        return 1
    if failed > written * 0.05:
        print(f'too many failures ({failed} vs {written} written) -- '
              f'NOT publishing a partial pyramid.', file=sys.stderr)
        return 1

    print('\nuploading...')
    sent, unchanged, up_failed = sync_dir(
        client, outdir, BUCKET, f'tiles/{a.name}/', TILE_EXTRA_ARGS,
        concurrency=a.concurrency, force=a.force)
    print(f'uploaded {sent}, unchanged {unchanged}, failed {up_failed}')
    if up_failed:
        # All-or-nothing, as `aws s3 sync` was: the manifest is what tells the
        # next run 'this pyramid is current', so publishing it over a partial
        # upload would strand the missing tiles until the next rule change.
        print('upload failed', file=sys.stderr)
        return 1

    man[a.name] = {'service': service, 'endpoint': endpoint,
                   'rule_hash': rule_hash, 'max_depth': MAX_DEPTH,
                   'step': STEP, 'ramp_points': len(RAMP),
                   'bbox': a.bbox, 'zmin': a.zmin, 'zmax': a.zmax,
                   'tiles': written, 'blank': skipped,
                   'rendered_at': time.time(),
                   'rendered_iso': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}
    mf = os.path.join(a.workdir, 'manifest.json')
    with open(mf, 'w') as f:
        json.dump(man, f, indent=1)
    save_manifest(client, mf)
    print(f'done: {written} tiles ({skipped} blank skipped, {failed} failed)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
