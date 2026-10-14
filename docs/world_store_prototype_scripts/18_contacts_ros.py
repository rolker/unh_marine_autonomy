#!/usr/bin/env python3
"""Operator-marked sidescan contacts (marine_interfaces/Contact from the rqt sonar-waterfall plugin) → features.
The operator bag is the SOURCE (content identity); each Contact message becomes one feature whose properties are the
message's own fields, mapped 1:1 (no re-interpretation), so the store and the message agree by construction.
Message → store mapping (state/origin axes): origin_kind HUMAN|AUTO → marked_by; status PROPOSED|CONFIRMED|REJECTED →
review status (the store state is `reviewed` when a person marked or confirmed it; `draft` for AUTO+PROPOSED).
Usage: 18_contacts_ros.py <operator_bag_dir> <topic>"""
import sys, os, json, hashlib, numpy as np, datetime as dt
from pathlib import Path
from rosbags.highlevel import AnyReader
from rosbags.typesys import Stores, get_typestore, get_types_from_msg
ROOT = Path(os.environ.get('WORLD_STORE_ROOT', os.path.expanduser('~/data/world'))); assert 'world_proto' in str(ROOT) or os.environ.get('WORLD_STORE_ALLOW_LIVE')
bag = Path(sys.argv[1]); topic = sys.argv[2]
ts = get_typestore(Stores.ROS2_JAZZY); add = {}
for f in Path('/home/roland/project11/layers/main/core_ws/src/unh_marine_autonomy/marine_interfaces/msg').glob('*.msg'):
    try: add.update(get_types_from_msg(f.read_text(), f'marine_interfaces/msg/{f.stem}'))
    except Exception: pass
ts.register(add)
files = sorted(bag.glob('*.mcap')); key = hashlib.sha256('\n'.join(f'{f.name}\t{hashlib.sha256(f.read_bytes()).hexdigest()}' for f in files).encode()).hexdigest()   # decision 1 bag id
ORIGIN = {0: 'auto', 1: 'human'}; STATUS = {0: 'proposed', 1: 'confirmed', 2: 'rejected'}; SHAPE = {0: 'point', 1: 'sphere', 2: 'cylinder', 3: 'box', 4: 'polygon'}
feats = []; gaps = {'no_observation_ids': 0, 'no_classification': 0, 'cov_zero_not_unknown': 0, 'geo_unresolved': 0, 'no_note': 0}
with AnyReader([bag], default_typestore=ts) as r:
    for conn, t, raw in r.messages(connections=[c for c in r.connections if c.topic == topic]):
        m = r.deserialize(raw, conn.msgtype); p = m.geo_pose.position; st = m.header.stamp.sec + m.header.stamp.nanosec * 1e-9
        if not np.isfinite(p.latitude): gaps['geo_unresolved'] += 1; continue
        if not m.observation_ids: gaps['no_observation_ids'] += 1
        if not m.classification: gaps['no_classification'] += 1
        if m.kinematics.pose.covariance[0] == 0: gaps['cov_zero_not_unknown'] += 1
        if not m.note: gaps['no_note'] += 1
        origin = ORIGIN.get(m.origin_kind, '?'); status = STATUS.get(m.status, '?')
        feats.append({'type': 'Feature', 'id': f'{key[:16]}:{m.id}', 'geometry': {'type': 'Point', 'coordinates': [p.longitude, p.latitude, p.altitude]},
            'properties': {'contact_id_in_session': m.id, 'datetime': dt.datetime.fromtimestamp(st, dt.timezone.utc).isoformat(), 'marked_at': dt.datetime.fromtimestamp(t/1e9, dt.timezone.utc).isoformat(),
                'source': m.source, 'sensor_frame': m.header.frame_id, 'origin_kind': origin, 'status': status, 'existence_probability': float(m.existence_probability),
                'shape': {'type': SHAPE.get(m.shape.type), 'dimensions_m': [m.shape.dimensions.x, m.shape.dimensions.y, m.shape.dimensions.z]},
                'sensor_frame_position_m': [m.kinematics.pose.pose.position.x, m.kinematics.pose.pose.position.y, m.kinematics.pose.pose.position.z],
                'position_uncertainty': 'unknown' if m.kinematics.pose.covariance[0] == -1 else ('ZERO (as published; not a real uncertainty)' if m.kinematics.pose.covariance[0] == 0 else list(m.kinematics.pose.covariance[:3])),
                'classification': [{'class_id': c.class_id, 'probability': c.probability} for c in m.classification], 'observation_ids': list(m.observation_ids), 'attributes': {a.key: a.value for a in m.attributes}, 'note': m.note,
                'height_note': 'geo_pose.altitude as published (ellipsoidal per plugin); position frame "WGS84 as labelled" (RTK datum finding)'}})
state = 'reviewed' if all(f['properties']['origin_kind'] == 'human' or f['properties']['status'] == 'confirmed' for f in feats) else 'draft'
out = ROOT / 'features' / state / 'surveyed' / 'contacts'; out.mkdir(parents=True, exist_ok=True); fid = f'contacts-sidescan-{bag.name}'
fc = {'type': 'FeatureCollection', 'id': fid, 'features': feats, 'worldstore': {'category': 'features', 'quantity': 'contacts', 'state': state, 'origin': 'surveyed', 'source_id': f'sha256:{key}', 'source_bag': bag.name, 'topic': topic, 'message': 'marine_interfaces/msg/Contact (fields mapped 1:1)', 'fingerprint': hashlib.sha256((key + topic + 'contacts-ros-v0').encode()).hexdigest(), 'container': 'PROVISIONAL GeoJSON'}}
json.dump(fc, open(out / f'{fid}.geojson', 'w'), indent=1)
d = np.array([f['properties']['shape']['dimensions_m'][:2] for f in feats])
print(f"{fid}: {len(feats)} contacts → features/{state}/surveyed; box sizes p50 {np.median(d[:,0]):.1f}×{np.median(d[:,1]):.1f} m, max {d[:,0].max():.0f}×{d[:,1].max():.0f}; sources {sorted(set(f['properties']['source'] for f in feats))}; status {sorted(set(f['properties']['status'] for f in feats))}")
print("message-content gaps in what the plugin publishes:", gaps)
