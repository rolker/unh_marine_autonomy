#!/usr/bin/env python3
"""Diff two link outputs BY VALUE, tile by tile (band 1 = mean ellipsoidal height of the seafloor). Usage: 10_compare.py <dirA> <dirB> [label]"""
import sys, glob, os, numpy as np
from osgeo import gdal
gdal.UseExceptions(); A, B = sys.argv[1:3]; label = sys.argv[3] if len(sys.argv) > 3 else f'{os.path.basename(A)} vs {os.path.basename(B)}'
tot = []; only_a = only_b = both = 0
for fa in sorted(glob.glob(f'{A}/10_*.tif')):
    fb = f'{B}/{os.path.basename(fa)}'
    da = gdal.Open(fa); a = da.GetRasterBand(1).ReadAsArray()
    if not os.path.exists(fb): only_a += np.isfinite(a).sum(); continue
    db = gdal.Open(fb); b = db.GetRasterBand(1).ReadAsArray()
    ma, mb = np.isfinite(a), np.isfinite(b); m = ma & mb; both += m.sum(); only_a += (ma & ~mb).sum(); only_b += (mb & ~ma).sum()
    if m.any(): tot.append((a[m] - b[m]))
d = np.concatenate(tot) if tot else np.array([np.nan])
print(f'{label}: cells both {both}, only-A {only_a}, only-B {only_b} | dz A-B: median {np.median(d):+.3f} m, mean {d.mean():+.3f}, p5 {np.percentile(d,5):+.3f}, p95 {np.percentile(d,95):+.3f}, max|dz| {np.abs(d).max():.3f}')
