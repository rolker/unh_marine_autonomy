#!/usr/bin/env python3
"""Component 6: cleaning marks. Two schemas under test:
  sounding-level: rows (source_id, receive_time_ns, beam) — identity survives any relink
  node-level (BAG tracking list shape): rows (tile, row, col, depth_before, depth_after) on the product grid
and an area mark (polygon + time window) applied either resolved-to-soundings at mark time or by position at link time.
The relink moves every sounding ~1.1 m horizontally, the order of the NAD83(2011) -> ITRF2020 change."""
import sys, json, numpy as np, pyarrow as pa, pyarrow.parquet as pq
sys.path.insert(0, '/home/roland/data/world_proto/scripts'); import link_lib as L
P = '/home/roland/data/world_proto'; OBS = f'{P}/c6/obs_truth_dedup.zarr'; TRJ = f'{P}/c7/traj_fcu.parquet'; CAT = f'{P}/corrections/items.json'; O = f'{P}/c6'
import os; os.makedirs(O, exist_ok=True); SHIFT = (1.0, -0.5)
def ids(g): r0 = g['recv'][g['valid']].min(); return (g['recv'] - r0) * 512 + g['beam'], r0
def cell_stats(key, Z):
    order = np.argsort(key, kind='stable'); ks, zs = key[order], Z[order]; b = np.r_[0, np.flatnonzero(np.diff(ks)) + 1, ks.size]
    med = np.empty_like(zs); mad = np.empty_like(zs)
    for s, e in zip(b[:-1], b[1:]): m = np.median(zs[s:e]); med[s:e] = m; mad[s:e] = np.median(np.abs(zs[s:e] - m))
    out_med = np.empty_like(med); out_mad = np.empty_like(mad); out_med[order] = med; out_mad[order] = mad; return out_med, out_mad
