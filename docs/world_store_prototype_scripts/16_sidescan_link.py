#!/usr/bin/env python3
"""Sidescan LINK — everything decision 4 keeps out of the observation, applied here: sound speed (a link input, taken from
the M3's transducer series in the same bag — the surface sound-speed series water/ will hold), slant→ground range with the
sonar's held nadir altitude (flat-bottom; the DEM path is marine_sidescan_mosaic's), pose from a trajectory, mounting from a
geometry revision (NONE recorded for the sidescan in this bag → ASSUMED at base_link). Writes level-10 tiles (mean intensity +
count) and a fingerprint-keyed CACHE of the per-sample ground ranges beside the observation.
Usage: 16_sidescan_link.py <ss_obs.zarr> <traj.parquet> <m3_obs.zarr> <out_dir>"""
import sys, os, json, hashlib, numpy as np, xarray as xr, pyarrow.parquet as pq, zarr
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent)); import link_lib as L
ss = xr.open_zarr(sys.argv[1]); tr = pq.read_table(sys.argv[2]); m3 = xr.open_zarr(sys.argv[3]); out = Path(sys.argv[4]); out.mkdir(parents=True, exist_ok=True)
t = ss.time.values; fs = ss.sample_rate_hz.values[:, None]; s0 = ss.sample0.values[:, None]; nsb = ss.samples_per_beam.values[:, None]; j = np.arange(ss.sizes['sample'])[None, :]
c = np.interp(t, m3.time.values, m3.sound_speed_at_transducer.values)                          # sound speed: LINK input (surface series)
slant = (s0 + j) * c[:, None] / (2.0 * fs); alt = np.where(ss.nadir_depth_age_s.values <= 5.0, ss.nadir_depth.values, np.nan)[:, None]
ground = np.sqrt(np.maximum(slant**2 - alt**2, 0.0)); ground[(slant <= alt) | (j >= nsb)] = np.nan            # samples above the bottom (water column) or beyond this ping's count
T = tr['time'].to_numpy(); i = np.clip(np.searchsorted(T, t), 1, len(T)-1); okp = ~(((T[i]-T[i-1]) > 2.0) | (t < T[0]) | (t > T[-1]))
g = lambda k: np.interp(t, T, tr[k].to_numpy()); lat, lon = g('latitude'), g('longitude'); h = np.interp(t, T, np.unwrap(tr['heading'].to_numpy())) % (2*np.pi)
mount = [0.0, 0.0, 0.0]; geom = 'ASSUMED base_link (no sidescan mount in tf_static; needs a geometry revision)'
ex, ey = np.cos(h)[:, None], -np.sin(h)[:, None]                                                   # unit vector to STARBOARD in (east, north) for heading h (from north, clockwise)
R = 6378137.0; res = {}
for side, sign in (('port', -1.0), ('starboard', 1.0)):
    e = sign * ground * ex + mount[0]; n = sign * ground * ey + mount[1]
    LAT = np.degrees(lat[:, None] + n / R); LON = np.degrees(lon[:, None] + e / (R * np.cos(lat[:, None])))
    v = np.isfinite(ground) & okp[:, None]; I = ss[side].values.astype(np.float32)
    res[side] = (LAT[v], LON[v], I[v])
LAT = np.concatenate([r[0] for r in res.values()]); LON = np.concatenate([r[1] for r in res.values()]); I = np.concatenate([r[2] for r in res.values()])
uy, ux, mean, cnt, _ = L.grid(LAT, LON, I)
fp = hashlib.sha256(f"{ss.attrs['source_id']}|ss-window={ss.attrs['window_t0']}|traj={tr.schema.metadata.get(b'source', b'?').decode()}|c=m3-transducer-series|flat-bottom|held-nadir<=5s|mount={mount}".encode()).hexdigest()
prov = {'observations': ss.attrs['source_id'], 'observation_window_t0': ss.attrs['window_t0'], 'trajectory': tr.schema.metadata.get(b'source', b'?').decode(), 'sound_speed': 'M3 transducer series at link (NOT the sidescan driver placeholder 1500)', 'ground_range': 'flat-bottom sqrt(slant^2 - alt^2), held nadir <= 5 s', 'geometry': geom, 'gridder': 'mean+count stand-in', 'fingerprint': fp}
L.write_tiles(str(out), uy, ux, mean, cnt, prov); json.dump(prov, open(out / 'provenance.json', 'w'), indent=1)
cache = Path(str(sys.argv[1]).rstrip('/') + '.link_cache') / fp[:16]; cache.mkdir(parents=True, exist_ok=True)
gz = zarr.open(str(cache / 'ground_range.zarr'), mode='w'); gz.create_dataset('ground_range_m', data=ground.astype(np.float32), chunks=(2000, ground.shape[1])); gz.create_dataset('sound_speed_used', data=c.astype(np.float32)); gz.create_dataset('altitude_used', data=alt[:, 0].astype(np.float32)); gz.attrs.update({'cache_of': 'slant_to_ground_range', 'fingerprint': fp, 'inputs': prov, 'note': 'cache only; raw samples/nadir untouched'})
# the numbers
smax = np.nanmax(slant[:, :1] * 0 + (s0 + nsb - 1) * c[:, None] / (2 * fs)); shift = smax * (1 - c.min() / 1500.0)
print(f"pings {t.size}: with pose {okp.sum()}, with altitude {np.isfinite(alt[:,0]).sum()}; altitude p50 {np.nanmedian(alt):.2f} m (range {np.nanmin(alt):.2f}–{np.nanmax(alt):.2f})")
print(f"sound speed at link {c.min():.1f}–{c.max():.1f} m/s vs driver placeholder 1500.0 → max slant range {smax:.1f} m, placeholder would misplace the outer sample by up to {shift:.2f} m")
print(f"samples placed {LAT.size:,} of {ground.size*2:,} ({LAT.size/(ground.size*2)*100:.1f}%); water-column samples (slant <= alt) {int(np.sum(slant <= alt)):,}; cells {uy.size:,}; tiles written to {out}")
print(f"cache {cache}")
