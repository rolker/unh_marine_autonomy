#!/bin/bash
# run_set.sh <label> <tag:bagdir> ...  : run the listed imports at the same time (each in its own store), with a memory guard.
W=/home/roland/project11/.agent/scratchpad/world_store_timing
label=$1; shift
[ $# -le 4 ] || { echo "max 4 processes"; exit 2; }
echo "=== set $label  $(date '+%F %T %:z')  loadavg: $(cut -d' ' -f1-3 /proc/loadavg)  MemAvail_MB: $(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo)"
# cold start: drop page cache of the bag files (read-only fadvise)
python3 $W/evict.py $(for a in "$@"; do echo "${a#*:}"; done) >/dev/null
S=$(date +%s.%N)
pids=(); tags=()
for a in "$@"; do
  tag=${a%%:*}; bag=${a#*:}
  setsid $W/run_import.sh "$tag" "$bag" &
  pids+=($!); tags+=($tag)
done
minavail=999999
while :; do
  alive=0; for p in "${pids[@]}"; do kill -0 $p 2>/dev/null && alive=1; done
  [ $alive = 1 ] || break
  av=$(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo)
  [ $av -lt $minavail ] && minavail=$av
  if [ $av -lt 4096 ]; then echo "MEMORY GUARD: available ${av} MB < 4096, killing set"; for p in "${pids[@]}"; do kill -TERM -- -$p 2>/dev/null; done; break; fi
  sleep 2
done
for p in "${pids[@]}"; do wait $p; done
E=$(date +%s.%N)
echo "set $label makespan_wall_s=$(echo "$E - $S" | bc)  min_MemAvailable_MB=$minavail"
