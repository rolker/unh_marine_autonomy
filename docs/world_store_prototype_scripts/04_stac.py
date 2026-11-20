#!/usr/bin/env python3
"""Component 4: STAC as catalog + coverage manifest + fingerprint container for one rung.
One Item per tile (proj + file + processing + version extensions, plus worldstore:* for GGGS addressing),
one Collection per rung carrying the build fingerprint. Usage: 04_stac.py <cog_dir> <extra_dir> <out_dir>"""
import sys, os, glob, re, json, hashlib, datetime
import pystac
from osgeo import gdal
cog, extra, out = sys.argv[1:4]; os.makedirs(out, exist_ok=True)
gdal.UseExceptions()
def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""): h.update(b)
    return "1220" + h.hexdigest()   # multihash prefix for sha2-256, as the file extension requires
coll = pystac.Collection(id="depths-processed", description="GGGS depth tiles, processed rung (prototype)",
    extent=pystac.Extent(pystac.SpatialExtent([[-180, -96, 180, 96]]), pystac.TemporalExtent([[None, None]])),
    license="proprietary")
coll.stac_extensions = ["https://stac-extensions.github.io/processing/v1.2.0/schema.json"]
coll.extra_fields["worldstore:quantity"] = "depths"; coll.extra_fields["worldstore:rung"] = "processed"
coll.extra_fields["worldstore:tms"] = "GGGS"
inputs = []
for f in sorted(glob.glob(f"{cog}/*.tif") + glob.glob(f"{extra}/*.tif")):
    l, r, c = map(int, re.match(r"(\d+)_(\d+)_(\d+)\.tif", os.path.basename(f)).groups())
    ds = gdal.Open(f); gt = ds.GetGeoTransform(); w, h = ds.RasterXSize, ds.RasterYSize
    x0, y1, x1, y0 = gt[0], gt[3], gt[0] + w * gt[1], gt[3] + h * gt[5]
    st = os.stat(f); chk = sha256(f); inputs.append(chk)
    item = pystac.Item(id=f"{l}_{r}_{c}", geometry={"type": "Polygon", "coordinates": [[[x0, y0], [x1, y0], [x1, y1], [x0, y1], [x0, y0]]]},
        bbox=[x0, y0, x1, y1], datetime=datetime.datetime.fromtimestamp(st.st_mtime, datetime.timezone.utc),
        properties={"worldstore:level": l, "worldstore:row": r, "worldstore:col": c, "worldstore:native": l == 10,
                    "proj:epsg": 4326, "proj:shape": [h, w], "proj:transform": [gt[1], gt[2], gt[0], gt[4], gt[5], gt[3]],  # STAC uses affine order (a b c d e f), not GDAL geotransform order
                    "processing:software": {"marine_tiled_raster_store": "prototype"}, "processing:level": "L2" if l == 10 else "L3",
                    "version": "1"})
    item.stac_extensions = ["https://stac-extensions.github.io/projection/v1.1.0/schema.json",
        "https://stac-extensions.github.io/file/v2.1.0/schema.json",
        "https://stac-extensions.github.io/processing/v1.2.0/schema.json",
        "https://stac-extensions.github.io/version/v1.2.0/schema.json"]
    item.add_asset("data", pystac.Asset(href=os.path.relpath(f, out), media_type="image/tiff; application=geotiff; profile=cloud-optimized",
        roles=["data"], extra_fields={"file:checksum": chk, "file:size": st.st_size, "raster:bands": [{"name": "depth", "unit": "m"}, {"name": "uncertainty", "unit": "m"}]}))
    coll.add_item(item)
fp = hashlib.sha256("\n".join(sorted(inputs)).encode()).hexdigest()
coll.extra_fields["worldstore:fingerprint"] = {"inputs_sha256": fp, "n_tiles": len(inputs), "builder": "prototype"}
coll.normalize_hrefs(out); coll.save(catalog_type=pystac.CatalogType.SELF_CONTAINED)
# also one flat ItemCollection (what GDAL STACIT and a GTI generator read in one go)
fc = {"type": "FeatureCollection", "features": [i.to_dict(include_self_link=False) for i in coll.get_all_items()]}
json.dump(fc, open(f"{out}/items.json", "w"))
print("items:", len(inputs), "fingerprint:", fp[:16], "catalog size:", sum(os.path.getsize(p) for p in glob.glob(f"{out}/**/*.json", recursive=True)) // 1024, "KiB")