# ---- first link (mark time) ------------------------------------------------------------------------------
g = L.georef(OBS, TRJ, CAT); v = g['valid']; sid, r0 = ids(g); LAT, LON, Z, SID = g['LAT'][v], g['LON'][v], g['Z'][v], sid[v]
gy, gx = L.cell_index(LAT, LON); key = gy * 100_000_000 + gx; med, mad = cell_stats(key, Z)
gate = (np.abs(Z - med) > 1.0) & (np.abs(Z - med) > 5 * np.maximum(mad, 0.02))
pq.write_table(pa.table({'source_id': [g['source_id']] * int(gate.sum()), 'receive_time_ns': (SID[gate] // 512 + r0).astype(np.int64), 'beam': (SID[gate] % 512).astype(np.int16),
    'action': ['reject'] * int(gate.sum()), 'reason': ['gate_v1: |z - cell median| > max(1 m, 5 MAD)'] * int(gate.sum())}).replace_schema_metadata({b'producer': b'gate_v1 (prototype)', b'kind': b'cleaning_marks/sounding'}), f'{O}/marks_gate_v1.parquet')
# ---- a person's mark: the 25 m square with the most kept-but-noisy soundings ------------------------------
keep = ~gate; noisy = keep & (np.abs(Z - med) > 0.4)
bx = np.floor(LON[noisy] / (25 / (111000 * np.cos(np.radians(43))))); by = np.floor(LAT[noisy] / (25 / 111000)); u, c = np.unique(np.stack([by, bx], 1), axis=0, return_counts=True); cy0, cx0 = u[np.argmax(c)]; lat0, lat1 = cy0 * 25 / 111000, (cy0 + 1) * 25 / 111000; w = 25 / (111000 * np.cos(np.radians(43))); lon0, lon1 = cx0 * w, (cx0 + 1) * w
inbox = (LAT >= lat0) & (LAT < lat1) & (LON >= lon0) & (LON < lon1); t_rec = SID // 512 + r0; tw = (int(t_rec[inbox].min()), int(t_rec[inbox].max()))
man = inbox & noisy                                          # the person rejects the noisy soundings in the box during that pass
area = {'type': 'FeatureCollection', 'features': [{'type': 'Feature', 'geometry': {'type': 'Polygon', 'coordinates': [[[lon0, lat0], [lon1, lat0], [lon1, lat1], [lon0, lat1], [lon0, lat0]]]},
        'properties': {'action': 'reject', 'selector': '|z - cell median| > 0.4 m', 'receive_time_ns': tw, 'author': 'synthesized manual mark (prototype)', 'reason': 'noisy patch the gate kept'}}]}
json.dump(area, open(f'{O}/mark_manual_area.geojson', 'w'))
pq.write_table(pa.table({'source_id': [g['source_id']] * int(man.sum()), 'receive_time_ns': (SID[man] // 512 + r0).astype(np.int64), 'beam': (SID[man] % 512).astype(np.int16), 'action': ['reject'] * int(man.sum()), 'reason': ['manual: noisy patch the gate kept'] * int(man.sum())}), f'{O}/marks_manual_resolved.parquet')
# ---- node-level edits (tracking list) on the product: the 50 cells the gate changes most -------------------
uy, ux, mean0, cnt0, _ = L.grid(LAT, LON, Z); uy1, ux1, mean1, cnt1, _ = L.grid(LAT[keep], LON[keep], Z[keep])
m0 = dict(zip(uy * 100_000_000 + ux, mean0)); m1 = dict(zip(uy1 * 100_000_000 + ux1, mean1)); d = {k: m1[k] - m0[k] for k in m1}
top = sorted(d, key=lambda k: abs(d[k]), reverse=True)[:50]
track = [{'cell_key': int(k), 'depth_before': float(m0[k]), 'depth_after': float(m1[k]), 'reason': 'tracking-list edit (prototype)'} for k in top]
json.dump(track, open(f'{O}/tracking_list.json', 'w'))
print(f'MARK TIME: soundings {Z.size:,}; gate rejects {gate.sum():,}; manual box 25 m, {inbox.sum():,} soundings in box, person rejects {man.sum():,}; tracking list: 50 cells, |edit| {np.abs([d[k] for k in top]).min():.2f}..{np.abs([d[k] for k in top]).max():.2f} m')
# ---- relink after a ~1.1 m horizontal shift ------------------------------------------------------------------
g2 = L.georef(OBS, TRJ, CAT, shift_en=SHIFT); v2 = g2['valid']; sid2, _ = ids(g2); LAT2, LON2, Z2, SID2 = g2['LAT'][v2], g2['LON'][v2], g2['Z'][v2], sid2[v2]
gate_ids = set((pq.read_table(f'{O}/marks_gate_v1.parquet')['receive_time_ns'].to_numpy() - r0) * 512 + pq.read_table(f'{O}/marks_gate_v1.parquet')['beam'].to_numpy())
man_ids = set((pq.read_table(f'{O}/marks_manual_resolved.parquet')['receive_time_ns'].to_numpy() - r0) * 512 + pq.read_table(f'{O}/marks_manual_resolved.parquet')['beam'].to_numpy())
rej_gate2 = np.isin(SID2, list(gate_ids)); rej_man2 = np.isin(SID2, list(man_ids))
print(f'RELINK (+{SHIFT[0]} m E, {SHIFT[1]} m N): sounding-level marks still select gate {rej_gate2.sum():,} of {len(gate_ids):,}, manual {rej_man2.sum():,} of {len(man_ids):,} (by identity)')
gy2, gx2 = L.cell_index(LAT2, LON2); key2 = gy2 * 100_000_000 + gx2; med2, _ = cell_stats(key2, Z2)
inbox2 = (LAT2 >= lat0) & (LAT2 < lat1) & (LON2 >= lon0) & (LON2 < lon1) & (SID2 // 512 + r0 >= tw[0]) & (SID2 // 512 + r0 <= tw[1])
sel2 = inbox2 & (np.abs(Z2 - med2) > 0.4); same = np.isin(SID2[sel2], list(man_ids)).sum()
print(f'  area mark applied BY POSITION at relink: selects {sel2.sum():,} soundings: {same:,} of the {len(man_ids):,} the person rejected, {sel2.sum() - same:,} they never saw, misses {len(man_ids) - same:,}')
uyS, uxS, meanS, cntS, _ = L.grid(LAT2, LON2, Z2); mS = dict(zip(uyS * 100_000_000 + uxS, meanS))
present = [e for e in track if e['cell_key'] in mS]; stale = [e for e in present if abs(mS[e['cell_key']] - e['depth_before']) > 0.01]
print(f'  tracking list on the relinked product: {len(track)} edits, {len(track) - len(present)} land on empty cells, {len(stale)} of {len(present)} find depth_before no longer matching (stale), {len(present) - len(stale)} still match')
