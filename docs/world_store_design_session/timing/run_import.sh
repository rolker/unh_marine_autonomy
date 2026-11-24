#!/bin/bash
# run_import.sh <tag> <bag_dir> [extra import_bag args...]
# Imports ONE bag into its own fresh scratch store (stores/<tag>/{depths,bs}), same arguments as
# unh_echoboats_project11/scripts/build_bathy_store.sh (minus ros2 run; reference layer copied to refstore/).
tag=$1; bag=$2; shift 2
W=/home/roland/project11/.agent/scratchpad/world_store_timing
source /home/roland/project11/.agent/scripts/setup.bash >/dev/null 2>&1
IB=/home/roland/project11/layers/main/sensors_ws/install/cube_bathymetry/lib/cube_bathymetry/import_bag
CURVE=/home/roland/project11/layers/main/platforms_ws/src/unh_echoboats_project11/bizzyboat_project11/config/m3_angular_response_curve.csv
rm -rf $W/stores/$tag; mkdir -p $W/stores/$tag/depths $W/stores/$tag/bs
echo "start $(date +%s.%N) $(date '+%F %T')" > $W/logs/$tag.log
/usr/bin/time -v $IB -o $W/stores/$tag/depths --bs-store $W/stores/$tag/bs \
  -d /bizzy/sensors/m3/detections --odom-topic /bizzy/odom -r 1.0 \
  --base-link-frame bizzy/base_link --level-frame bizzy/base_link_north_up --tide-frame bizzy/map_tide \
  --platform bizzyboat --sensor kongsberg-m3 --campaign timing-test \
  --reference-store $W/refstore \
  --backscatter-correction empirical --backscatter-curve $CURVE \
  "$@" "$bag" >> $W/logs/$tag.log 2>&1
echo "exit $? end $(date +%s.%N)" >> $W/logs/$tag.log
