#!/usr/bin/env python3
"""Derive per-level GTI index layers from a STAC ItemCollection (no gdaltindex): STAC is the source of truth, GTI the reader view.
Usage: 04_stac_to_gti.py <items.json> <out_dir> (paths in items are relative to the items.json directory)"""
import json, os, sys, math
from osgeo import ogr, osr, gdal
gdal.UseExceptions()
items, out = sys.argv[1:3]; base = os.path.dirname(os.path.abspath(items)); os.makedirs(out, exist_ok=True)
fc = json.load(open(items)); by_level = {}
for it in fc["features"]: by_level.setdefault(it["properties"]["worldstore:level"], []).append(it)
srs = osr.SpatialReference(); srs.ImportFromEPSG(4326); srs.SetAxisMappingStrategy(osr.OAMS_TRADITIONAL_GIS_ORDER)
# common extent snapped to level-0 grids, from the native level
xs = [b for it in by_level[10] for b in (it["bbox"][0], it["bbox"][2])]; ys = [b for it in by_level[10] for b in (it["bbox"][1], it["bbox"][3])]
f = lambda v, o: math.floor((v - o) / 8) * 8 + o; c = lambda v, o: math.ceil((v - o) / 8) * 8 + o
te = (f(min(xs), -180), f(min(ys), -96), c(max(xs), -180), c(max(ys), -96))
drv = ogr.GetDriverByName("GPKG")
for lvl, its in sorted(by_level.items()):
    path = f"{out}/level{lvl}.gti.gpkg"; os.path.exists(path) and drv.DeleteDataSource(path)
    ds = drv.CreateDataSource(path); lyr = ds.CreateLayer("tiles", srs, ogr.wkbPolygon)
    lyr.CreateField(ogr.FieldDefn("location", ogr.OFTString)); lyr.CreateField(ogr.FieldDefn("checksum", ogr.OFTString))
    res = 8.0 / (1 << lvl) / 960
    for k, v in {"RESX": res, "RESY": res, "MINX": te[0], "MINY": te[1], "MAXX": te[2], "MAXY": te[3],
                 "BAND_COUNT": 2, "DATA_TYPE": "Float64", "NODATA": "nan", "LOCATION_FIELD": "location"}.items():
        lyr.SetMetadataItem(k, str(v))
    for it in its:
        ft = ogr.Feature(lyr.GetLayerDefn()); ft.SetGeometry(ogr.CreateGeometryFromJson(json.dumps(it["geometry"])))
        ft["location"] = os.path.abspath(os.path.join(base, it["assets"]["data"]["href"])); ft["checksum"] = it["assets"]["data"]["file:checksum"]; lyr.CreateFeature(ft)
    ds = None
with open(f"{out}/depths_processed.gti", "w") as g:
    g.write(f"<GDALTileIndexDataset><IndexDataset>{out}/level10.gti.gpkg</IndexDataset><IndexLayer>tiles</IndexLayer>\n")
    for lvl in range(9, -1, -1): g.write(f"  <Overview><Dataset>{out}/level{lvl}.gti.gpkg</Dataset><Layer>tiles</Layer></Overview>\n")
    g.write("</GDALTileIndexDataset>\n")
print("levels:", sorted(by_level), "extent:", te)
