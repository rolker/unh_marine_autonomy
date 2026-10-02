#!/usr/bin/env python3
"""Component 9 (catalog-aware): SBET-shaped trajectory for one nav source; applies catalog Items owned by 'trajectory'.
Usage: 09_trajectory.py <main_bag> <t0_epoch> <dur_s> <out.parquet> --source fcu [--catalog items.json] [--gap-from s --gap-to s]"""
import sys, math, json, argparse, numpy as np, pyarrow as pa, pyarrow.parquet as pq
from pathlib import Path
from rosbags.highlevel import AnyReader
sys.path.insert(0, str(Path(__file__).parent)); import corrections_lib as cl
ap = argparse.ArgumentParser(); ap.add_argument('bag'); ap.add_argument('t0', type=float); ap.add_argument('dur', type=float); ap.add_argument('out')
ap.add_argument('--source', default='fcu'); ap.add_argument('--catalog'); ap.add_argument('--gap-from', type=float); ap.add_argument('--gap-to', type=float); a = ap.parse_args()
assert a.source == 'fcu', 'prototype: only the FCU source is declared well enough to relink (SBG attitude convention undeclared)'
def rpy(q):
    x, y, z, w = q; r = math.atan2(2*(w*x+y*z), 1-2*(x*x+y*y)); p = math.asin(max(-1, min(1, 2*(w*y-z*x)))); yv = math.atan2(2*(w*z+x*y), 1-2*(y*y+z*z)); return r, p, (math.pi/2-yv) % (2*math.pi)
TOP = {'pos': '/bizzy/mavros/global_position/global', 'att': '/bizzy/mavros/imu/data', 'raw': '/bizzy/mavros/global_position/raw/fix'}
pos, att, raw_ = [], [], []
with AnyReader([Path(a.bag)]) as r:
    for c, ts, raw in r.messages(connections=[c for c in r.connections if c.topic in TOP.values()]):
        t = ts / 1e9
        if t < a.t0 - 5: continue
        if t > a.t0 + a.dur + 5: break
        m = r.deserialize(raw, c.msgtype); st = m.header.stamp.sec + m.header.stamp.nanosec * 1e-9
        if c.topic == TOP['pos']: pos.append((st, m.latitude, m.longitude, m.altitude))
        elif c.topic == TOP['raw']: raw_.append((st, m.altitude))
        else: q = m.orientation; att.append((st,) + rpy((q.x, q.y, q.z, q.w)))
P_ = np.array(sorted(pos)); A = np.array(sorted(att)); R = np.array(sorted(raw_)); t = P_[:, 0]; alt = P_[:, 3].copy(); applied = []
for it in cl.applicable(cl.load(a.catalog), 'trajectory', t[0], t[-1], platform='bizzyboat', source='fcu'):
    p = it['properties']['worldstore:parameters']
    if it['properties']['worldstore:kind'] == 'vertical_reference' and p['method'] == 'derived_from_raw_fix' and len(R):
        corr = float(np.median(np.interp(t, R[:, 0], R[:, 1]) - alt)); alt += corr
        applied.append({'id': it['id'], 'derived_correction_m': round(corr, 3), 'fallback_m': p['fallback_m']})
roll = np.interp(t, A[:, 0], A[:, 1]); pitch = np.interp(t, A[:, 0], A[:, 2]); hd = np.interp(t, A[:, 0], np.unwrap(A[:, 3])) % (2*math.pi)
keep = np.ones(len(t), bool)
if a.gap_from is not None: keep &= ~((t - t[0] >= a.gap_from) & (t - t[0] <= a.gap_to))
tbl = pa.table({'time': t[keep], 'latitude': np.radians(P_[keep, 1]), 'longitude': np.radians(P_[keep, 2]), 'altitude': alt[keep], 'roll': roll[keep], 'pitch': pitch[keep], 'heading': hd[keep]})
meta = {'worldstore:category': 'trajectories', 'platform': 'bizzyboat', 'source': 'fcu', 'reference_point': 'bizzy/base_link', 'attitude_convention': 'ENU / FLU (sensor_msgs/Imu from mavros)',
        'frame': 'RTK via MaCORS (NAD83(2011) epoch 2010.0 labelled WGS84); store frame ITRF2020@2020.0 NOT yet applied', 'height': 'ellipsoidal', 'corrections_applied': json.dumps(applied), 'gap_rule': 'no interpolation across a gap > 2 s'}
pq.write_table(tbl.replace_schema_metadata({k.encode(): v.encode() for k, v in meta.items()}), a.out)
print(f'{Path(a.out).name}: {keep.sum()} poses {1/np.median(np.diff(t)):.0f} Hz, corrections {applied or "none"}')
