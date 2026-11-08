#!/usr/bin/env python3
"""Sound-speed cast import — PROCESS TEST ONLY (Roland 2026-09-21: cast file formats are NOT settled; this reader is a
test adapter and the water/ profile schema is provisional).
A cast is a single-file source: identity = sha256 of the file (the annex key's hash), the Item's time/position/instrument
come from the .asvp header. Output under $WORLD_STORE_ROOT (default ~/data/world — the prototype sets it elsewhere):
  sources/casts/<sha256[:16]>.asvp + .json          the file as received + its source Item
  water/draft/surveyed/profiles/<id>.json           the provisional profile product: samples with QC flags
Flags: measured | suspect (spike vs neighbours) | padded (the exporter's standard deep-water extension, not measured).
Usage: 13_casts.py <cast.asvp> [...]"""
import sys, os, json, hashlib, shutil, datetime as dt, numpy as np
from pathlib import Path
ROOT = Path(os.environ.get('WORLD_STORE_ROOT', os.path.expanduser('~/data/world')))
assert 'world_proto' in str(ROOT) or os.environ.get('WORLD_STORE_ALLOW_LIVE'), f"refusing to write the live store {ROOT} from a prototype (set WORLD_STORE_ROOT)"
def read_asvp(p):
    lines = Path(p).read_text().splitlines(); h = lines[0].strip('( )').split()
    # ( SoundVelocity 1.0 0 YYYYMMDDHHMMSS lat lon -1 YYYYMMDDHHMM 0 Instrument P n )
    t = dt.datetime.strptime(h[3], '%Y%m%d%H%M%S').replace(tzinfo=dt.timezone.utc); lat, lon = float(h[4]), float(h[5])
    inst = ' '.join(h[9:-2]) if len(h) > 11 else 'unknown'
    d, c = np.array([[float(x) for x in l.split()[:2]] for l in lines[1:] if l.strip()]).T
    return t, lat, lon, inst, d, c
def qc(d, c):
    flag = np.full(d.size, 'measured', dtype=object)
    step = np.diff(d); med = np.median(step[:max(3, step.size//2)])
    pad_start = next((i+1 for i, s in enumerate(step) if s > max(1.0, 5*med)), d.size)
    # the exporter writes its first padding value AT the cast's bottom depth too: walk back over samples equal to it
    while pad_start > 1 and pad_start < d.size and abs(c[pad_start-1] - c[pad_start]) < 0.05: pad_start -= 1
    flag[pad_start:] = 'padded'
    m = flag == 'measured'; cm = c[m]
    if cm.size >= 3:
        nb = np.copy(cm); nb[1:-1] = (cm[:-2] + cm[2:]) / 2
        spike = np.abs(cm - nb) > 3.0; idx = np.where(m)[0][spike]; flag[idx] = 'suspect'
    return flag, pad_start
for p in sys.argv[1:]:
    t, lat, lon, inst, d, c = read_asvp(p); key = hashlib.sha256(Path(p).read_bytes()).hexdigest(); sid = key[:16]
    src = ROOT / 'sources' / 'casts'; src.mkdir(parents=True, exist_ok=True); shutil.copy2(p, src / f'{sid}.asvp')
    item_src = {'type': 'Feature', 'stac_version': '1.0.0', 'id': f'cast-{sid}', 'geometry': {'type': 'Point', 'coordinates': [lon, lat]},
                'properties': {'datetime': t.isoformat(), 'worldstore:category': 'sources', 'worldstore:kind': 'sound_speed_cast', 'worldstore:origin': 'surveyed',
                               'worldstore:source_id': f'sha256:{key}', 'instrument': inst, 'file:format': 'asvp (exporter format — provisional, not the design)',
                               'received_as': str(p)}, 'assets': {'file': {'href': f'{sid}.asvp'}}}
    json.dump(item_src, open(src / f'{sid}.json', 'w'), indent=1)
    flag, pad_start = qc(d, c)
    prof = ROOT / 'water' / 'draft' / 'surveyed' / 'profiles'; prof.mkdir(parents=True, exist_ok=True)
    item = {'type': 'Feature', 'stac_version': '1.0.0', 'id': f'ssp-{sid}', 'geometry': item_src['geometry'],
            'properties': {'datetime': t.isoformat(), 'worldstore:category': 'water', 'worldstore:quantity': 'sound_speed_profile', 'worldstore:state': 'draft',
                           'worldstore:origin': 'surveyed', 'worldstore:inputs': [item_src['id']], 'worldstore:fingerprint': hashlib.sha256((key + 'qc-v0').encode()).hexdigest(),
                           'measured_depth_max_m': float(d[pad_start-1]), 'n_measured': int((flag == 'measured').sum()), 'n_suspect': int((flag == 'suspect').sum()), 'n_padded': int((flag == 'padded').sum()),
                           'schema': 'PROVISIONAL water/sound_speed_profile v0'},
            'samples': [{'depth_m': float(a), 'sound_speed_m_s': float(b), 'flag': str(f)} for a, b, f in zip(d, c, flag)]}
    json.dump(item, open(prof / f'{item["id"]}.json', 'w'), indent=1)
    print(f"{Path(p).name}: {t:%Y-%m-%d %H:%M}Z ({lat:.4f},{lon:.4f}) {inst}: measured to {d[pad_start-1]:.2f} m, {item['properties']['n_measured']} measured, {item['properties']['n_suspect']} suspect, {item['properties']['n_padded']} padded -> {item['id']}")
