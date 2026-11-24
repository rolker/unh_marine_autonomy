#!/usr/bin/env python3
"""Shoreline prototype (spine decision 5.3), Massabesic. Three boundaries, kept distinct:
  A. IMPORTED coastline: USGS NHD waterbody polygon (no chart exists for the lake)  → features/published/imported/shoreline
  B. DERIVED navigable boundary: a depth contour of the depths store at the water level derived from the trajectory
     (RTK height at base_link; same frame/bias as the tiles)                         → features/draft/surveyed/navigable_boundary
  C. The MEASUREMENT: distance from the mapped shore to the edge of surveyed coverage — the band where "unsurveyed is
     lethal" stands in for a shoreline today.
Vectors are the record (5.2); rasters here are scratch. Container GeoPackage (PROVISIONAL). Usage: 19_shoreline.py [--depth 2.5] [--level 48.80]"""
import os, sys, json, glob, hashlib, argparse, datetime as dt, numpy as np
from pathlib import Path
from osgeo import gdal, ogr, osr
from scipy import ndimage
gdal.UseExceptions(); ap = argparse.ArgumentParser(); ap.add_argument('--depth', type=float, default=2.5, help='contour depth below the water surface, m (data supports >= ~2 m here)'); ap.add_argument('--level', type=float, default=48.80, help='water-surface ellipsoidal height, m (fcu trajectory median; same +0.626 m EGM96 bias as the tiles, so depths cancel it)'); a = ap.parse_args()
ROOT = Path(os.environ.get('WORLD_STORE_ROOT', os.path.expanduser('~/data/world'))); assert 'world_proto' in str(ROOT) or os.environ.get('WORLD_STORE_ALLOW_LIVE')
def _in_lake(t):   # the store holds Massabesic, Shoals and Lewes: keep the tiles whose extent touches the lake bbox
    g = gdal.Open(t).GetGeoTransform(); x0, y1 = g[0], g[3]; x1, y0 = x0 + 960 * g[1], y1 + 960 * g[5]
    return x1 > -71.42 and x0 < -71.32 and y1 > 42.94 and y0 < 43.03
TILES = [t for t in sorted(glob.glob(os.path.expanduser('~/data/world/depths/processed/10_*.tif'))) if _in_lake(t)]   # read-only: the live store's native tiles
srs = osr.SpatialReference(); srs.ImportFromEPSG(4326)
def gpkg(path, layer, geom_type, fields, feats, meta):
    if path.exists(): path.unlink()
    ds = ogr.GetDriverByName('GPKG').CreateDataSource(str(path)); ly = ds.CreateLayer(layer, srs, geom_type)
    for n, t in fields: ly.CreateField(ogr.FieldDefn(n, t))
    for g, props in feats:
        f = ogr.Feature(ly.GetLayerDefn()); f.SetGeometry(g); [f.SetField(k, v) for k, v in props.items()]; ly.CreateFeature(f)
    ds = None; json.dump(meta, open(path.with_suffix('.json'), 'w'), indent=1)
# ---- A. imported coastline (NHD) ----
src = ROOT / 'sources' / 'shoreline' / 'nhd_massabesic_lake.geojson'; key = hashlib.sha256(src.read_bytes()).hexdigest()
nhd = json.load(open(src))['features'][0]; poly = ogr.CreateGeometryFromJson(json.dumps(nhd['geometry']))
outA = ROOT / 'features' / 'published' / 'imported' / 'shoreline'; outA.mkdir(parents=True, exist_ok=True)
gpkg(outA / 'nhd_massabesic_lake.gpkg', 'shoreline', ogr.wkbPolygon, [('name', ogr.OFTString), ('source', ogr.OFTString), ('water_level_basis', ogr.OFTString), ('horizontal_datum', ogr.OFTString)],
     [(poly, {'name': 'Massabesic Lake', 'source': 'USGS NHD waterbody', 'water_level_basis': 'NHD: as mapped (nominal/ordinary level; not stated per feature)', 'horizontal_datum': 'NAD83 (NHD); served as EPSG:4326'})],
     {'worldstore': {'category': 'features', 'quantity': 'shoreline', 'state': 'published', 'origin': 'imported', 'source_id': f'sha256:{key}', 'fingerprint': hashlib.sha256((key + 'nhd-v0').encode()).hexdigest(), 'container': 'PROVISIONAL GeoPackage', 'rings': len(nhd['geometry']['coordinates']), 'note': 'the coastline is a FALLBACK for the costmap where no bathymetry exists; the real product is B'}})
