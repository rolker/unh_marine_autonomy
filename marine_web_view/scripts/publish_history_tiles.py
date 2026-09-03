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
Pre-render previously surveyed days into a static tile pyramid in S3.

WHY: the live coverage layer shows what the boat's coverage source holds RIGHT
NOW. Reset that source -- or start a new day against a fresh store -- and every
earlier day leaves the map, even though it is the same seabed and the operator
still needs to see which ground has already been run. The surveyed days do
exist ashore, as QPS exports on disk. This turns them into their own static
pyramid under the live layer, so history is carried by the thing that has it
(a finished grid) rather than by asking a live renderer to hold a survey's
worth of state it deliberately does not keep (see "Memory-only, by design" in
the package README).

WHY MUTED: the export carries QPS's own rainbow ramp with hillshade baked in,
which is NOT the page's pinned 0-40 m blue-green scale -- the same depth is a
different colour in each. That cannot be fixed by recolouring: 211,575 distinct
colours over 1.46 M data pixels in the Day 2 export says shading is baked into
the RGB, so the mapping from colour back to depth is not invertible. So the
divergence is turned into the signal instead. History is desaturated toward a
warm neutral and lightened; live coverage stays cool and vivid. The hue axis
alone then says old-vs-new, before the operator reads the footprint outline or
the readout. What survives the mute is relief and footprint -- which is what a
history layer is actually for -- and what is deliberately destroyed is any
invitation to compare its colours with the live layer's as if they were one
scale.

The mute is AFFINE in RGB (a luminance blend, a tint multiply, a lighten), so
it commutes with the averaging gdal2tiles does to build overview levels. Muting
the source once and tiling the result gives the same pixels as tiling first and
muting every tile; test_history_tiles.py pins that.

WHEN TO RE-RUN: whenever a new day's export lands. The run is idempotent --
same sources in, same tiles out -- and it un-publishes: tiles the previous run
left behind that this one does not produce are deleted, so a shrinking or
re-cut mosaic does not strand PNGs the page will still fetch.

DEPENDENCIES: stdlib, numpy, GDAL's Python bindings, and boto3. Nothing from
marine_web_view -- like refresh_chart_tiles.py this is meant to run from a
plain shell (or cron) with no ROS overlay sourced. GDAL and boto3 are imported
lazily so the pure-computation entry points, and this package's tests, work
without either.

  Local preview -- writes into web/live/history/, so opening web/index.html
  shows the layer exactly as the bucket will serve it:
      ./publish_history_tiles.py --dry-run \
          ~/'Shoals Day1_1m_CUBE_ITRF2020.tif' \
                                           ~/Shoals2026_Day2_1m_CUBE_ITRF2020.tif

  Publish, oldest source FIRST (later sources win where they overlap):
      ./publish_history_tiles.py --labels 2026-08-25,2026-08-26 \
          ~/'Shoals Day1_1m_CUBE_ITRF2020.tif' \
          ~/Shoals2026_Day2_1m_CUBE_ITRF2020.tif
