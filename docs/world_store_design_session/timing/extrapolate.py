# Extrapolation (NOT a measurement). Inputs are measured, see results.md.
T1=84.0   # min, owner's measured single-process full-campaign rebuild (33 bags, ~12 M pings)
# measured CPU inflation per process (sum user CPU parallel / sum user CPU solo, local disk):
#   2 procs: (166.9+131.9)/(156.0+124.6)=1.065 ; 4 procs: 325.2/280.6=1.159
# effective core-equivalents E(K)=K/f(K). 8 physical cores / 16 threads (i7-10875H).
f={1:1.0,2:1.065,4:1.159, 8:1.36, 12:1.62}   # 8 = linear extrapolation of (K-1)*0.053 ; 12 = SMT: capped at ~1.25x of the 8-proc capacity
def T_naive(K,a,imb,f):   # every worker reads+projects ALL bags, CUBEs only its area
    return T1*(a+(1-a)*imb/K)*f
def T_pre(K,a,imb,f):     # project once in parallel (by bag) into area-partitioned spill, then CUBE by area; spill I/O NOT modelled
    return T1*(a/K+(1-a)*imb/K)*f
sc={"optimistic":(0.13,1.00,0.95),"central":(0.20,1.15,1.00),"pessimistic":(0.28,1.30,1.10)}
print("| workers | design | optimistic min | central min | pessimistic min |\n|---|---|---|---|---|")
for K in (2,4,8,12):
    for name,fn in (("every worker reads+projects all bags",T_naive),("project once, partition by area",T_pre)):
        if K==1 and fn is T_pre: continue
        vals=[fn(K,a,imb,f[K]*ff) for a,imb,ff in sc.values()]
        print(f"| {K} | {name} | "+" | ".join(f"{v:.0f}" for v in vals)+" |")
