#!/bin/bash
# phases.sh <bag> <t0> <t1> : per-phase wall split of one read_mbes_window via openat timestamps of the 3 reader opens (strace --seccomp-bpf, openat only)
source /home/roland/project11/.agent/scripts/setup.bash >/dev/null 2>&1
P=/home/roland/project11/layers/main/ui_ws/install/marine_perception_tools/lib/marine_perception_tools/sidescan_probe
out=$(mktemp /home/roland/project11/.agent/scratchpad/world_store_timing/st.XXXXXX)
S=$(date +%s.%N)
strace -f --seccomp-bpf -ttt -e trace=openat -o $out $P --mbes-window "$@" > $out.txt 2>&1
E=$(date +%s.%N)
m=($(grep "metadata.yaml" $out | awk '{print $2}'))
python3 - "$S" "$E" "${m[@]}" <<'P'
import sys
S,E,m1,m2,m3=map(float,sys.argv[1:6])
print(f"startup(proc start->first open)={m1-S:.2f}s tf_static_phase={m2-m1:.2f}s tf_phase={m3-m2:.2f}s detections_phase={E-m3:.2f}s total={E-S:.2f}s")
P
head -1 $out.txt; rm -f $out $out.txt
