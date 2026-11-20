#!/bin/bash
set -e; P=/proto; COG=$P/depths/processed_cog_c26; cd /tmp
ext() { gdalinfo "$1" | python3 -c "import sys,re; t=sys.stdin.read(); ul=re.search(r'Upper Left\s*\(\s*([-\d.]+),\s*([-\d.]+)',t); lr=re.search(r'Lower Right\s*\(\s*([-\d.]+),\s*([-\d.]+)',t); print(ul[1],lr[2],lr[1],ul[2])"; }
T9=$COG/overviews/9_8626_6709.tif; E=$(ext $T9); W=$(echo $E | awk '{print $1,$4,$3,$2}'); s9=$(python3 -c "print(8/2**9/960)"); s10=$(python3 -c "print(8/2**10/960)")
echo "== (b2) same window straight from level9.gti.gpkg (no chain)"; gdal_translate -q -projwin_srs EPSG:4326 -projwin $W $P/gti/level9.gti.gpkg b2.tif; gdalinfo -checksum b2.tif | grep Checksum | tr '\n' ' '; echo
echo "== (b3) chained GTI, explicit -ovr 0"; gdal_translate -q -projwin_srs EPSG:4326 -projwin $W -ovr 0 $P/gti/depths_processed.gti b3.tif; gdalinfo -checksum b3.tif | grep -E 'Size|Checksum' | tr '\n' ' '; echo
echo "== (b4) chained GTI, -tr level9 with -r near vs average"; for r in near average; do gdal_translate -q -projwin_srs EPSG:4326 -projwin $W -tr $s9 $s9 -r $r $P/gti/depths_processed.gti b4.tif; printf '  %s: ' $r; gdalinfo -checksum b4.tif | grep Checksum | tr '\n' ' '; echo; done
echo "== (b5) does the level-9 tile itself differ from a nearest fold of level-10? band2 stats of T9 vs its 4 children"; gdalinfo -stats $T9 | grep -E 'STATISTICS_(MEAN|VALID)' | tr '\n' ' '; echo
echo "== (d2) mixed index, level-9 tile FIRST then level-10 (last wins?)"; gdaltindex -q -overwrite -f GPKG -lyr_name tiles -tr $s10 $s10 -ot Float64 -bandcount 2 -nodata nan -nodata nan -write_absolute_path $P/gti/mixed2.gti.gpkg $T9 $COG/10_17252_13419.tif; E=$(ext $COG/10_17252_13419.tif); gdal_translate -q -projwin_srs EPSG:4326 -projwin $(echo $E | awk '{print $1,$4,$3,$2}') $P/gti/mixed2.gti.gpkg d2.tif; gdalinfo -checksum d2.tif | grep Checksum | tr '\n' ' '; echo " (expect 20175 15241)"
echo "== (d3) mixed index with a 'res' sort field (finest last)"; ogrinfo -q $P/gti/mixed.gti.gpkg -sql "ALTER TABLE tiles ADD COLUMN res REAL"; ogrinfo -q $P/gti/mixed.gti.gpkg -sql "UPDATE tiles SET res = CASE WHEN location LIKE '%/overviews/%' THEN 2 ELSE 1 END"; python3 - <<PY
from osgeo import gdal, ogr
ds = ogr.Open("$P/gti/mixed.gti.gpkg", 1); lyr = ds.GetLayer(0); lyr.SetMetadataItem("SORT_FIELD", "res"); lyr.SetMetadataItem("SORT_FIELD_ASC", "NO"); ds = None
PY
gdal_translate -q -projwin_srs EPSG:4326 -projwin $(echo $E | awk '{print $1,$4,$3,$2}') $P/gti/mixed.gti.gpkg d3.tif; gdalinfo -checksum d3.tif | grep Checksum | tr '\n' ' '; echo " (expect 20175 15241)"
