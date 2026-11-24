#!/bin/bash
P=$1; S=$P/stacta/depths_processed.stacta.json; cd /tmp
ext() { gdalinfo "$1" | python3 -c "import sys,re; t=sys.stdin.read(); ul=re.search(r'Upper Left\s*\(\s*([-\d.]+),\s*([-\d.]+)',t); lr=re.search(r'Lower Right\s*\(\s*([-\d.]+),\s*([-\d.]+)',t); print(ul[1],lr[2],lr[1],ul[2])"; }
for t in 10_21888_4693 10_23168_1564; do E=$(ext $P/depths/polar_synth/$t.tif); gdal_translate -q -projwin_srs EPSG:4326 -projwin $(echo $E | awk '{print $1,$4,$3,$2}') $S sp.tif 2>&1 | grep -v -i warning | head -1; printf '   %s: ' $t; gdalinfo -stats sp.tif 2>/dev/null | grep -E 'Size is|STATISTICS_(MINIMUM|MAXIMUM|VALID_PERCENT)' | head -4 | tr '\n' ' ' | sed 's/  */ /g'; echo; done
echo "-- sparse coverage: zoomed-out read of the whole extent (touches absent tiles)"; gdal_translate -q -outsize 1000 0 $S sz.tif 2>&1 | grep -c -i 'cannot open' | sed 's/^/   errors: /'; gdalinfo -stats sz.tif 2>/dev/null | grep -E 'STATISTICS_VALID_PERCENT' | head -1