# ---- mosaic the native tiles: depth = level - bed height ----
vrt = gdal.BuildVRT('/vsimem/depths.vrt', TILES); gt = vrt.GetGeoTransform(); W, H = vrt.RasterXSize, vrt.RasterYSize
h = vrt.GetRasterBand(1).ReadAsArray().astype(np.float32); u = vrt.GetRasterBand(2).ReadAsArray().astype(np.float32)
ok = np.isfinite(h) & (np.abs(h - 45) <= 30) & np.isfinite(u) & (u <= 10); depth = np.where(ok, a.level - h, np.nan).astype(np.float32)
cell_m = abs(gt[5]) * 111_320; print(f"mosaic {W}x{H} cells of ~{cell_m:.2f} m; surveyed cells {ok.sum():,}; water level {a.level} m; shallowest surveyed depth p1 {np.nanpercentile(depth,1):.2f} m")
# ---- B. derived navigable boundary: contour at --depth ----
tmp = str(ROOT / 'scratch_depth.tif'); d = gdal.GetDriverByName('GTiff').Create(tmp, W, H, 1, gdal.GDT_Float32); d.SetGeoTransform(gt); d.SetProjection(srs.ExportToWkt()); b = d.GetRasterBand(1); b.WriteArray(np.nan_to_num(depth, nan=-9999)); b.SetNoDataValue(-9999); d = None
mem = ogr.GetDriverByName('Memory').CreateDataSource(''); cl = mem.CreateLayer('c', srs, ogr.wkbLineString); cl.CreateField(ogr.FieldDefn('depth', ogr.OFTReal))
d2 = gdal.Open(tmp); gdal.ContourGenerate(d2.GetRasterBand(1), 0, 0, [a.depth], 1, -9999, cl, -1, 0); d2 = None; os.remove(tmp)
lines = [(f.GetGeometryRef().Clone(), {'depth_m': a.depth, 'water_level_m': a.level, 'basis': 'depths/processed native tiles, contour at --depth below the trajectory-derived water level'}) for f in cl]
tile_keys = hashlib.sha256('\n'.join(f'{Path(t).name}\t{Path(t).stat().st_size}' for t in TILES).encode()).hexdigest()
outB = ROOT / 'features' / 'draft' / 'surveyed' / 'navigable_boundary'; outB.mkdir(parents=True, exist_ok=True)
gpkg(outB / f'massabesic_contour_{a.depth:.1f}m.gpkg', 'navigable_boundary', ogr.wkbLineString, [('depth_m', ogr.OFTReal), ('water_level_m', ogr.OFTReal), ('basis', ogr.OFTString)], lines,
     {'worldstore': {'category': 'features', 'quantity': 'navigable_boundary', 'state': 'draft', 'origin': 'surveyed', 'inputs': {'depth_tiles': tile_keys[:16], 'water_level_m': a.level, 'water_level_source': 'trajectories/fcu 2026-06-22 window median altitude at base_link (ASSUMED at the waterline)', 'contour_depth_m': a.depth}, 'fingerprint': hashlib.sha256(f'{tile_keys}|{a.level}|{a.depth}|contour-v0'.encode()).hexdigest(), 'container': 'PROVISIONAL GeoPackage', 'n_lines': len(lines), 'total_length_km': sum(l[0].Length() for l in lines) * 111.32 * np.cos(np.radians(43)), 'note': 'rasterise ON DEMAND for a costmap (5.2); the contour is only meaningful where coverage exists'}})
# ---- C. the measurement: shore -> surveyed coverage distance ----
# rasterise the NHD polygon on the mosaic grid; distance transform from surveyed cells; sample the shore ring every ~5 m
mask_ds = gdal.GetDriverByName('MEM').Create('', W, H, 1, gdal.GDT_Byte); mask_ds.SetGeoTransform(gt); mask_ds.SetProjection(srs.ExportToWkt())
pm = ogr.GetDriverByName('Memory').CreateDataSource(''); pl = pm.CreateLayer('p', srs, ogr.wkbPolygon); pf = ogr.Feature(pl.GetLayerDefn()); pf.SetGeometry(poly); pl.CreateFeature(pf)
gdal.RasterizeLayer(mask_ds, [1], pl, burn_values=[1]); lake = mask_ds.ReadAsArray().astype(bool)
dist_cells = ndimage.distance_transform_edt(~ok); dist_m = dist_cells * cell_m                     # distance from every cell to the nearest surveyed cell
ring = poly.GetGeometryRef(0); pts = []; L = ring.Length(); step = 5.0 / (111_320 * np.cos(np.radians(43)))
for s in np.arange(0, L, step): p = ring.Value(s); pts.append((p.GetX(), p.GetY()))
px = ((np.array([p[0] for p in pts]) - gt[0]) / gt[1]).astype(int); py = ((np.array([p[1] for p in pts]) - gt[3]) / gt[5]).astype(int); inside = (px >= 0) & (px < W) & (py >= 0) & (py < H)
sd = dist_m[py[inside], px[inside]]
print(f"\nMEASUREMENT — outer NHD shoreline, {inside.sum():,} samples at 5 m ({inside.mean()*100:.0f}% inside the tiled area):")
print(f"  distance from shore to nearest surveyed cell: p10 {np.percentile(sd,10):.0f}  p50 {np.percentile(sd,50):.0f}  p90 {np.percentile(sd,90):.0f} m;  within 20 m: {np.mean(sd<=20)*100:.1f}%  within 50 m: {np.mean(sd<=50)*100:.1f}%  beyond 100 m: {np.mean(sd>100)*100:.1f}%")
shore_d = ndimage.distance_transform_edt(lake) * cell_m                                            # distance from lake cells to the shore (edge of the polygon)
for band in (20, 50, 100):
    z = lake & (shore_d <= band); print(f"  lake area within {band:3d} m of shore: {z.sum()*cell_m**2/1e4:7.1f} ha, unsurveyed {np.mean(~ok[z])*100:5.1f}%")
z = lake; print(f"  whole lake (tiled part): {z.sum()*cell_m**2/1e4:7.1f} ha, unsurveyed {np.mean(~ok[z])*100:5.1f}%")
print(f"\nB: {len(lines)} contour lines at {a.depth} m; written under {outB}")
