#!/usr/bin/env python3
"""First FEATURES — human-marked contacts imported as vector records (spine decision 5.2: vectors are the record; rasterise
only on demand). Test adapters for two human sources: a HYPACK targets.db and a QINSy target sheet. Both are single-file
sources (sha256 identity) → features/reviewed/surveyed/contacts/<id>.geojson (reviewed = a person marked them).
Container is PROVISIONAL (GeoJSON here; GeoPackage/GeoParquet are the candidates). Usage: 17_contacts.py"""
import os, json, hashlib, sqlite3, shutil, datetime as dt
from pathlib import Path
ROOT = Path(os.environ.get('WORLD_STORE_ROOT', os.path.expanduser('~/data/world'))); assert 'world_proto' in str(ROOT) or os.environ.get('WORLD_STORE_ALLOW_LIVE')
SRC = ROOT / 'sources' / 'contacts'; OUT = ROOT / 'features' / 'reviewed' / 'surveyed' / 'contacts'; SRC.mkdir(parents=True, exist_ok=True); OUT.mkdir(parents=True, exist_ok=True)
def source(p, kind):
    key = hashlib.sha256(Path(p).read_bytes()).hexdigest(); sid = key[:16]; shutil.copy2(p, SRC / f'{sid}{Path(p).suffix}')
    json.dump({'type': 'Feature', 'stac_version': '1.0.0', 'id': f'contacts-src-{sid}', 'geometry': None, 'properties': {'worldstore:category': 'sources', 'worldstore:kind': kind, 'worldstore:origin': 'surveyed', 'worldstore:source_id': f'sha256:{key}', 'received_as': str(p)}}, open(SRC / f'{sid}.json', 'w'), indent=1)
    return sid, key
def write(fid, feats, src_sid, key, extra):
    fc = {'type': 'FeatureCollection', 'id': fid, 'features': feats,
          'worldstore': {'category': 'features', 'quantity': 'contacts', 'state': 'reviewed', 'origin': 'surveyed', 'inputs': [f'contacts-src-{src_sid}'], 'fingerprint': hashlib.sha256((key + 'contacts-v0').encode()).hexdigest(), 'container': 'PROVISIONAL GeoJSON', **extra}}
    json.dump(fc, open(OUT / f'{fid}.geojson', 'w'), indent=1); lats = [f['geometry']['coordinates'][1] for f in feats]; lons = [f['geometry']['coordinates'][0] for f in feats]
    print(f"{fid}: {len(feats)} contacts, lat {min(lats):.4f}–{max(lats):.4f} lon {min(lons):.4f}–{max(lons):.4f}")
# HYPACK targets.db (Massabesic turret search, Aug 2026)
db = '/mnt/nadata/map2026asv/projects/massabesic/massabesic_turret_search/Massabesic Turret Search/targets.db'; sid, key = source(db, 'hypack_targets_db')
con = sqlite3.connect(db); groups = dict(con.execute('select TGTID, GROUP_NAME from GROUP_TGTS join GROUPS on GROUPS.ID = GROUPID'))
attrs = {}
for tid, name, val in con.execute('select TGTID, ATTRIB_NAME, VALUE from ATTRIB_DATA join ATTRIBS on ATTRIBS.ATTRIBID = ATTRIB_DATA.ATTRIBID') if 'VALUE' in [c[1] for c in con.execute('pragma table_info(ATTRIB_DATA)')] else []: attrs.setdefault(tid, {})[name] = val
feats = []
for r in con.execute('select ID, NAME, DATETIME_ACQUIRED, WGS84LATITUDE, WGS84LONGITUDE, DEPTH, SOURCE_PROGRAM, SOURCE_LINE, NOTES, SYMBOL, DELETED from GENERAL'):
    ID, NAME, T, lat, lon, depth, prog, line, notes, sym, deleted = r
    feats.append({'type': 'Feature', 'id': f'hypack-{ID}', 'geometry': {'type': 'Point', 'coordinates': [float(lon), float(lat)]},
                  'properties': {'name': NAME, 'datetime': (dt.datetime.fromisoformat(T).replace(tzinfo=dt.timezone.utc).isoformat() if T else None), 'depth_m': depth or None, 'marked_by': 'person (HYPACK ' + (prog or '?') + ')', 'survey_line': line, 'notes': (notes or '').strip() or None, 'symbol': sym, 'group': groups.get(ID), 'deleted': bool(deleted), 'attributes': attrs.get(ID, {}), 'sensor_context': 'magnetometer turret search (Aug 2026); not sidescan'}})
write('contacts-hypack-massabesic-2026-08', feats, sid, key, {'note': 'positions as marked in HYPACK (WGS84 as labelled — see the RTK datum finding); no geometry/datum revision applied'})
# QINSy target sheet (salmon share)
import openpyxl; x = '/mnt/nadata/map2026asv/salmon/share/qinsy_target_coordinates.xlsx'; sid2, key2 = source(x, 'qinsy_target_sheet')
ws = openpyxl.load_workbook(x, read_only=True).worksheets[0]; feats = []
for i, row in enumerate(ws.iter_rows(values_only=True)):
    if i == 0 or not row or not row[1]: continue
    lat, lon = [float(v) for v in str(row[1]).split(',')[:2]]
    feats.append({'type': 'Feature', 'id': f'qinsy-{i}', 'geometry': {'type': 'Point', 'coordinates': [lon, lat]}, 'properties': {'description': row[0], 'marked_by': 'person (QINSy)', 'datetime': None, 'sensor_context': 'unknown from the sheet (QINSy; likely M3/sidescan targets)'}})
write('contacts-qinsy-massabesic-2026', feats, sid2, key2, {'note': 'sheet carries description + lat,lon only: no time, no line, no datum statement'})
