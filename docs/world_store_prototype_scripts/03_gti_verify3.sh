#!/bin/bash
set -e; P=/proto; COG=$P/depths/processed_cog_c26; G=$P/gti_from_stac/depths_processed.gti; cd /tmp
ext() { gdalinfo "$1" | python3 -c "import sys,re; t=sys.stdin.read(); ul=re.search(r'Upper Left\s*\(\s*([-\d.]+),\s*([-\d.]+)',t); lr=re.search(r'Lower Right\s*\(\s*([-\d.]+),\s*([-\d.]+)',t); print(ul[1],lr[2],lr[1],ul[2])"; }
echo "== (b6) chained overviews with common extent: level-9 and level-3 windows vs the overview tiles"
for T in $COG/overviews/9_8626_6709.tif $(ls $COG/overviews/3_*.tif | head -1); do l=$(basename $T | cut -d_ -f1); s=$(python3 -c "print(8/2**$l/960)"); E=$(ext $T); gdal_translate -q -projwin_srs EPSG:4326 -projwin $(echo $E | awk '{print $1,$4,$3,$2}') -tr $s $s $G o.tif; printf '  L%s GTI: ' $l; gdalinfo -checksum o.tif | grep -E 'Size is|Checksum' | tr '\n' ' '; printf '\n  L%s tile: ' $l; gdalinfo -checksum $T | grep -E 'Size is|Checksum' | tr '\n' ' '; echo; done
echo "== (d4) mixed index: level-10 wins where it has data, level-9 shows through NaN holes?"
s10=$(python3 -c "print(8/2**10/960)"); gdaltindex -q -overwrite -f GPKG -lyr_name tiles -tr $s10 $s10 -ot Float64 -bandcount 2 -nodata nan -nodata nan -write_absolute_path $P/gti_from_stac/mixed.gti.gpkg $COG/overviews/9_8626_6709.tif $COG/10_17252_13419.tif
E=$(ext $COG/10_17252_13419.tif); gdal_translate -q -projwin_srs EPSG:4326 -projwin $(echo $E | awk '{print $1,$4,$3,$2}') $P/gti_from_stac/mixed.gti.gpkg d4.tif
python3 - <<PY
from osgeo import gdal; import numpy as np
a=gdal.Open("/tmp/d4.tif").ReadAsArray(); b=gdal.Open("$COG/10_17252_13419.tif").ReadAsArray()
m=~np.isnan(b[0]); print("  level-10 data cells:", m.sum(), " equal there:", np.array_equal(a[0][m], b[0][m]) and np.array_equal(a[1][m], b[1][m]), " filled-from-level-9 cells:", (~np.isnan(a[0]) & ~m).sum())
PY