"""

import argparse
import fcntl
import json
import os
import shutil
import stat
import sys
import tempfile
import time

import numpy as np

BUCKET = 'unh-ccom-p11-live'

# Under live/, and that is not cosmetic: the p11-renderer credential the boat
# publishes with is scoped to s3:PutObject on live/* only. A sibling top-level
# prefix would need an IAM change and an admin profile -- which is exactly why
# refresh_chart_tiles.py has to run under ccom-jhc to write tiles/.
PREFIX = 'live/history/'
META_KEY = PREFIX + 'meta.json'

# The key index is a SEPARATE object from the manifest on purpose.
# Un-publishing needs the full list of keys the last run wrote --
# hundreds of strings -- and
# meta.json is fetched by every viewer on a poll. Carrying the index in it
# would put ~30 KB of bookkeeping on the page's budget to serve four numbers.
INDEX_KEY = PREFIX + 'index.json'

# A day that has been surveyed does not change. Cache the tiles hard; the
# manifest is what a viewer re-reads to notice that a NEW day was added, so it
# gets a short one of its own.
TILE_EXTRA_ARGS = {'ContentType': 'image/png',
                   'CacheControl': 'public,max-age=86400'}        # 1 day
META_EXTRA_ARGS = {'ContentType': 'application/json',
                   'CacheControl': 'public,max-age=300'}          # 5 minutes

# ---------------------------------------------------------------------------
# The mute. Defaults chosen against a composite of the real Day 2 export under
# a mock live swath (the page's own ramp at its real 0.95 opacity) over Esri
# imagery water, which is the only comparison that answers the question this
# layer has to answer: does fresh coverage still read as fresh?
#
# `MUTE_OPACITY` is NOT baked into the tiles. It is reported in the manifest
# and applied by the page as the layer's opacity, so the balance between
# the two
# layers can be re-tuned with a page edit instead of a re-render of the
# pyramid.
MUTE_SATURATION = 0.18      # 0 = greyscale, 1 = the export's own colours
MUTE_LIGHTEN = 0.28         # blend toward white; "old" in the page's idiom
MUTE_TINT = (1.00, 0.955, 0.86)   # warm, so the hue axis opposes the live ramp
MUTE_OPACITY = 0.80

# Rec. 601 luma, the same weights the page's own hillshade reasoning assumes.
LUMA = (0.299, 0.587, 0.114)

# Zoom bounds. The floor is not a detail: the page's map has minZoom 8, and a
# tile layer whose minNativeZoom sits above the current map zoom is laid out by
# Leaflet in tiles OF THAT NATIVE ZOOM -- 4^n of them (web/index.html documents
# the browser freeze this causes). The page hides the layer below its own
# min_zoom, and rendering down to 13 keeps that hide-point close enough to the
# map's floor that the layer is not simply absent for anyone zoomed out.
ZOOM_FLOOR = 13
# The ceiling bounds cost, not quality: each extra level is 4x the tiles for
# ground that a 1 m grid cannot resolve further anyway.
ZOOM_CEILING = 18

# Web Mercator, the numbers gdal2tiles works in.
MERCATOR_CIRCUMFERENCE = 40075016.6855785
TILE_SIZE = 256

# Bound the fan-out the same way refresh_chart_tiles.py does, and for the same
# reason: this is our own bucket, but an unbounded pool against a slow endpoint
# is just a queue that fails all at once.
DEFAULT_CONCURRENCY = 10
UPLOAD_DEADLINE_SECONDS = 1800

# A held lock older than this is a wedged run, not a slow one: a full pyramid
# over a few km2 is minutes.
LOCK_STALE_SECONDS = 3600


# ---------------------------------------------------------------------------
# Pure computation. No GDAL, no boto3, no filesystem -- this is the part the
# tests can exercise directly.
# ---------------------------------------------------------------------------

def mute_rgb(rgb, saturation=MUTE_SATURATION, lighten=MUTE_LIGHTEN,
             tint=MUTE_TINT):
    """Desaturate, tint and lighten an RGB array of floats in 0..255.

    Affine in `rgb` -- deliberately. gdal2tiles builds every overview level by
    averaging the level below it, and an affine function commutes with an
    average, so muting the source and tiling it produces the same pyramid as
    tiling the source and muting each tile. That equivalence is what lets this
    run once over the source instead of over every tile at every level.

    Nothing is clipped here. The caller writes into a uint8 band and clips
    there; clipping twice would be the same operation and it is easier to
    reason about the arithmetic when it stays linear all the way through.
    """
    weights = np.array(LUMA, dtype=np.float32)
    luma = np.tensordot(rgb, weights, axes=([-1], [0]))[..., None]
    out = luma + (rgb - luma) * float(saturation)
    out = out * np.array(tint, dtype=np.float32)
    return out * (1.0 - float(lighten)) + 255.0 * float(lighten)


def zoom_range(resolution, floor=ZOOM_FLOOR, ceiling=ZOOM_CEILING):
    """Return (min_zoom, max_zoom) for a mosaic at `resolution` m/px.

    `resolution` is in Web Mercator metres, which are the source's ground
    metres divided by cos(latitude) -- at 43 N a 1 m grid is about 1.37 m/px
    here. Rounding to the NEAREST level rather than up is the point: rounding
    up buys a level of empty magnification and 4x the tiles.
    """
    if not resolution > 0:
        raise ValueError('resolution must be positive, got {!r}'.format(
            resolution))
    native = np.log2(MERCATOR_CIRCUMFERENCE / (TILE_SIZE * resolution))
    top = int(min(ceiling, max(floor, round(native))))
    return floor, top


def plan_removals(previous_keys, current_keys):
    """Return the keys a previous run published that this one did not.

    A tile PNG stands in the bucket until something overwrites or deletes it,
    and the page paints a miss transparent -- so ground that has dropped out of
    the mosaic (a source withdrawn, a re-cut that no longer reaches a tile)
    keeps being served from the old render with nothing to reveal that it is
    stale. The live coverage renderer un-publishes for exactly this reason;
    this is the same discipline, done across runs because a static pyramid has
    no next pass to heal in.

    Sorted so a run's deletions are reproducible and reviewable.
    """
    return sorted(set(previous_keys) - set(current_keys))


def build_manifest(min_zoom, max_zoom, tile_count, sources, bounds, stamp):
    """Assemble the page-facing manifest.

    Small by construction -- every viewer polls it. The key index that
    un-publishing needs lives in its own object (see INDEX_KEY).

    `bounds` is [[south, west], [north, east]] in degrees: Leaflet's own
    LatLngBounds order, so the page can hand it straight to fitBounds without
    a coordinate swap. That is the reverse of GeoJSON's [lon, lat], and the
    two orders being reversed from each other is a standing trap in this page
    (state_renderer's output is GeoJSON and IS [lon, lat]).
    """
    return {
        'min_zoom': int(min_zoom),
        'max_zoom': int(max_zoom),
        'tiles': int(tile_count),
        'sources': list(sources),
        'labels': [source['label'] for source in sources],
        'bounds': bounds,
        'opacity': MUTE_OPACITY,
        'mute': {'saturation': MUTE_SATURATION, 'lighten': MUTE_LIGHTEN,
                 'tint': list(MUTE_TINT)},
        'prefix': PREFIX.rstrip('/'),
        'stamp': float(stamp),
        'generated': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime(stamp)),
    }


def resolve_labels(paths, labels):
    """Pair each source path with the label it will be published under.

    Labels are NOT derived from the file's mtime, and that is a correctness
    decision rather than laziness: an export written the morning after a survey
    carries the wrong date, so mtime would silently publish "2026-08-26" over
    ground that was run on the 25th. Absent an explicit --labels, the basename
    is used -- which is merely unhelpful, where an invented date is wrong.
    """
    if not labels:
        return [os.path.splitext(os.path.basename(p))[0] for p in paths]
    if len(labels) != len(paths):
        raise ValueError(
            '--labels has {} entries for {} sources; they are matched in '
            'order, so give one per source or none at all'.format(
                len(labels), len(paths)))
    return list(labels)


# ---------------------------------------------------------------------------
# Raster. GDAL is imported inside these, not at module scope, so the pure half
# above stays importable (and testable) on a host without the bindings.
# ---------------------------------------------------------------------------

def _gdal():
    """Return the GDAL module with exceptions on.

    UseExceptions() is not optional: without it GDAL reports failure by
    returning None and setting a global error string, so a warp that produced
    nothing carries on to gdal2tiles as an empty mosaic and publishes a
    pyramid of transparent tiles over the ground it was meant to show.
    """
    from osgeo import gdal
    gdal.UseExceptions()
    return gdal


def open_source(path):
    """Open an RGBA export and refuse anything this script cannot honour.

    Checked here rather than left to fail downstream, because each of these
    fails QUIETLY further along:

    - no alpha band: the mosaic has no notion of "not surveyed", so the
      export's own background floods every tile it touches and the history
      layer becomes an opaque rectangle over the chart.
    - not 8-bit: mute_rgb's arithmetic is in 0..255 and the tint/lighten
      constants are meaningless against a float depth band. A float export is
      a BETTER input than this one -- it could be coloured on the page's own
      ramp -- but it is a different pipeline, not this one with a wider dtype.
    - no projection: gdal.Warp would have nothing to warp from and lands the
      mosaic at the origin, which puts a survey off West Africa.
    """
    gdal = _gdal()
    dataset = gdal.Open(path)
    if dataset.RasterCount != 4:
        raise ValueError(
            '{}: expected an RGBA export, got {} band(s). A single-band depth '
            'grid needs the page ramp, not this script.'.format(
                path, dataset.RasterCount))
    types = {gdal.GetDataTypeName(dataset.GetRasterBand(i).DataType)
             for i in range(1, 5)}
    if types != {'Byte'}:
        raise ValueError('{}: expected 8-bit bands, got {}'.format(
            path, ', '.join(sorted(types))))
    interp = gdal.GetColorInterpretationName(
        dataset.GetRasterBand(4).GetColorInterpretation())
    if interp != 'Alpha':
        raise ValueError(
            '{}: band 4 is {}, not Alpha -- without a validity mask the '
            'export would be mosaicked as an opaque rectangle'.format(
                path, interp))
    if not dataset.GetProjection():
        raise ValueError('{}: no projection; refusing to guess one'.format(
            path))
    return dataset


def mute_source(path, destination, saturation=MUTE_SATURATION,
                lighten=MUTE_LIGHTEN, tint=MUTE_TINT, block_rows=512):
    """Write a muted copy of `path` to `destination`, georeferencing intact.

    Row-blocked because these exports are ~8700 x 10200: read whole, the
    float32 working copy alone is over a gigabyte, on a host that is also
    running a survey. Alpha is copied through untouched -- muting is about
    colour, and touching alpha would move the footprint boundary, which is the
    one thing in this layer that must stay exactly where the sonar put it.
    """
    gdal = _gdal()
    source = open_source(path)
    width, height = source.RasterXSize, source.RasterYSize
    driver = gdal.GetDriverByName('GTiff')
    out = driver.Create(destination, width, height, 4, gdal.GDT_Byte,
                        options=['COMPRESS=LZW', 'TILED=YES', 'BIGTIFF=YES'])
    out.SetGeoTransform(source.GetGeoTransform())
    out.SetProjection(source.GetProjection())
    out.GetRasterBand(4).SetColorInterpretation(gdal.GCI_AlphaBand)
    for top in range(0, height, block_rows):
        rows = min(block_rows, height - top)
        rgb = np.dstack([source.GetRasterBand(i).ReadAsArray(0, top, width,
                                                             rows)
                         for i in (1, 2, 3)]).astype(np.float32)
        muted = np.clip(mute_rgb(rgb, saturation, lighten, tint), 0, 255)
        for index in (1, 2, 3):
            out.GetRasterBand(index).WriteArray(
                muted[..., index - 1].astype(np.uint8), 0, top)
        out.GetRasterBand(4).WriteArray(
            source.GetRasterBand(4).ReadAsArray(0, top, width, rows), 0, top)
    out.FlushCache()
    return destination


def mosaic_to_mercator(paths, destination):
    """Warp `paths` into one Web Mercator RGBA mosaic, later sources on top.

    One gdal.Warp call over the whole list rather than a VRT: a VRT stacks its
    sources band by band with no notion of alpha, so a later source's
    TRANSPARENT pixels overwrite an earlier source's data and punch the
    previous day's coverage full of holes wherever the current day's export
    merely extends past it. Warp reads the source alpha as a validity mask, so
    each source paints only where it actually has ground -- which is what
    "later sources win where they overlap" has to mean.

    Bilinear, not cubic: cubic rings at the hard alpha edge these exports have,
    and a bright halo tracing every survey line is exactly the kind of artifact
    that gets read as data.
    """
    gdal = _gdal()
    warped = gdal.Warp(destination, list(paths), dstSRS='EPSG:3857',
                       srcAlpha=True, dstAlpha=True, resampleAlg='bilinear',
                       multithread=True,
                       creationOptions=['COMPRESS=LZW', 'TILED=YES',
                                        'BIGTIFF=YES'])
    warped.FlushCache()
    return warped


def mosaic_bounds(dataset):
    """Return [[south, west], [north, east]] in degrees for a 3857 dataset."""
    from osgeo import osr
    transform = dataset.GetGeoTransform()
    left, top = transform[0], transform[3]
    right = left + transform[1] * dataset.RasterXSize
    bottom = top + transform[5] * dataset.RasterYSize
    source = osr.SpatialReference(wkt=dataset.GetProjection())
    target = osr.SpatialReference()
    target.ImportFromEPSG(4326)
    # Without this, GDAL 3 honours the authority's own axis order and hands
    # back (lat, lon) for EPSG:4326 -- silently transposed, and a survey off
    # the coast of Somalia.
    target.SetAxisMappingStrategy(osr.OAMS_TRADITIONAL_GIS_ORDER)
    convert = osr.CoordinateTransformation(source, target)
    west, south = convert.TransformPoint(left, bottom)[:2]
    east, north = convert.TransformPoint(right, top)[:2]
    # Released here, transform first. The CoordinateTransformation holds both
    # SpatialReferences, and left to interpreter shutdown SWIG cannot find a
    # destructor for them -- every run ended with two "detected a memory leak
    # of type OSRSpatialReferenceShadow" lines on stderr. Harmless, and
    # exactly the kind of harmless that trains an operator to ignore this
    # script's output.
    del convert, source, target
    return [[south, west], [north, east]]


def render_pyramid(source, outdir, min_zoom, max_zoom, processes=1):
    """Cut `source` into an XYZ pyramid under `outdir`.

    --xyz is load-bearing. gdal2tiles defaults to TMS, whose Y axis runs the
    other way, and the page's tile URL is the plain Leaflet {z}/{x}/{y}. Get
    this wrong and every tile resolves, every request returns 200, and the
    survey appears mirrored about the equator -- no error anywhere.
    """
    from osgeo_utils import gdal2tiles
    argv = ['gdal2tiles.py', '--profile=mercator', '--xyz',
            '--zoom={}-{}'.format(min_zoom, max_zoom),
            '--resampling=average', '--webviewer=none', '--no-kml',
            # Without --exclude, gdal2tiles writes the mosaic's whole bounding
            # RECTANGLE, empty tiles included. These exports are survey lines
            # inside an 8 x 10 km box that is ~1.5 % covered, so that is
            # thousands of fully transparent PNGs -- PUT, stored, served, and
            # re-fetched by every viewer to paint nothing. The page already
            # treats a missing tile as "not surveyed" (errorTileUrl), which is
            # the same picture for free.
            '--exclude',
            '--processes={}'.format(processes), '-q', source, outdir]
    status = gdal2tiles.main(argv)
    if status:
        raise RuntimeError('gdal2tiles exited {}'.format(status))
    return outdir


def walk_tiles(outdir):
    """Return the pyramid's PNGs as (local path, key-relative path) pairs.

    PNGs only: gdal2tiles also drops tilemapresource.xml and, depending on
    version and options, a viewer HTML beside them. Uploading those would put
    a second, unversioned description of the pyramid in the bucket next to the
    manifest that is meant to be the only one.
    """
    found = []
    for root, _, names in os.walk(outdir):
        for name in sorted(names):
            if not name.endswith('.png'):
                continue
            path = os.path.join(root, name)
            found.append((path, os.path.relpath(path, outdir)))
    return sorted(found, key=lambda pair: pair[1])


# ---------------------------------------------------------------------------
# Run lock. Same shape, and the same reasoning, as refresh_chart_tiles.py's.
# ---------------------------------------------------------------------------

def _lock_opener(path, flags):
    """Open the lock file without following a symlink, mode 0600."""
    # The caller's 'w' carries O_TRUNC, and truncating the lock file before
    # knowing whether the flock succeeded destroys the holder record the
    # failure path reads. Same reasoning as refresh_chart_tiles.py.
    del flags
    return os.open(path, os.O_CREAT | os.O_WRONLY | os.O_NOFOLLOW, 0o600)


def lock_dir():
    """Return a per-user directory to keep the run lock in.

    NOT the workdir: that defaults under /tmp, a world-writable tree this
    script does not own, where a squatted lock file would stop every future
    run. $XDG_RUNTIME_DIR when there is one, ~/.cache otherwise (which is
    where a cron run with no session lands).
    """
    base = os.environ.get('XDG_RUNTIME_DIR')
    path = (os.path.join(base, 'p11-tiles') if base
            else os.path.join(os.path.expanduser('~'), '.cache', 'p11-tiles'))
    os.makedirs(path, mode=0o700, exist_ok=True)
    # lstat, not stat: a symlink here is exactly the substitution being guarded
    # against, and stat() would follow it and report the target. A symlink is in
    # practice already rejected by the mode check below (a link's own mode is
    # 0o777), but only incidentally and with a misleading reason -- say what is
    # actually wrong. refresh_chart_tiles.py carries the same guard.
    info = os.lstat(path)
    if not stat.S_ISDIR(info.st_mode):
        raise RuntimeError('{} is not a directory'.format(path))
    if info.st_uid != os.geteuid():
        raise RuntimeError('{} is owned by uid {}, not by this user'.format(
            path, info.st_uid))
    if info.st_mode & (stat.S_IWGRP | stat.S_IWOTH):
        raise RuntimeError(
            '{} is group- or world-writable ({:o}); refusing to keep a run '
            'lock there'.format(path, stat.S_IMODE(info.st_mode)))
    return path


def acquire_run_lock(name='history'):
    """Take the exclusive run lock, or raise RuntimeError saying who holds it.

    Two runs would interleave writes into the same prefix and, worse, race the
    un-publish: run A's removal plan is computed against an index run B is in
    the middle of replacing, so A deletes tiles B has just published and the
    page serves holes over surveyed ground until someone notices.

    The handle must stay referenced for the life of the run -- closing it
    releases the lock. flock is released by the kernel on exit, so a killed
    run strands nothing.
    """
    path = os.path.join(lock_dir(), '{}.lock'.format(name))
    handle = open(path, 'w', opener=_lock_opener)
    try:
        fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        handle.close()
        try:
            with open(path) as held:
                pid, started = held.read().split()[:2]
            age = max(0.0, time.time() - int(started))
        except (OSError, ValueError, IndexError):
            pid, age = None, None
        detail = '' if pid is None else ' by pid {} ({:.0f} s ago)'.format(
            pid, age)
        wedged = (age is not None and age > LOCK_STALE_SECONDS)
        raise RuntimeError('{} is held{}{}'.format(
            path, detail,
            ' -- older than {} s, so that run is wedged, not slow'.format(
                LOCK_STALE_SECONDS) if wedged else ''))
    handle.truncate(0)
    handle.write('{} {}\n'.format(os.getpid(), int(time.time())))
    handle.flush()
    return handle


# ---------------------------------------------------------------------------
# S3. boto3 is imported lazily for the same reason GDAL is.
# ---------------------------------------------------------------------------

def s3_client(profile):
    """Build an S3 client for `profile`, or the default chain if it is empty.

    An empty profile means "use boto3's own credential chain" -- how an
    instance role is picked up with no key on disk -- and is passed as no
    profile at all rather than as an empty string, matching how
    coverage_renderer treats the same parameter.
    """
    import boto3
    from botocore.config import Config
    session = (boto3.Session(profile_name=profile) if profile
               else boto3.Session())
    return session.client('s3', config=Config(
        connect_timeout=10, read_timeout=60,
        retries={'max_attempts': 3, 'mode': 'standard'}))


def load_index(client):
    """Return the keys the previous run published, or [] if there is none.

    A read that FAILS is not the same as an absent index, and the difference
    is destructive: treating an unreadable index as empty makes this run's
    removal plan empty too, which is the safe direction (nothing is deleted)
    -- so it is deliberately not raised. What must never happen is the
    reverse, deleting on no evidence, and that cannot arise from this shape.
    """
    try:
        body = client.get_object(Bucket=BUCKET, Key=INDEX_KEY)
        keys = json.loads(body['Body'].read())
    except Exception:
        return []
    return [k for k in keys if isinstance(k, str)] if isinstance(keys, list) \
        else []


def upload_tiles(client, tiles, concurrency, deadline_seconds,
                 log=print):
    """PUT every tile, bounded by `concurrency` and an aggregate deadline.

    The deadline is checked before a job STARTS rather than enforced per
    request: a run that overruns leaves the manifest unwritten and the tiles
    it did upload standing, which is recoverable by re-running. A run that
    hangs forever holds the lock and stops every future one, which is not.
    """
    import concurrent.futures
    started = time.monotonic()
    done = 0
    # Clamp: ThreadPoolExecutor(0) raises ValueError, so an operator typo
    # (--concurrency 0) would abort the run with a stack trace rather than a
    # controlled error. refresh_chart_tiles.py clamps the same way.
    with concurrent.futures.ThreadPoolExecutor(max(1, int(concurrency))) as pool:
        futures = {}
        for path, relative in tiles:
            if time.monotonic() - started > deadline_seconds:
                raise RuntimeError(
                    'upload deadline ({} s) reached after {}/{} tiles; the '
                    'manifest was NOT written, so the page still describes '
                    'the previous pyramid. Re-run to finish.'.format(
                        deadline_seconds, done, len(tiles)))
            key = PREFIX + relative.replace(os.sep, '/')
            futures[pool.submit(client.upload_file, path, BUCKET, key,
                                ExtraArgs=TILE_EXTRA_ARGS)] = key
        for future in concurrent.futures.as_completed(futures):
            future.result()
            done += 1
            if done % 100 == 0:
                log('  uploaded {}/{}'.format(done, len(tiles)))
    return [PREFIX + rel.replace(os.sep, '/') for _, rel in tiles]


def delete_keys(client, keys, log=print):
    """Delete `keys` in batches of 1000, the DeleteObjects ceiling."""
    for start in range(0, len(keys), 1000):
        batch = keys[start:start + 1000]
        client.delete_objects(
            Bucket=BUCKET,
            Delete={'Objects': [{'Key': k} for k in batch], 'Quiet': True})
        log('  un-published {} tile(s)'.format(len(batch)))


# ---------------------------------------------------------------------------
# Local output, for --dry-run.
# ---------------------------------------------------------------------------

def default_local_dir():
    """Return web/live/history/ inside this package.

    The same relative path the bucket serves, so the page's relative URLs
    resolve identically against a local copy and against CloudFront -- the
    convention the renderers' own dry_run modes already follow.
    """
    package = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(package, 'web', 'live', 'history')


def replace_local_dir(path):
    """Empty `path` for a fresh local render, refusing anything unexpected.

    An earlier preview has to go -- left in place, tiles from a previous
    source list survive underneath the new ones and the preview shows a
    mosaic the bucket would never serve. But this is an rm -rf against a path
    that came off the command line, so it runs ONLY over a directory that
    looks like a previous run of this script: empty, or holding nothing but
    the pyramid and its two JSON objects. Anything else and the operator is
    told to clear it themselves rather than having it done for them.
    """
    if not os.path.exists(path):
        return
    allowed = {'meta.json', 'index.json'}
    for name in os.listdir(path):
        if name in allowed or (name.isdigit() and os.path.isdir(
                os.path.join(path, name))):
            continue
        raise RuntimeError(
            '{} holds {!r}, which this script did not write. Refusing to '
            'delete it -- clear the directory yourself, or point --local-dir '
            'somewhere else.'.format(path, name))
    shutil.rmtree(path)


def write_local(outdir, local_dir, manifest, keys, log=print):
    """Move a rendered pyramid and its JSON into `local_dir`."""
    replace_local_dir(local_dir)
    os.makedirs(os.path.dirname(local_dir), exist_ok=True)
    shutil.move(outdir, local_dir)
    for name, payload in (('meta.json', manifest), ('index.json', keys)):
        with open(os.path.join(local_dir, name), 'w') as handle:
            json.dump(payload, handle, indent=2, sort_keys=True)
    log('wrote {} tile(s) to {}'.format(len(keys), local_dir))


# ---------------------------------------------------------------------------

def parse_args(argv=None):
    """Parse the command line."""
    parser = argparse.ArgumentParser(
        description=__doc__.strip().splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('sources', nargs='+', metavar='GEOTIFF',
                        help='RGBA exports, OLDEST FIRST -- later sources win '
                             'where footprints overlap')
    parser.add_argument('--labels', default='',
                        help='comma-separated label per source, in the same '
                             'order (e.g. survey dates). Defaults to each '
                             "file's basename; deliberately NOT its mtime, "
                             'which dates the export rather than the survey')
    parser.add_argument('--dry-run', action='store_true',
                        help='render locally and touch no AWS credentials')
    parser.add_argument('--local-dir', default=None,
                        help='where --dry-run writes (default: the package '
                             'web/live/history, where the page will find '
                             'it)')
    parser.add_argument('--profile', default='p11-renderer',
                        help='AWS profile. The default is the boat-side '
                             'credential, which can write live/* and nothing '
                             'else -- which is why this prefix lives there. '
                             'Empty means the default credential chain')
    parser.add_argument('--saturation', type=float, default=MUTE_SATURATION)
    parser.add_argument('--lighten', type=float, default=MUTE_LIGHTEN)
    parser.add_argument('--min-zoom', type=int, default=ZOOM_FLOOR)
    parser.add_argument('--max-zoom', type=int, default=None,
                        help='default: derived from the mosaic resolution, '
                             'capped at {}'.format(ZOOM_CEILING))
    parser.add_argument('--concurrency', type=int,
                        default=DEFAULT_CONCURRENCY)
    parser.add_argument('--processes', type=int,
                        default=max(1, (os.cpu_count() or 2) // 2),
                        help='gdal2tiles worker processes')
    parser.add_argument('--workdir', default=None,
                        help='scratch for the muted sources and the mosaic '
                             '(default: a temporary directory, removed on the '
                             'way out)')
    return parser.parse_args(argv)


def main(argv=None):
    """Render the history pyramid and publish it."""
    args = parse_args(argv)
    labels = [part for part in args.labels.split(',') if part]
    try:
        labels = resolve_labels(args.sources, labels)
    except ValueError as exc:
        print('error: {}'.format(exc), file=sys.stderr)
        return 2
    for path in args.sources:
        if not os.path.isfile(path):
            print('error: no such file: {}'.format(path), file=sys.stderr)
            return 2

    # Taken before any work: the point is to keep two runs from interleaving,
    # and the expensive half is the render, not the upload.
    try:
        lock = acquire_run_lock()
    except RuntimeError as exc:
        print('error: {}'.format(exc), file=sys.stderr)
        return 1

    workdir = args.workdir or tempfile.mkdtemp(prefix='p11-history-')
    os.makedirs(workdir, exist_ok=True)
    temporary = args.workdir is None
    try:
        muted = []
        for index, path in enumerate(args.sources):
            print('muting {} ({})'.format(os.path.basename(path),
                                          labels[index]))
            muted.append(mute_source(
                path, os.path.join(workdir, 'muted_{:02d}.tif'.format(index)),
                saturation=args.saturation, lighten=args.lighten))

        print('mosaicking {} source(s) to Web Mercator'.format(len(muted)))
        mosaic_path = os.path.join(workdir, 'mosaic.tif')
        mosaic = mosaic_to_mercator(muted, mosaic_path)
        resolution = abs(mosaic.GetGeoTransform()[1])
        bounds = mosaic_bounds(mosaic)
        derived_min, derived_max = zoom_range(resolution)
        min_zoom = args.min_zoom
        max_zoom = args.max_zoom if args.max_zoom is not None else derived_max
        if min_zoom > max_zoom:
            print('error: --min-zoom {} is above --max-zoom {}'.format(
                min_zoom, max_zoom), file=sys.stderr)
            return 2
        del derived_min
        print('  {:.2f} m/px -> zoom {}-{}'.format(resolution, min_zoom,
                                                   max_zoom))

        outdir = os.path.join(workdir, 'tiles')
        render_pyramid(mosaic_path, outdir, min_zoom, max_zoom,
                       processes=args.processes)
        tiles = walk_tiles(outdir)
        if not tiles:
            print('error: the render produced no tiles -- every source may be '
                  'fully transparent, or outside the mosaic', file=sys.stderr)
            return 1
        print('rendered {} tile(s)'.format(len(tiles)))

        sources = [{'file': os.path.basename(p), 'label': labels[i]}
                   for i, p in enumerate(args.sources)]
        manifest = build_manifest(min_zoom, max_zoom, len(tiles), sources,
                                  bounds, time.time())

        if args.dry_run:
            keys = [PREFIX + rel.replace(os.sep, '/') for _, rel in tiles]
            write_local(outdir, args.local_dir or default_local_dir(),
                        manifest, keys)
            return 0

        client = s3_client(args.profile)
        previous = load_index(client)
        keys = upload_tiles(client, tiles, args.concurrency,
                            UPLOAD_DEADLINE_SECONDS)

        # Index BEFORE manifest, manifest LAST. The manifest is what the page
        # reads, so it must not advertise a pyramid that is not fully
        # uploaded; the index is what the NEXT run's un-publish is computed
        # against, so it must not still describe the previous pyramid once
        # these tiles are live.
        client.put_object(Bucket=BUCKET, Key=INDEX_KEY,
                          Body=json.dumps(sorted(keys)).encode(),
                          ContentType='application/json',
                          CacheControl='no-cache')
        stale = plan_removals(previous, keys)
        if stale:
            delete_keys(client, stale)
        client.put_object(Bucket=BUCKET, Key=META_KEY,
                          Body=json.dumps(manifest, indent=2,
                                          sort_keys=True).encode(),
                          **META_EXTRA_ARGS)
        print('published {} tile(s) to s3://{}/{} ({} un-published)'.format(
            len(keys), BUCKET, PREFIX, len(stale)))
        return 0
    finally:
        lock.close()
        if temporary:
            shutil.rmtree(workdir, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())
