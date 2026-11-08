#!/usr/bin/env python3
"""Link (catalog-aware): observations + trajectory + geometry -> georeferenced soundings -> level-10 tiles (mean+count stand-in gridder).
Geometry: a catalog 'platform_geometry' Item REPLACES the mounting recorded in the bag; otherwise the recorded one is used.
Usage: 10_link.py <obs.zarr> <traj.parquet> <out_dir> [--catalog items.json]"""
import sys, os, json, argparse, numpy as np, xarray as xr, pyarrow.parquet as pq
from pathlib import Path
from osgeo import gdal, osr
sys.path.insert(0, str(Path(__file__).parent)); import corrections_lib as cl
gdal.UseExceptions(); ap = argparse.ArgumentParser(); ap.add_argument('obs'); ap.add_argument('traj'); ap.add_argument('out'); ap.add_argument('--catalog'); a = ap.parse_args(); os.makedirs(a.out, exist_ok=True)
ds = xr.open_zarr(a.obs); tr = pq.read_table(a.traj); meta = {k.decode(): v.decode() for k, v in tr.schema.metadata.items()}
t = ds.time.values; mount = json.loads(ds.attrs['recorded_mounting_m']); geom_src = 'recorded tf_static'
for it in cl.applicable(cl.load(a.catalog), 'link', t[0], t[-1], platform='bizzyboat', child=ds.attrs['sensor_frame']):
    if it['properties']['worldstore:kind'] == 'platform_geometry': mount = it['properties']['worldstore:parameters']['translation_m']; geom_src = it['id']
T = tr['time'].to_numpy(); i = np.clip(np.searchsorted(T, t), 1, len(T)-1); ok_pose = ~(((T[i] - T[i-1]) > 2.0) | (t < T[0]) | (t > T[-1]))
g = lambda c: np.interp(t, T, tr[c].to_numpy()); lat_p, lon_p, alt_p, r_p, p_p = g('latitude'), g('longitude'), g('altitude'), g('roll'), g('pitch')
h_p = np.interp(t, T, np.unwrap(tr['heading'].to_numpy())) % (2*np.pi)
rng = ds.two_way_travel_time.values * 0.5 * ds.sound_speed_at_transducer.values[:, None]; tx = ds.tx_angle.values; rx = ds.rx_angle.values
xs = rng * -np.sin(tx); ys = rng * np.sin(rx); zs = rng * np.cos(tx) * np.cos(rx)
xb = xs + mount[0]; yb = -ys + mount[1]; zb = -zs + mount[2]                      # sonar frame is rolled 180 deg about x
yaw = np.pi/2 - h_p; cr, sr, cp, sp, cy, sy = [f(v)[:, None] for v, f in ((r_p, np.cos), (r_p, np.sin), (p_p, np.cos), (p_p, np.sin), (yaw, np.cos), (yaw, np.sin))]
e = cy*cp*xb + (cy*sp*sr - sy*cr)*yb + (cy*sp*cr + sy*sr)*zb; n = sy*cp*xb + (sy*sp*sr + cy*cr)*yb + (sy*sp*cr - cy*sr)*zb; u = -sp*xb + cp*sr*yb + cp*cr*zb
R = 6378137.0; LAT = np.degrees(lat_p[:, None] + n / R); LON = np.degrees(lon_p[:, None] + e / (R * np.cos(lat_p[:, None]))); Z = alt_p[:, None] + u
valid = np.isfinite(rng) & (rng > 0) & ok_pose[:, None] & (ds.detection_flag.values == 0)
span = 8.0/1024; cell = span/960; gy = np.floor((LAT[valid] + 96)/cell).astype(np.int64); gx = np.floor((LON[valid] + 180)/cell).astype(np.int64)
uk, inv = np.unique(gy * 100_000_000 + gx, return_inverse=True); mean = np.bincount(inv, Z[valid]) / np.bincount(inv); cnt = np.bincount(inv)
tiles = {}
for y, x, m_, c_ in zip(uk // 100_000_000, uk % 100_000_000, mean, cnt):
    k = (y // 960, x // 960); arr = tiles.setdefault(k, [np.full((960, 960), np.nan), np.zeros((960, 960))]); arr[0][959 - int(y - k[0]*960), int(x - k[1]*960)] = m_; arr[1][959 - int(y - k[0]*960), int(x - k[1]*960)] = c_
prov = {'observations': ds.attrs['source_id'], 'observation_corrections': ds.attrs['corrections_applied'], 'trajectory_corrections': meta.get('corrections_applied'), 'geometry': f'{geom_src} {mount}', 'gridder': 'mean+count stand-in, not CUBE'}
srs = osr.SpatialReference(); srs.ImportFromEPSG(4326)
for (ty, tx_), (m_, c_) in tiles.items():
    path = f'{a.out}/10_{ty}_{tx_}.tif'; tmp = path + '.tmp.tif'; d = gdal.GetDriverByName('GTiff').Create(tmp, 960, 960, 2, gdal.GDT_Float64, ['TILED=YES'])
    d.SetGeoTransform([-180 + tx_*span, cell, 0, -96 + (ty+1)*span, 0, -cell]); d.SetProjection(srs.ExportToWkt())
    for b, v in ((1, m_), (2, np.where(c_ > 0, c_, np.nan))): d.GetRasterBand(b).WriteArray(v); d.GetRasterBand(b).SetNoDataValue(float('nan'))
    for k, v in prov.items(): d.SetMetadataItem('WORLDSTORE_' + k.upper(), str(v))
    d = None; gdal.Translate(path, tmp, format='COG', creationOptions=['COMPRESS=DEFLATE', 'PREDICTOR=3']); os.remove(tmp)
json.dump(prov, open(f'{a.out}/provenance.json', 'w'), indent=1)
print(f'{Path(a.out).name}: valid soundings {valid.sum()}, no-pose pings {int((~ok_pose).sum())}, tiles {len(tiles)}, geometry {geom_src} {mount}')
