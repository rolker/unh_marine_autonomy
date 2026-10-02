#!/usr/bin/env python3
"""Synthesize GGGS level-10 tiles in the polar coalescing bands (75N: x3 wide, 85N: x9 wide), named level_ROW_COL like the store."""
import sys, subprocess, os
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
lvl = 10; m = 1 << lvl; span = 8.0 / m
for lat, lon, coal, val in ((75.0, -70.0, 3, -75.0), (85.0, -70.0, 9, -85.0), (38.5, -75.0, 1, -38.5)):
    row = int((lat + 96.0) / span); col = int((lon + 180.0) / (span * coal))
    s, w = -96.0 + row * span, -180.0 + col * span * coal
    name = f"{out}/{lvl}_{row}_{col}.tif"
    subprocess.run(["gdal_create", "-q", "-of", "GTiff", "-outsize", "960", "960", "-bands", "2", "-ot", "Float64",
                    "-a_srs", "EPSG:4326", "-a_ullr", str(w), str(s + span), str(w + span * coal), str(s),
                    "-a_nodata", "nan", "-burn", str(val), "-burn", "0.5", "-co", "TILED=YES", "-co", "COMPRESS=DEFLATE", name], check=True)
    print(name, "lon span", span * coal, "coalesce", coal)
