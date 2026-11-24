"""Shared link core: georeference observations with a trajectory + catalog geometry; grid to GGGS level-10 tiles."""
import json, os, numpy as np, xarray as xr, pyarrow.parquet as pq
from osgeo import gdal, osr
import corrections_lib as cl
gdal.UseExceptions()
SPAN = 8.0 / 1024; CELL = SPAN / 960; R = 6378137.0
def georef(obs_path, traj_path, catalog=None, shift_en=(0.0, 0.0)):
    ds = xr.open_zarr(obs_path); tr = pq.read_table(traj_path); t = ds.time.values
    mount = json.loads(ds.attrs['recorded_mounting_m']); geom = 'recorded tf_static'
    for it in cl.applicable(cl.load(catalog), 'link', t[0], t[-1], platform='bizzyboat', child=ds.attrs['sensor_frame']):
        if it['properties']['worldstore:kind'] == 'platform_geometry': mount = it['properties']['worldstore:parameters']['translation_m']; geom = it['id']
    T = tr['time'].to_numpy(); i = np.clip(np.searchsorted(T, t), 1, len(T)-1); ok = ~(((T[i]-T[i-1]) > 2.0) | (t < T[0]) | (t > T[-1]))
    g = lambda c: np.interp(t, T, tr[c].to_numpy()); lat, lon, alt, r_, p_ = g('latitude'), g('longitude'), g('altitude'), g('roll'), g('pitch')
    h = np.interp(t, T, np.unwrap(tr['heading'].to_numpy())) % (2*np.pi)
    rng = ds.two_way_travel_time.values * 0.5 * ds.sound_speed_at_transducer.values[:, None]; tx = ds.tx_angle.values; rx = ds.rx_angle.values
    xs = rng * -np.sin(tx); ys = rng * np.sin(rx); zs = rng * np.cos(tx) * np.cos(rx)
    xb = xs + mount[0]; yb = -ys + mount[1]; zb = -zs + mount[2]
    yaw = np.pi/2 - h; cr, sr, cp, sp, cy, sy = [f(v)[:, None] for v, f in ((r_, np.cos), (r_, np.sin), (p_, np.cos), (p_, np.sin), (yaw, np.cos), (yaw, np.sin))]
    e = cy*cp*xb + (cy*sp*sr - sy*cr)*yb + (cy*sp*cr + sy*sr)*zb + shift_en[0]; n = sy*cp*xb + (sy*sp*sr + cy*cr)*yb + (sy*sp*cr - cy*sr)*zb + shift_en[1]
    u = -sp*xb + cp*sr*yb + cp*cr*zb
    LAT = np.degrees(lat[:, None] + n / R); LON = np.degrees(lon[:, None] + e / (R*np.cos(lat[:, None]))); Z = alt[:, None] + u
    valid = np.isfinite(rng) & (rng > 0) & ok[:, None] & (ds.detection_flag.values == 0)
    recv = np.broadcast_to(ds.receive_time.values[:, None], rng.shape); beam = np.broadcast_to(np.arange(rng.shape[1])[None, :], rng.shape)
    return dict(LAT=LAT, LON=LON, Z=Z, valid=valid, recv=recv, beam=beam, source_id=ds.attrs['source_id'], geometry=f'{geom} {mount}', obs_attrs=dict(ds.attrs))
def cell_index(LAT, LON):
    return np.floor((LAT + 96) / CELL).astype(np.int64), np.floor((LON + 180) / CELL).astype(np.int64)
def grid(LAT, LON, Z):
    gy, gx = cell_index(LAT, LON); key = gy * 100_000_000 + gx; uk, inv = np.unique(key, return_inverse=True)
    return uk // 100_000_000, uk % 100_000_000, np.bincount(inv, Z) / np.bincount(inv), np.bincount(inv), key
def write_tiles(out, uy, ux, mean, cnt, prov):
    os.makedirs(out, exist_ok=True); tiles = {}
    for y, x, m_, c_ in zip(uy, ux, mean, cnt):
        k = (y // 960, x // 960); a = tiles.setdefault(k, [np.full((960, 960), np.nan), np.zeros((960, 960))]); a[0][959 - int(y - k[0]*960), int(x - k[1]*960)] = m_; a[1][959 - int(y - k[0]*960), int(x - k[1]*960)] = c_
    srs = osr.SpatialReference(); srs.ImportFromEPSG(4326)
    for (ty, tx_), (m_, c_) in tiles.items():
        path = f'{out}/10_{ty}_{tx_}.tif'; tmp = path + '.tmp.tif'; d = gdal.GetDriverByName('GTiff').Create(tmp, 960, 960, 2, gdal.GDT_Float64, ['TILED=YES'])
        d.SetGeoTransform([-180 + tx_*SPAN, CELL, 0, -96 + (ty+1)*SPAN, 0, -CELL]); d.SetProjection(srs.ExportToWkt())
        for b, v in ((1, m_), (2, np.where(c_ > 0, c_, np.nan))): d.GetRasterBand(b).WriteArray(v); d.GetRasterBand(b).SetNoDataValue(float('nan'))
        for k, v in prov.items(): d.SetMetadataItem('WORLDSTORE_' + k.upper(), str(v))
        d = None; gdal.Translate(path, tmp, format='COG', creationOptions=['COMPRESS=DEFLATE', 'PREDICTOR=3']); os.remove(tmp)
    json.dump(prov, open(f'{out}/provenance.json', 'w'), indent=1); return len(tiles)
