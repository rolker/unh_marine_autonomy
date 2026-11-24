#!/usr/bin/env python3
"""Extract an absolute-time window of M3 detections (+ /tf_static) from an archived sonar bag into two local bags:
<out>/truth   : unaltered (the archive holds retrofitted = correct bags)
<out>/faulted : the recorded faults re-injected: +6 s on header stamps before the step time, tf_static M3 back to the install-log value
Usage: 07_extract.py <sonar_bag> <t0_epoch> <dur_s> <out_dir> <step_fraction>"""
import sys, struct, shutil
from pathlib import Path
from rosbags.highlevel import AnyReader
from rosbags.rosbag2 import Writer
bag, t0, dur, out, frac = Path(sys.argv[1]), float(sys.argv[2]), float(sys.argv[3]), Path(sys.argv[4]), float(sys.argv[5])
t1 = t0 + dur; step = t0 + frac * dur
for d in ('truth', 'faulted'): shutil.rmtree(out / d, ignore_errors=True)
out.mkdir(parents=True, exist_ok=True)
with AnyReader([bag]) as r, Writer(out / 'truth', version=9) as wt, Writer(out / 'faulted', version=9) as wf:
    conns = [c for c in r.connections if c.topic in ('/bizzy/sensors/m3/detections', '/tf_static')]
    cw = {c.id: (wt.add_connection(c.topic, c.msgtype, typestore=r.typestore, offered_qos_profiles=c.ext.offered_qos_profiles),
                 wf.add_connection(c.topic, c.msgtype, typestore=r.typestore, offered_qos_profiles=c.ext.offered_qos_profiles)) for c in conns}
    n = nf = 0
    for c, ts, raw in r.messages(connections=conns):
        if c.topic == '/tf_static':
            wt.write(cw[c.id][0], ts, raw); m = r.deserialize(raw, c.msgtype)
            for tr in m.transforms:
                if tr.child_frame_id == 'bizzy/m3': tr.transform.translation.x, tr.transform.translation.z = -0.23, -0.145
            wf.write(cw[c.id][1], ts, r.typestore.serialize_cdr(m, c.msgtype)); continue
        t = ts / 1e9
        if t < t0: continue
        if t > t1: break
        assert raw[1] == 1, 'expected little-endian CDR'
        wt.write(cw[c.id][0], ts, raw); n += 1
        if t < step:
            b = bytearray(raw); sec, = struct.unpack_from('<i', b, 4); struct.pack_into('<i', b, 4, sec + 6); raw = bytes(b); nf += 1
        wf.write(cw[c.id][1], ts, raw)
print(f'pings {n}, faulted (+6 s) {nf} before step at {step:.0f}, tf_static copied to both (faulted: install-log M3 value)')
