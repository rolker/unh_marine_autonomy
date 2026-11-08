#!/bin/bash
# Component 3a: GDAL GTI over a rung, one index per level, chained as overviews. Needs GDAL >= 3.9. Usage: 03_gti.sh <cog_dir> <extra_tiles_dir> <out_dir>
set -e; COG=$1; EXTRA=$2; OUT=$3; mkdir -p $OUT; cd $OUT; rm -f *.gti.gpkg *.gti
span10=$(python3 -c "print(8/2**10/960)")
gdaltindex -q -overwrite -f GPKG -lyr_name tiles -tr $span10 $span10 -ot Float64 -bandcount 2 -nodata nan -nodata nan -write_absolute_path level10_raw.gti.gpkg $COG/10_*.tif $EXTRA/10_*.tif
TE=$(gdalinfo level10_raw.gti.gpkg | python3 -c "import sys,re; t=sys.stdin.read(); ul=re.search(r'Upper Left\s*\(\s*([-\d.]+),\s*([-\d.]+)',t); lr=re.search(r'Lower Right\s*\(\s*([-\d.]+),\s*([-\d.]+)',t); print(ul[1],lr[2],lr[1],ul[2])")
TE=$(python3 -c "import math; x0,y0,x1,y1=map(float,'$TE'.split()); f=lambda v,o: math.floor((v-o)/8)*8+o; c=lambda v,o: math.ceil((v-o)/8)*8+o; print(f(x0,-180),f(y0,-96),c(x1,-180),c(y1,-96))")
gdaltindex -q -overwrite -f GPKG -lyr_name tiles -tr $span10 $span10 -te $TE -ot Float64 -bandcount 2 -nodata nan -nodata nan -write_absolute_path level10.gti.gpkg $COG/10_*.tif $EXTRA/10_*.tif
echo "base extent snapped to level-0 grids: $TE"
for l in 9 8 7 6 5 4 3 2 1 0; do
  span=$(python3 -c "print(8/2**$l/960)")
  gdaltindex -q -overwrite -f GPKG -lyr_name tiles -tr $span $span -ot Float64 -bandcount 2 -nodata nan -nodata nan -te $TE -write_absolute_path level$l.gti.gpkg $COG/overviews/${l}_*.tif
done
{ echo "<GDALTileIndexDataset><IndexDataset>$OUT/level10.gti.gpkg</IndexDataset><IndexLayer>tiles</IndexLayer>"
  for l in 9 8 7 6 5 4 3 2 1 0; do echo "  <Overview><Dataset>$OUT/level$l.gti.gpkg</Dataset><Layer>tiles</Layer></Overview>"; done
  echo "</GDALTileIndexDataset>"; } > depths_processed.gti
echo "== open the chained GTI"; gdalinfo depths_processed.gti | grep -E 'Driver|Size is|Pixel Size|Overviews|Band [12]|NoData' | head -8
