#!/usr/bin/env python3
"""Component 2: write GGGS as an OGC TileMatrixSet 2.0 JSON, from the constants in marine_autonomy/gggs/core.h."""
import json, sys
ORIGIN = [-180.0, 96.0]          # lon, lat: latitude range is -96..+96 (24 rows x 8 deg)
L0_COLS, L0_ROWS, SPAN0, PX = 45, 24, 8.0, 960
M_PER_DEG = 111319.4907932736    # OGC: metres per degree at the equator on WGS84
tms = {
  "id": "GGGS", "title": "Global Geographic Grid System (Ware & Mayer 2020) as implemented in marine_autonomy/gggs",
  "description": "8-degree level-0 grids of 960x960 cells from origin (-180, +96); rows 72-80 deg coalesce 3 columns, beyond 80 deg coalesce 9. GGGS file names are level_ROW_COL with ROW counted from -96 northward; TMS rows count from +96 southward: tms_row = matrixHeight-1-gggs_row.",
  "crs": "http://www.opengis.net/def/crs/OGC/1.3/CRS84", "orderedAxes": ["Lon", "Lat"],
  "tileMatrices": []}
for lvl in range(21):
    m = 1 << lvl; span = SPAN0 / m; cell = span / PX
    rows, cols = L0_ROWS * m, L0_COLS * m
    r = lambda lat: int((lat + 96.0) / span)   # gggs row index of a latitude boundary
    # TMS rows (from north): band [72,80) south->north gggs rows [r(72), r(80)) => tms rows rows-1-... ; symmetric so use both hemispheres
    def tms_rows(lo_gggs, hi_gggs):  # half-open gggs row range -> inclusive tms range
        return rows - hi_gggs, rows - 1 - lo_gggs
    vmw = []
    for coal, (a, b) in ((9, (r(80), rows)), (3, (r(72), r(80))), (3, (r(-80), r(-72))), (9, (0, r(-80)))):
        lo, hi = tms_rows(a, b)
        vmw.append({"coalesce": coal, "minTileRow": lo, "maxTileRow": hi})
    tms["tileMatrices"].append({
        "id": str(lvl), "scaleDenominator": cell * M_PER_DEG / 0.00028, "cellSize": cell,
        "cornerOfOrigin": "topLeft", "pointOfOrigin": ORIGIN,
        "tileWidth": PX, "tileHeight": PX, "matrixWidth": cols, "matrixHeight": rows,
        "variableMatrixWidths": sorted(vmw, key=lambda d: d["minTileRow"])})
json.dump(tms, open(sys.argv[1], "w"), indent=1)
l10 = tms["tileMatrices"][10]; print("level 10 cellSize deg:", l10["cellSize"], "matrix", l10["matrixWidth"], "x", l10["matrixHeight"])
print("tile 10_17252_13419 -> tms row", l10["matrixHeight"]-1-17252, "col 13419")
