#!/usr/bin/env python3
"""Sidescan OBSERVATIONS — spine decision 4 applied to sidescan (process test).
Bakes only what is a pure function of the bag bytes + this decoder: per-ping sample arrays as recorded (uint16), sample
rate, sample0, samples_per_beam, beamwidth, the sonar's reported sound speed (a PLACEHOLDER 1500.0 in this driver — kept
as recorded, never used for ranging here), and the sonar's own nadir_depth measurement. NO pose, NO slant→ground
conversion, NO sound-speed ranging: those are link-time (16_sidescan_link.py).
Usage: 15_sidescan_obs.py <bag_dir> <out.zarr> --t0 <epoch> --dur <s>"""
import sys, hashlib, argparse, numpy as np, xarray as xr
from pathlib import Path
from rosbags.highlevel import AnyReader
ap = argparse.ArgumentParser(); ap.add_argument('bag'); ap.add_argument('out'); ap.add_argument('--t0', type=float, required=True); ap.add_argument('--dur', type=float, required=True); a = ap.parse_args()
bag = Path(a.bag); files = sorted(p for p in bag.iterdir() if p.suffix == '.mcap')
src_id = 'sha256:' + hashlib.sha256(b''.join(hashlib.sha256(f.read_bytes()).digest() for f in files)).hexdigest() if len(files) > 1 else 'sha256:' + hashlib.sha256(files[0].read_bytes()).hexdigest()
side = {'port': {}, 'starboard': {}}; nadir = {}; meta = {}; perping = {}
with AnyReader([bag]) as r:
    conns = [c for c in r.connections if c.topic.endswith(('sonar_image_port', 'sonar_image_starboard', 'nadir_depth'))]
    for c, ts, raw in r.messages(connections=conns):
        t = ts / 1e9
        if t < a.t0: continue
        if t > a.t0 + a.dur: break
        m = r.deserialize(raw, c.msgtype); st = m.header.stamp.sec + m.header.stamp.nanosec * 1e-9
        if c.topic.endswith('nadir_depth'): nadir[st] = float(m.range); continue
        s = 'port' if c.topic.endswith('port') else 'starboard'
        dt_ = {2: np.uint16, 0: np.uint8, 8: np.float32}[m.image.dtype]
        side[s][st] = np.frombuffer(m.image.data, dtype=dt_)[:m.samples_per_beam]
        if s == 'port': perping[st] = (float(m.sample_rate), int(m.sample0), int(m.samples_per_beam), float(m.ping_info.sound_speed))
        meta.setdefault(s, dict(sample_rate=float(m.sample_rate), sample0=int(m.sample0), n=int(m.samples_per_beam), c_reported=float(m.ping_info.sound_speed), f=float(m.ping_info.frequency), txbw=float(m.ping_info.tx_beamwidths[0]) if len(m.ping_info.tx_beamwidths) else np.nan, frame=m.header.frame_id, recv0=t))
n = meta['port']['n']; tp = np.array(sorted(side['port'])); tsb = np.array(sorted(side['starboard']))
j = np.clip(np.searchsorted(tsb, tp), 0, tsb.size - 1); j = np.where((j > 0) & (np.abs(tsb[np.maximum(j-1, 0)] - tp) < np.abs(tsb[j] - tp)), j - 1, j)
paired = np.abs(tsb[j] - tp) < 0.03                      # port and starboard stamps differ slightly: pair within 30 ms
times = list(tp[paired]); sb_times = tsb[j[paired]]
print(f"port pings {tp.size}, starboard {tsb.size}, paired {len(times)} (stamp offset p50 {np.median(np.abs(tsb[j]-tp)[paired])*1e3:.1f} ms)")
n = max(perping[t][2] for t in times)   # samples per beam varies with range scale: pad to the max, keep the true count per ping
def stack(s): return np.stack([np.pad(side[s][t][:n], (0, n - min(n, side[s][t].size))) for t in (times if s == 'port' else sb_times)]).astype(np.uint16)
nt = np.array(sorted(nadir)); nv = np.array([nadir[t] for t in nt])
ds = xr.Dataset({'port': (('ping', 'sample'), stack('port')), 'starboard': (('ping', 'sample'), stack('starboard')),
                 'sample_rate_hz': (('ping',), np.array([perping[t][0] for t in times], np.float32)), 'sample0': (('ping',), np.array([perping[t][1] for t in times], np.int32)), 'samples_per_beam': (('ping',), np.array([perping[t][2] for t in times], np.int32)), 'sound_speed_reported': (('ping',), np.array([perping[t][3] for t in times], np.float32)),
                 'nadir_depth': (('ping',), np.interp(times, nt, nv, left=np.nan, right=np.nan).astype(np.float32) if nt.size else np.full(len(times), np.nan, np.float32)),
                 'nadir_depth_age_s': (('ping',), np.array([t - nt[nt <= t].max() if (nt <= t).any() else np.nan for t in times], np.float32))},
                coords={'time': (('ping',), np.array(times)), 'sample': np.arange(n)},
                attrs={'worldstore:category': 'observations', 'worldstore:quantity': 'sidescan', 'worldstore:state': 'draft', 'worldstore:origin': 'surveyed',
                       'platform': 'bizzyboat', 'sensor': 'garmin-sidescan', 'sensor_frames': f"{meta['port']['frame']} / {meta['starboard']['frame']}",
                       'source_id': src_id, 'source_file': files[0].name, 'window_t0': a.t0, 'window_dur_s': a.dur,
                       'sample_rate_hz': meta['port']['sample_rate'], 'sample0': meta['port']['sample0'], 'frequency_hz': meta['port']['f'], 'tx_beamwidth_rad': meta['port']['txbw'],
                       'sound_speed_reported_m_s': meta['port']['c_reported'], 'sound_speed_note': 'driver placeholder as recorded; NOT used for ranging — sound speed is a link input',
                       'pose_applied': 'none (deferred to link)', 'ground_range_applied': 'none (slant->ground is link-time; nadir_depth is the sonar\'s own measurement, baked as recorded)',
                       'producer_version': 'UNKNOWN (not recorded in bag)', 'decoder': '15_sidescan_obs.py v0'})
ds.to_zarr(a.out, mode='w', zarr_version=2, encoding={'port': {'chunks': (2000, n)}, 'starboard': {'chunks': (2000, n)}})   # no dask in this venv: chunk via encoding
print(f"{Path(a.out).name}: {len(times)} pings x {n} samples/side, nadir_depth {np.nanmin(nv) if nv.size else float('nan'):.2f}–{np.nanmax(nv) if nv.size else float('nan'):.2f} m ({nt.size} readings), fs {meta['port']['sample_rate']:.0f} Hz, sample0 {meta['port']['sample0']}, c_reported {meta['port']['c_reported']}, source {src_id[:23]}…")
