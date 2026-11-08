#!/bin/bash
# Component 1: convert every tile of a store rung to COG. Usage: 01_cog.sh <src_dir> <dst_dir>
set -e
SRC=$1; DST=$2; mkdir -p "$DST/overviews"
n=0; t0=$(date +%s)
for f in "$SRC"/*.tif "$SRC"/overviews/*.tif; do
  rel=${f#$SRC/}
  gdal_translate -q -of COG -co COMPRESS=DEFLATE -co PREDICTOR=3 -co BLOCKSIZE=256 -co OVERVIEWS=NONE "$f" "$DST/$rel"
  n=$((n+1))
done
echo "converted $n tiles in $(( $(date +%s) - t0 ))s: $(du -sh "$SRC" | cut -f1) -> $(du -sh "$DST" | cut -f1)"
