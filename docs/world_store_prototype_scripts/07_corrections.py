#!/usr/bin/env python3
"""Component 7: write the corrections catalog (STAC Collection 'corrections', one Item per reviewed correction).
Usage: 07_corrections.py <out_dir>"""
import sys, os, json, datetime as dt, pystac
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
def T(s): return dt.datetime.fromisoformat(s.replace('Z', '+00:00'))
coll = pystac.Collection(id='corrections', description='Reviewed corrections, applied by the builder that owns each kind (prototype)',
    extent=pystac.Extent(pystac.SpatialExtent([[-180, -96, 180, 96]]), pystac.TemporalExtent([[T('2026-04-01T00:00:00Z'), T('2026-09-18T00:00:00Z')]])), license='proprietary')
coll.extra_fields.update({'worldstore:category': 'corrections'})
def item(iid, kind, owner, start, end, applies, params, evidence, note):
    it = pystac.Item(id=iid, geometry=None, bbox=None, datetime=None, start_datetime=T(start), end_datetime=T(end),
        properties={'worldstore:kind': kind, 'worldstore:owner': owner, 'worldstore:applies_to': applies, 'worldstore:parameters': params,
                    'worldstore:evidence': evidence, 'worldstore:reviewed_by': 'UNREVIEWED (prototype)', 'worldstore:note': note})
    coll.add_item(it)
item('m3-clock-skew-2026-06', 'clock_skew', 'observations', '2026-06-24T00:00:00Z', '2026-06-27T16:00:00Z',
     {'platform': 'bizzyboat', 'topic': '/bizzy/sensors/m3/detections'},
     {'method': 'windowed_upper_envelope_integer_offset', 'reference': 'receive_time', 'half_window_pings': 200, 'derived': True},
     ['https://github.com/rolker/unh_echoboats_project11/issues/338', 'https://github.com/rolker/unh_echoboats_project11/pull/343'],
     'Integer-second skew of the M3 header stamps vs the gabby receive clock (+6 s, June 24-26). DERIVED per ping from the bag, so re-applying to corrected data measures 0 and changes nothing. Interval bounds UNVERIFIED.')
item('m3-geometry-2026-06-27', 'platform_geometry', 'link', '2026-04-01T00:00:00Z', '2026-09-14T00:00:00Z',
     {'platform': 'bizzyboat', 'parent': 'bizzy/base_link', 'child': 'bizzy/m3'},
     {'translation_m': [-0.29, 0.0, -0.28], 'rotation_xyzw': [1.0, 0.0, 0.0, 0.0], 'replaces_recorded_tf_static': True},
     ['https://github.com/rolker/unh_echoboats_project11/issues/339', 'https://github.com/rolker/unh_echoboats_project11/pull/341'],
     'Measured 2026-06-27; the install-log value (-0.23, 0, -0.145) was wrong from installation. Start = season start (install date UNVERIFIED); end = M3 removed 2026-09-14. Replaces, never adds: idempotent.')
item('fcu-egm96-altitude-2026', 'vertical_reference', 'trajectory', '2026-04-01T00:00:00Z', '2026-08-21T00:00:00Z',
     {'platform': 'bizzyboat', 'source': 'fcu', 'topic': '/bizzy/mavros/global_position/global'},
     {'method': 'derived_from_raw_fix', 'reference_topic': '/bizzy/mavros/global_position/raw/fix', 'statistic': 'median(raw - global)', 'fallback_m': -0.626, 'fallback_site': 'UNH pier 2026-08-21'},
     ['https://github.com/rolker/seafloor_echoboat_project11 echo_helm ellipsoidal_fix_node (e491740, 2026-08-21)'],
     'mavros round-trips the fused altitude through EGM96-5 while the receiver used its own geoid; the difference is SITE-DEPENDENT, so it is derived per window from the raw (ellipsoidal) fix, not a constant.')
coll.normalize_hrefs(out); coll.save(catalog_type=pystac.CatalogType.SELF_CONTAINED)
json.dump({'type': 'FeatureCollection', 'features': [i.to_dict(include_self_link=False) for i in coll.get_all_items()]}, open(f'{out}/items.json', 'w'), indent=1)
print('corrections:', [i.id for i in coll.get_all_items()])
