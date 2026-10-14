#!/usr/bin/env python3
"""Spine decision 2 evidence: how far apart are MIN (shoalest) and MEAN folds of the
Massabesic depths at 1, 2 and 3 fold steps (2x2, 4x4, 8x8), compared with per-cell uncertainty."""
import glob, numpy as np
from osgeo import gdal
gdal.UseExceptions()
tiles = sorted(glob.glob('/home/roland/data/world/depths/processed/10_*.tif'))
res = {k: [] for k in (2, 4, 8)}
nblund=[0]
depth_all = []; unc_all = []
def fold(a, k, fn):
    h, w = a.shape; a = a[:h//k*k, :w//k*k].reshape(h//k, k, w//k, k).transpose(0,2,1,3).reshape(h//k, w//k, k*k)
    return fn(a, axis=2)
for t in tiles:
    ds = gdal.Open(t); d = ds.GetRasterBand(1).ReadAsArray().astype(float); u = ds.GetRasterBand(2).ReadAsArray().astype(float)
    ok = np.isfinite(d)
    blunder = ok & ((np.abs(d - 45.0) > 30.0) | ~np.isfinite(u) | (u > 10.0))   # bed height ~40-50 m ellipsoidal; anything wild is a blunder
    nblund[0] += int(blunder.sum()); ok &= ~blunder
    depth_all.append(d[ok]); unc_all.append(u[ok])
    for k in res:
        cnt = fold(ok.astype(int), k, np.sum)
        mn = fold(np.where(ok, d, np.inf), k, np.min); mx = fold(np.where(ok, d, -np.inf), k, np.max)
        sm = fold(np.where(ok, d, 0.0), k, np.sum); umean = fold(np.where(ok & np.isfinite(u), u, 0.0), k, np.sum) / np.maximum(cnt, 1)
        full = cnt >= max(2, (k*k)//2)          # parents with at least half their children
        mean = sm[full] / cnt[full]
        res[k].append(np.column_stack([mn[full], mean, mx[full], cnt[full], umean[full]]))
d = np.concatenate(depth_all); u = np.concatenate(unc_all)
print(f"tiles: {len(tiles)}   native cells kept: {d.size:,}   blunders masked: {nblund[0]:,}")
print(f"bed height (m, ellipsoidal, +up)  min {d.min():.2f}  median {np.median(d):.2f}  max {d.max():.2f}")
print(f"uncertainty (band 2)              min {u.min():.3f}  median {np.median(u):.3f}  p95 {np.percentile(u,95):.3f}  max {u.max():.3f}")
sign = -1.0   # heights: shoalest = MAX; convert to positive-down below
for k in res:
    r = np.concatenate(res[k]); mn, mean, mx, cnt, um = r.T
    if sign < 0: mn, mx = -mx, -mn; mean = -mean          # convert to positive-down depth
    gap = mean - mn                                     # how much shoaler MIN is than MEAN, metres
    rel = gap / np.maximum(50.5 - mn, 0.5)     # relative to water depth, taking ~50.5 m as the surface height (max bed height seen)
    print(f"\n== fold {k}x{k} ({k*0.9:.1f} m cells; {len(gap):,} parent cells) ==")
    for q in (50, 90, 95, 99): print(f"  MEAN-MIN gap  p{q}: {np.percentile(gap,q):6.3f} m   ({np.percentile(rel,q)*100:5.1f}% of depth)")
    print(f"  gap > mean cell uncertainty: {np.mean(gap > um)*100:5.1f}%   gap > 2x: {np.mean(gap > 2*um)*100:5.1f}%   gap > 0.5 m: {np.mean(gap > 0.5)*100:5.1f}%   gap > 1 m: {np.mean(gap > 1.0)*100:5.1f}%")
    print(f"  MAX-MIN spread p50 {np.percentile(mx-mn,50):.3f} m  p95 {np.percentile(mx-mn,95):.3f} m")
