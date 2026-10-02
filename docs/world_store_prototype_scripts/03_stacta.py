#!/usr/bin/env python3
"""Component 3b: STACTA (STAC tiled assets) manifest over a rung. Builds a symlink tree in TMS numbering
(STACTA templates use {TileMatrix}/{TileRow}/{TileCol}; TMS rows count from +96 south, GGGS rows from -96 north)
and writes one STAC item embedding the full GGGS TMS. Usage: 03_stacta.py <tms.json> <out_dir> <tile_dir>..."""
import json, os, sys, glob, re
tms = json.load(open(sys.argv[1])); out = sys.argv[2]; dirs = sys.argv[3:]
tree = os.path.join(out, "tiles"); os.makedirs(tree, exist_ok=True)
limits = {}
for d in dirs:
    for f in glob.glob(os.path.join(d, "*.tif")):
        l, r, c = map(int, re.match(r"(\d+)_(\d+)_(\d+)\.tif", os.path.basename(f)).groups())
        rows = 24 << l; tr = rows - 1 - r
        span = 8.0 / (1 << l); lat_s = -96.0 + r * span; a = abs(lat_s + span / 2)
        coal = 9 if a >= 80 else 3 if a >= 72 else 1     # TMS 2.0 variable-width rows: tile columns keep un-coalesced numbering, stepping by 'coalesce'
        c = c * coal
        dst = os.path.join(tree, str(l), str(tr)); os.makedirs(dst, exist_ok=True)
        link = os.path.join(dst, f"{c}.tif")
        if not os.path.lexists(link): os.symlink(os.path.relpath(os.path.abspath(f), dst), link)
        lim = limits.setdefault(str(l), {"min_tile_row": tr, "max_tile_row": tr, "min_tile_col": c, "max_tile_col": c})
        lim["min_tile_row"] = min(lim["min_tile_row"], tr); lim["max_tile_row"] = max(lim["max_tile_row"], tr)
        lim["min_tile_col"] = min(lim["min_tile_col"], c); lim["max_tile_col"] = max(lim["max_tile_col"], c)
item = {"type": "Feature", "stac_version": "1.0.0",
  "stac_extensions": ["https://stac-extensions.github.io/tiled-assets/v1.0.0/schema.json"],
  "id": "depths-processed", "geometry": None, "bbox": [-180, -96, 180, 96],
  "properties": {"datetime": "2026-09-18T00:00:00Z",
    "tiles:tile_matrix_sets": {"GGGS": {**tms, "tileMatrices": [m for m in tms["tileMatrices"] if m["id"] in limits]}},  # only levels present: GDAL otherwise sizes the raster at the finest TMS level
    "tiles:tile_matrix_links": {"GGGS": {"url": "#GGGS", "limits": limits}}},
  "asset_templates": {"tiles": {"href": "tiles/{TileMatrix}/{TileRow}/{TileCol}.tif", "type": "image/tiff; application=geotiff", "roles": ["data"]}},
  "assets": {}, "links": []}
json.dump(item, open(os.path.join(out, "depths_processed.stacta.json"), "w"), indent=1)
print("levels indexed:", {k: (v["max_tile_row"]-v["min_tile_row"]+1, v["max_tile_col"]-v["min_tile_col"]+1) for k, v in sorted(limits.items(), key=lambda kv: int(kv[0]))})
