#!/usr/bin/env python3
"""Sound-velocity correction AT LINK with a fingerprint-tagged cache (spine decision 4's cache clause) — process test.
Ray-traces every valid sounding through a water/ profile (layered, Snell) and compares with the straight-line
(surface-sound-speed) geometry the link uses today. Writes the SVC result beside the observation as a cache keyed by the
fingerprint of its inputs (observation source_id + profile fingerprint + method + draft); never touches the raw variables.
Usage: 14_svc_link.py <obs.zarr> <profile_item.json> [--draft 0.3] [--out cache_dir]"""
import sys, os, json, hashlib, argparse, numpy as np, xarray as xr, zarr
from pathlib import Path
ap = argparse.ArgumentParser(); ap.add_argument('obs'); ap.add_argument('profile'); ap.add_argument('--draft', type=float, default=0.3, help='transducer depth below the surface, m (ASSUMED; not in the bag)'); ap.add_argument('--out'); a = ap.parse_args()
ds = xr.open_zarr(a.obs); it = json.load(open(a.profile)); s = it['samples']
d = np.array([x['depth_m'] for x in s]); c = np.array([x['sound_speed_m_s'] for x in s]); fl = np.array([x['flag'] for x in s])
meas_max = float(it['properties']['measured_depth_max_m'])
# use measured + suspect samples as given; keep padded samples so deep rays have *something*, but flag soundings that reach them
# fine layering below the transducer
zg = np.arange(a.draft, min(d.max(), 60.0), 0.02); cg = np.interp(zg, d, c); dz = np.diff(zg); cl_ = 0.5 * (cg[:-1] + cg[1:])   # fine layers only as deep as a ray here can reach (the padded profile runs to 12 km)
th0 = np.radians(np.arange(0.0, 75.0, 0.05)); p = np.sin(th0)[:, None] / cg[0]            # Snell constant per launch angle
sin_ = np.clip(p * cl_[None, :], 0, 0.999999); cos_ = np.sqrt(1 - sin_**2)
dt_ = dz[None, :] / (cl_[None, :] * cos_); dx_ = dz[None, :] * sin_ / cos_                  # time and horizontal step per layer
t_cum = np.concatenate([np.zeros((th0.size, 1)), np.cumsum(dt_, 1)], 1); x_cum = np.concatenate([np.zeros((th0.size, 1)), np.cumsum(dx_, 1)], 1)
turned = (p * cl_[None, :]) >= 0.999999; t_cum[:, 1:][np.cumsum(turned, 1) > 0] = np.nan     # ray turned: no solution beyond
tt = np.arange(0.0, 0.03, 2e-5)
Z = np.full((th0.size, tt.size), np.nan); X = np.full_like(Z, np.nan)
for i in range(th0.size):
    ok = np.isfinite(t_cum[i]); Z[i] = np.interp(tt, t_cum[i][ok], zg[ok], right=np.nan); X[i] = np.interp(tt, t_cum[i][ok], x_cum[i][ok], right=np.nan)
