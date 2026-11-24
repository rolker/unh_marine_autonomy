#!/bin/bash
# usage: run_boxA.sh <label> <s w n e>   -- emulates the lab's gather+read, run by run
label=$1; shift
source /home/roland/project11/.agent/scripts/setup.bash >/dev/null 2>&1
Q=/home/roland/project11/layers/main/core_ws/install/marine_survey_index/lib/marine_survey_index/survey_index_query
P=/home/roland/project11/layers/main/ui_ws/install/marine_perception_tools/lib/marine_perception_tools/sidescan_probe
cd /home/roland/project11/.agent/scratchpad/world_store_timing
echo "=== $label  box=$*  start $(date '+%Y-%m-%d %H:%M:%S %:z')"
echo "-- stage 1: survey index query (CLI incl. process start)"
/usr/bin/time -f "index_query wall=%e user=%U sys=%S maxrss_kb=%M" $Q --box "$@" --db ~/data/world/survey_index.db --sensor mbes-bathy --json >/dev/null
python3 plan_box.py "$@" > plan_$label.tsv
echo "-- stage 2..4: read_mbes_window per coalesced pass (sidescan_probe --mbes-window)"
T0=$(date +%s.%N)
while IFS=$'\t' read -r bag ts te pings; do
  /usr/bin/time -f "  wall=%e user=%U sys=%S maxrss_kb=%M  fs_in=%I" $P --mbes-window "$bag" $ts $te 2>&1 | grep -E "mbes window|wall=" | tr '\n' ' '; echo "  [$(basename $bag) idx_pings=$pings]"
done < plan_$label.tsv
T1=$(date +%s.%N)
echo "TOTAL read wall: $(echo "$T1 - $T0" | bc) s"
