import re,glob,os,collections
W="/home/roland/project11/.agent/scratchpad/world_store_timing/logs/"
def parse(f):
    t=open(f).read()
    st=float(re.search(r"^start (\d+\.\d+)",t,re.M)[1]); en=float(re.search(r"end (\d+\.\d+)",t)[1])
    pings=int(re.search(r"Offline projection: (\d+) pings projected",t)[1])
    sold=int(re.search(r"pings projected, \d+ georeferenced into the grid, \d+ dropped \(no earth TF\); (\d+) soundings",t)[1])
    g=lambda p:float(re.search(p,t)[1])
    el=re.search(r"Elapsed \(wall clock\) time \(h:mm:ss or m:ss\): ([\d:.]+)",t)[1]
    parts=[float(x) for x in el.split(":")]; el=sum(v*60**i for i,v in enumerate(reversed(parts)))
    return dict(start=st,end=en,wall=el,user=g(r"User time \(seconds\): ([\d.]+)"),sys=g(r"System time \(seconds\): ([\d.]+)"),
      rss=g(r"Maximum resident set size \(kbytes\): (\d+)")/1024,cpu=g(r"Percent of CPU this job got: (\d+)%"),pings=pings,soundings=sold)
sets=collections.OrderedDict([
 ("NAS 1 alone (cold cache)",["solo/solo_b1","solo/solo_b2","solo/solo_b3","solo/solo_b4"]),
 ("NAS 2 at once: b1+b2",["par2/par2a_b1","par2/par2a_b2"]),
 ("NAS 2 at once: b3+b4",["par2/par2b_b3","par2/par2b_b4"]),
 ("NAS 4 at once",["par4/par4_b1","par4/par4_b2","par4/par4_b3","par4/par4_b4"]),
 ("LOCAL 1 alone (cold cache)",["local/lsolo_b1","local/lsolo_b2","local/local_cold","local/lsolo_b4"]),
 ("LOCAL 2 at once: b1+b2",["local/lpar2a_b1","local/lpar2a_b2"]),
 ("LOCAL 2 at once: b3+b4",["local/lpar2b_b3","local/lpar2b_b4"]),
 ("LOCAL 4 at once",["local/lpar4_b1","local/lpar4_b2","local/lpar4_b3","local/lpar4_b4"]),
])
print("| run | bag | pings | wall s | user s | sys s | %CPU | max RSS MB | pings/min (own wall) |\n|---|---|---|---|---|---|---|---|---|")
res={}
for name,fs in sets.items():
    rows=[(f,parse(W+f+".log")) for f in fs]
    res[name]=rows
    for f,r in rows:
        print(f"| {name} | {os.path.basename(f)} | {r['pings']} | {r['wall']:.1f} | {r['user']:.1f} | {r['sys']:.1f} | {r['cpu']:.0f} | {r['rss']:.0f} | {r['pings']/r['wall']*60:,.0f} |")
print()
print("| set | procs | total pings | makespan s (first start -> last end) | total pings/min | pings/min per process (mean) | sum of own walls s | sum user s |\n|---|---|---|---|---|---|---|---|")
for name,rows in res.items():
    rs=[r for f,r in rows]
    if name.endswith("alone (cold cache)"):
        continue
    ms=max(r['end'] for r in rs)-min(r['start'] for r in rs)
    tp=sum(r['pings'] for r in rs)
    print(f"| {name} | {len(rs)} | {tp} | {ms:.1f} | {tp/ms*60:,.0f} | {tp/ms*60/len(rs):,.0f} | {sum(r['wall'] for r in rs):.1f} | {sum(r['user'] for r in rs):.1f} |")
# efficiency: sum of solo walls for the same bags / (n * makespan)
def solo(prefix,bagtag):
    return {k:v for k,v in dict((os.path.basename(f).split('_')[-1],r) for f,r in res[prefix]).items()}
S_nas=solo("NAS 1 alone (cold cache)",None); S_loc=solo("LOCAL 1 alone (cold cache)",None)
S_loc['b3']=dict(parse(W+"local/local_cold.log"))
print()
print("| set | bags | sum of solo walls s | makespan s | speedup (sum solo / makespan) | efficiency = speedup / procs |\n|---|---|---|---|---|---|")
for name,rows in res.items():
    if name.endswith("alone (cold cache)"): continue
    rs=[r for f,r in rows]; bags=[os.path.basename(f).split('_')[-1] for f,r in rows]
    S=S_nas if name.startswith("NAS") else S_loc
    ssum=sum(S[b]['wall'] for b in bags)
    ms=max(r['end'] for r in rs)-min(r['start'] for r in rs)
    print(f"| {name} | {','.join(bags)} | {ssum:.1f} | {ms:.1f} | {ssum/ms:.2f}x | {ssum/ms/len(rs)*100:.0f}% |")
