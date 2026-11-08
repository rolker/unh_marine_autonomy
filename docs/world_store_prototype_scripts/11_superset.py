#!/usr/bin/env python3
"""Component 11: the replica rule over two builds' STAC records.
A build's INPUT SET = the checksums of its native (level-10) tiles, read from items.json.
  side A superset of B  -> A supersedes (its conflicted files win on both sides)
  equal                 -> identical inputs, keep either (prefer the newer build)
  incomparable          -> keep BOTH builds; the DESIGNATED one (given by the caller) is served as default.
Usage: 11_superset.py <side1_items.json> <side2_items.json> [--designate side1|side2]
Prints one line: WINNER=<side1|side2|both> REASON=<...>; exit 0 always (the decision is data, not an error)."""
import json, sys
def inputs(p):
    fc = json.load(open(p))
    return {f["assets"]["data"]["file:checksum"] for f in fc["features"] if f["properties"].get("worldstore:native")}, len(fc["features"])
a, b = sys.argv[1], sys.argv[2]; designated = sys.argv[4] if len(sys.argv) > 4 and sys.argv[3] == "--designate" else "side1"
A, na = inputs(a); B, nb = inputs(b)
if A == B: print(f"WINNER=either REASON=identical input sets ({len(A)} native tiles)")
elif A > B: print(f"WINNER=side1 REASON=side1 inputs superset of side2 (+{len(A-B)} native tiles, none missing)")
elif B > A: print(f"WINNER=side2 REASON=side2 inputs superset of side1 (+{len(B-A)} native tiles, none missing)")
else: print(f"WINNER=both REASON=incomparable: side1 has {len(A-B)} native tiles side2 lacks, side2 has {len(B-A)} side1 lacks; keep both, serve {designated} as designated")
