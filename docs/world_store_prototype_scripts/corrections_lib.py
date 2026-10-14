"""Corrections catalog lookup: each builder asks for the Items it OWNS that apply to what it is building."""
import json, datetime as dt
def _t(s): return dt.datetime.fromisoformat(s.replace('Z', '+00:00')).timestamp()
def load(items_json):
    return json.load(open(items_json))['features'] if items_json else []
def applicable(items, owner, t0, t1, **match):
    out = []
    for it in items:
        p = it['properties']
        if p.get('worldstore:owner') != owner: continue
        if _t(p['end_datetime']) < t0 or _t(p['start_datetime']) > t1: continue
        a = p.get('worldstore:applies_to', {})
        if all(a.get(k) == v for k, v in match.items() if k in a): out.append(it)
    return out
