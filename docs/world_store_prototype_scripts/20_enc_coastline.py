#!/usr/bin/env python3
"""Imported coastline for the Isles of Shoals from the ENC corpus already in sources (charts/ENC_ROOT): COALNE lines and
LNDARE polygons from US4NH1BD (1:80k; the 1:20k US5NH1AG stops at 42.975 N, just south of Appledore). Vector record
(5.2), state published / origin imported. Usage: 20_enc_coastline.py"""
import os, json, hashlib
from pathlib import Path
from osgeo import ogr, osr, gdal
gdal.UseExceptions(); ROOT = Path(os.environ.get('WORLD_STORE_ROOT', os.path.expanduser('~/data/world'))); assert 'world_proto' in str(ROOT) or os.environ.get('WORLD_STORE_ALLOW_LIVE')
cell = Path(os.path.expanduser('~/data/world/charts/ENC_ROOT/US4NH1BD/US4NH1BD.000')); key = hashlib.sha256(cell.read_bytes()).hexdigest()
bbox = ogr.CreateGeometryFromWkt('POLYGON((-70.68 42.95,-70.55 42.95,-70.55 43.03,-70.68 43.03,-70.68 42.95))')
ds = ogr.Open(str(cell)); srs = osr.SpatialReference(); srs.ImportFromEPSG(4326)
dsid = ds.GetLayerByName('DSID'); f = dsid.GetNextFeature(); meta = {k: f.GetField(k) for k in ('DSID_DSNM', 'DSID_EDTN', 'DSID_UPDN', 'DSID_ISDT', 'DSPM_HDAT', 'DSPM_VDAT', 'DSPM_SDAT', 'DSPM_CSCL') if f.GetFieldIndex(k) >= 0}
out = ROOT / 'features' / 'published' / 'imported' / 'shoreline'; out.mkdir(parents=True, exist_ok=True); path = out / 'enc_shoals_US4NH1BD.gpkg'
if path.exists(): path.unlink()
g = ogr.GetDriverByName('GPKG').CreateDataSource(str(path)); counts = {}
for name, gtype in (('COALNE', ogr.wkbMultiLineString), ('LNDARE', ogr.wkbMultiPolygon)):
    src = ds.GetLayerByName(name); ly = g.CreateLayer(name.lower(), srs, gtype); ly.CreateField(ogr.FieldDefn('catcoa', ogr.OFTString)); ly.CreateField(ogr.FieldDefn('objnam', ogr.OFTString)); n = 0
    for feat in src:
        geom = feat.GetGeometryRef()
        if geom is None or not geom.Intersects(bbox): continue
        o = ogr.Feature(ly.GetLayerDefn()); o.SetGeometry(ogr.ForceTo(geom.Clone(), gtype)); o.SetField('catcoa', str(feat.GetField('CATCOA')) if feat.GetFieldIndex('CATCOA') >= 0 else None); o.SetField('objnam', feat.GetField('OBJNAM') if feat.GetFieldIndex('OBJNAM') >= 0 else None); ly.CreateFeature(o); n += 1
    counts[name] = n
g = None
json.dump({'worldstore': {'category': 'features', 'quantity': 'shoreline', 'state': 'published', 'origin': 'imported', 'source_id': f'sha256:{key}', 'source_file': str(cell), 'fingerprint': hashlib.sha256((key + 'enc-coastline-v0').encode()).hexdigest(), 'container': 'PROVISIONAL GeoPackage', 'cell': meta, 'counts': counts,
           'water_level_basis': 'S-57 COALNE = the coastline at the sounding/chart datum convention of the cell (DSPM_VDAT/SDAT above); LNDARE = land above it', 'horizontal_datum': 'WGS84 per DSPM_HDAT', 'note': 'coastline is a FALLBACK for the costmap; no navigable-boundary contour at Shoals yet (no shallow coverage)'}}, open(path.with_suffix('.json'), 'w'), indent=1)
print(f"{path.name}: {counts}; cell {meta}")
