#!/bin/bash
RUNS=${1:-31}
for res in 640x360 1280x720 1920x1080; do
  python3 - "$RUNS" "$res" <<PYEOF
import subprocess,sys,statistics
runs=int(sys.argv[1]);res=sys.argv[2]
def run(bin):
    core=str(11+(hash(bin+res)%4))
    p=subprocess.run(["taskset","-c",core,bin,f"t_{res}.ogv"],capture_output=True,text=True)
    return int(p.stderr.split("avg_ns=")[1])
B=sorted(run("./dec_bench_base") for _ in range(runs))
O=sorted(run("./dec_bench_opt") for _ in range(runs))
L=sorted(run("./dec_bench_lto") for _ in range(runs))
def med(a):return statistics.median(a)
def pct(a,q):return a[min(len(a)-1,int(len(a)*q))]
bm,om,lm=med(B),med(O),med(L)
print(f"{res}")
print(f"  base median={bm}  p25={pct(B,.25)}  p75={pct(B,.75)}")
print(f"  opt  median={om} ({(1-om/bm)*100:+.1f}%)  p25={pct(O,.25)} p75={pct(O,.75)}")
print(f"  lto  median={lm} ({(1-lm/bm)*100:+.1f}% vs base, {(1-lm/om)*100:+.1f}% vs opt) p25={pct(L,.25)} p75={pct(L,.75)}")
PYEOF
done
