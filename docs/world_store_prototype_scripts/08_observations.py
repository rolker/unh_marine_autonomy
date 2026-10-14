#!/usr/bin/env python3
"""Component 8 (catalog-aware): Kluster-shaped observations. Keeps the receive time, records the sensor mounting found in the
bag's tf_static, and applies the corrections catalog Items owned by 'observations'.
Usage: 08_observations.py <sonar_bag_dir> <out.zarr> [--catalog items.json] [--t0 epoch --dur s]"""
import sys, hashlib, json, argparse, numpy as np, pandas as pd, xarray as xr
from pathlib import Path
from rosbags.highlevel import AnyReader
sys.path.insert(0, str(Path(__file__).parent)); import corrections_lib as cl
ap = argparse.ArgumentParser(); ap.add_argument('bag'); ap.add_argument('out'); ap.add_argument('--catalog'); ap.add_argument('--t0', type=float, default=0); ap.add_argument('--dur', type=float, default=1e12)
a = ap.parse_args(); bag = Path(a.bag)
h = hashlib.sha256()
for f in sorted(p for p in bag.iterdir() if p.suffix in ('.mcap', '.db3')):
    with open(f, 'rb') as fh:
        for b in iter(lambda: fh.read(1 << 24), b''): h.update(b)
source_id = 'sha256:' + h.hexdigest()          # PROVISIONAL source identity (spine decision 2 pending)
seen=set(); dups=0
stamps=[]; recv=[]; twtt=[]; rx=[]; tx=[]; inten=[]; flag=[]; ss=[]; frame=None; nb=0; mount=None
with AnyReader([bag]) as r:
    for c, ts, raw in r.messages(connections=[c for c in r.connections if c.topic == '/tf_static']):
        for tr in r.deserialize(raw, c.msgtype).transforms:
            if tr.child_frame_id == 'bizzy/m3': t_ = tr.transform.translation; mount = [t_.x, t_.y, t_.z]
    c = [c for c in r.connections if c.topic.endswith('/m3/detections')]
    for _, ts, raw in r.messages(connections=c):
        if ts/1e9 < a.t0: continue
        if ts/1e9 > a.t0 + a.dur: break
        k_ = (ts, raw[4:12])                                  # exact duplicate = same receive time + same recorded stamp
        if k_ in seen: dups += 1; continue
        seen.add(k_)
        m = r.deserialize(raw, c[0].msgtype); frame = frame or m.header.frame_id; nb = max(nb, len(m.two_way_travel_times))
        stamps.append(m.header.stamp.sec * 1_000_000_000 + m.header.stamp.nanosec); recv.append(ts)
        twtt.append(np.asarray(m.two_way_travel_times, np.float32)); rx.append(np.asarray(m.rx_angles, np.float32)); tx.append(np.asarray(m.tx_angles, np.float32))
        inten.append(np.asarray(m.intensities, np.float32)); flag.append(np.asarray([f.flag for f in m.flags], np.uint8)); ss.append(m.ping_info.sound_speed)
stamps = np.asarray(stamps, np.int64); recv = np.asarray(recv, np.int64); applied = []
for it in cl.applicable(cl.load(a.catalog), 'observations', recv[0]/1e9, recv[-1]/1e9, platform='bizzyboat', topic='/bizzy/sensors/m3/detections'):
    p = it['properties']['worldstore:parameters']
    if it['properties']['worldstore:kind'] == 'clock_skew' and p['method'] == 'windowed_upper_envelope_integer_offset':
        measured = np.round((stamps - recv) / 1e9).astype(np.int64)
        skew = pd.Series(measured).rolling(2 * p['half_window_pings'] + 1, center=True, min_periods=1).max().astype(np.int64).to_numpy()
        stamps = stamps - skew * 1_000_000_000
        u, cnt = np.unique(skew, return_counts=True); applied.append({'id': it['id'], 'applied_integer_s': dict(zip(map(int, u), map(int, cnt)))})
def pad(rows, fill):
    out = np.full((len(rows), nb), fill, rows[0].dtype)
    for i, r_ in enumerate(rows): out[i, :len(r_)] = r_
    return out
ds = xr.Dataset({'two_way_travel_time': (('ping', 'beam'), pad(twtt, np.nan)), 'rx_angle': (('ping', 'beam'), pad(rx, np.nan)), 'tx_angle': (('ping', 'beam'), pad(tx, np.nan)),
                 'intensity': (('ping', 'beam'), pad(inten, np.nan)), 'detection_flag': (('ping', 'beam'), pad(flag, 255)),
                 'beam_count': (('ping',), np.asarray([len(r_) for r_ in twtt], np.int16)), 'sound_speed_at_transducer': (('ping',), np.asarray(ss, np.float32)),
                 'receive_time': (('ping',), recv), 'status': (('ping', 'beam'), np.zeros((len(stamps), nb), np.uint8))},
                coords={'time': (('ping',), stamps / 1e9), 'beam': np.arange(nb)},
                attrs={'worldstore:category': 'observations', 'source_id': source_id, 'sensor_frame': frame, 'platform': 'bizzyboat',
                       'recorded_mounting_m': json.dumps(mount), 'duplicates_dropped': int(dups), 'corrections_applied': json.dumps(applied),
                       'producer_version': 'UNKNOWN (not recorded in bag)', 'pose_applied': 'none (deferred to link)'})
ds.to_zarr(a.out, mode='w', zarr_version=2)
print(f'{Path(a.out).name}: pings {len(stamps)} ({dups} exact duplicates dropped), recorded mounting {mount}, corrections {applied or "none"}')