# per sounding
twtt = ds.two_way_travel_time.values; tx = ds.tx_angle.values; rx = ds.rx_angle.values; cs = ds.sound_speed_at_transducer.values[:, None]
valid = np.isfinite(twtt) & (twtt > 0) & (ds.detection_flag.values == 0)
th = np.nan_to_num(np.arccos(np.clip(np.cos(tx) * np.cos(rx), -1, 1))); t1 = np.nan_to_num(twtt * 0.5)   # invalid beams carry NaN; they are masked by `valid` below
ia = np.clip(th / np.radians(0.05), 0, th0.size - 2); it_ = np.clip(t1 / 2e-5, 0, tt.size - 2); i0 = ia.astype(int); j0 = it_.astype(int); fa = ia - i0; ft = it_ - j0
bil = lambda T: (1-fa)*(1-ft)*T[i0, j0] + fa*(1-ft)*T[i0+1, j0] + (1-fa)*ft*T[i0, j0+1] + fa*ft*T[i0+1, j0+1]
z_svc = bil(Z) - a.draft; r_svc = bil(X)                                                       # depth below transducer, horizontal range
rng = t1 * cs; z_str = rng * np.cos(th); r_str = rng * np.sin(th)
reached_padding = z_svc + a.draft > meas_max
fp = hashlib.sha256(f"{ds.attrs['source_id']}|{it['properties']['worldstore:fingerprint']}|layered-snell-0.02m|draft={a.draft}".encode()).hexdigest()
out = Path(a.out or (str(a.obs).rstrip('/') + '.svc_cache')) / fp[:16]; out.mkdir(parents=True, exist_ok=True)
g = zarr.open(str(out / 'svc.zarr'), mode='w')
g.create_dataset('z_below_transducer', data=np.where(valid, z_svc, np.nan).astype(np.float32)); g.create_dataset('horizontal_range', data=np.where(valid, r_svc, np.nan).astype(np.float32))
g.create_dataset('reached_padded_profile', data=(valid & reached_padding).astype(np.uint8))
g.attrs.update({'cache_of': 'sound_velocity_correction', 'fingerprint': fp, 'inputs': {'observation': ds.attrs['source_id'], 'profile': it['id'], 'profile_fingerprint': it['properties']['worldstore:fingerprint'], 'method': 'layered Snell, 0.02 m layers, lookup 0.05deg x 20us', 'draft_m_ASSUMED': a.draft},
                'note': 'cache only — raw two_way_travel_time / angles / sound_speed_at_transducer are untouched; invalid when any input fingerprint changes'})
json.dump(dict(g.attrs), open(out / 'cache.json', 'w'), indent=1)
# the numbers
dzv = (z_svc - z_str)[valid]; drv = (r_svc - r_str)[valid]; zv = z_str[valid]; thv = np.degrees(th[valid]); pad = reached_padding[valid]
print(f"soundings {valid.sum():,}; profile surface c {cg[0]:.2f} vs bag surface c {cs.min():.2f}–{cs.max():.2f} m/s (cast 06-16, bag 06-22)")
print(f"reached the exporter's padded profile (deeper than {meas_max:.2f} m measured): {pad.mean()*100:.1f}% of soundings")
print(f"depth change (svc - straight)  p50 {np.percentile(dzv,50):+.3f}  p5 {np.percentile(dzv,5):+.3f}  p95 {np.percentile(dzv,95):+.3f}  |max| {np.abs(dzv).max():.3f} m")
print(f"horizontal change              p50 {np.percentile(drv,50):+.3f}  p5 {np.percentile(drv,5):+.3f}  p95 {np.percentile(drv,95):+.3f}  |max| {np.abs(drv).max():.3f} m")
for lo, hi in ((0, 20), (20, 40), (40, 50), (50, 60), (60, 75)):
    m = (thv >= lo) & (thv < hi)
    if m.any(): print(f"  angle {lo:2d}-{hi:2d}°: n={m.sum():9,}  dz p50 {np.percentile(dzv[m],50):+.3f} p95 {np.percentile(np.abs(dzv[m]),95):.3f}   dr p50 {np.percentile(drv[m],50):+.3f} p95 {np.percentile(np.abs(drv[m]),95):.3f}   depth p50 {np.percentile(zv[m],50):.1f} m")
for lo, hi in ((0, 3), (3, 6), (6, 10), (10, 30)):
    m = (zv >= lo) & (zv < hi)
    if m.any(): print(f"  depth {lo:2d}-{hi:2d} m: n={m.sum():9,}  dz p50 {np.percentile(dzv[m],50):+.3f} p95 {np.percentile(np.abs(dzv[m]),95):.3f}   dr p95 {np.percentile(np.abs(drv[m]),95):.3f}")
print(f"cache: {out}")
