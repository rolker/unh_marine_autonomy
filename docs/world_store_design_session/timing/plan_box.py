#!/usr/bin/env python3
"""Replicate the CUBE lab's pass gather: survey_index_query --box (same
marine_survey_index::queryPasses library call as SurveyIndexBridge::queryBox),
then coalescePasses() from pass_coalesce.hpp (5 s gap rule, per bag/sensor/topic).
Prints one TSV line per coalesced pass: bag t_start_ns t_end_ns pings."""
import json, subprocess, sys, collections
Q="/home/roland/project11/layers/main/core_ws/install/marine_survey_index/lib/marine_survey_index/survey_index_query"
s,w,n,e = sys.argv[1:5]
out = subprocess.run([Q,"--box",s,w,n,e,"--db","/home/roland/data/world/survey_index.db","--sensor","mbes-bathy","--json"],capture_output=True,text=True,check=True).stdout
rows = json.loads(out)
rows.sort(key=lambda r:(r["bag"],r["t_start_ns"]))
GAP=5_000_000_000
buckets=collections.OrderedDict()
for r in rows:
    k=(r["bag"],r["sensor"],r["topic"]); m=buckets.setdefault(k,[])
    if m and r["t_start_ns"]<=m[-1][2]+GAP:
        m[-1][2]=max(m[-1][2],r["t_end_ns"]); m[-1][3]+=r["ping_count"]
    else: m.append([r["bag"],r["t_start_ns"],r["t_end_ns"],r["ping_count"]])
passes=sorted([p for m in buckets.values() for p in m],key=lambda p:(p[0],p[1]))
for p in passes: print("\t".join(map(str,p)))
print(f"# rows={len(rows)} passes={len(passes)} bags={len(set(p[0] for p in passes))} pings={sum(p[3] for p in passes)} span_s={sum(p[2]-p[1] for p in passes)/1e9:.1f}",file=sys.stderr)
