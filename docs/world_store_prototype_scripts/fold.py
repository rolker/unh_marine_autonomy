#!/usr/bin/env python3
"""Stand-in per-parent fold: parent tile = 2x2 block mean of its (up to 4) children, NaN-aware (R11: representative, not shoalest)."""
import sys, re, os, numpy as np
from osgeo import gdal
gdal.UseExceptions()
out = sys.argv[1]; children = sys.argv[2:]
l, r, c = map(int, re.match(r"(\d+)_(\d+)_(\d+)\.tif", os.path.basename(out)).groups())
span = 8.0 / (1 << l); n = 960; acc = np.full((2, n, n), np.nan)
for ch in children:
    cl, cr, cc = map(int, re.match(r"(\d+)_(\d+)_(\d+)\.tif", os.path.basename(ch)).groups())
    a = gdal.Open(ch).ReadAsArray()                       # (2, 960, 960) north-up
    f = np.nanmean(a.reshape(2, 480, 2, 480, 2), axis=(2, 4)) if False else np.stack([np.nanmean(b.reshape(480, 2, 480, 2), axis=(1, 3)) for b in a])
    dy = 1 - (cr - 2 * r); dx = cc - 2 * c                # child row 2r is the SOUTH half -> lower half of the north-up raster
    acc[:, dy * 480:(dy + 1) * 480, dx * 480:(dx + 1) * 480] = f
drv = gdal.GetDriverByName("GTiff"); tmp = out + ".tmp.tif"
ds = drv.Create(tmp, n, n, 2, gdal.GDT_Float64, ["TILED=YES", "COMPRESS=DEFLATE", "PREDICTOR=3"])
ds.SetGeoTransform([-180 + c * span, span / n, 0, -96 + (r + 1) * span, 0, -span / n]); ds.SetProjection("EPSG:4326")
for i in range(2): ds.GetRasterBand(i + 1).WriteArray(acc[i]); ds.GetRasterBand(i + 1).SetNoDataValue(float("nan"))
ds = None; gdal.Translate(out, tmp, format="COG", creationOptions=["COMPRESS=DEFLATE", "PREDICTOR=3"]); os.remove(tmp)
