# Copyright 2026 University of New Hampshire
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#
#    * Neither the name of the University of New Hampshire nor the names of its
#      contributors may be used to endorse or promote products derived from
#      this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

"""Regenerate the derived tile indexes from the Items.

Design section 7: STAC Items plus the Collection are the RECORD; a tile index
is *derived* from them for readers. A derived index is never synced between
replicas -- it is regenerated locally, which is why this rule exists at all
rather than the index being a product the store ships. The file list comes
from the Items' data assets (`mws_list_tiles`), not from globbing a directory.

One index per band schema, because a tile index describes one raster: the
native tiles (2-band value/sigma) at ``<layer>/index.gti.fgb``, the derived
overview tiles (4-band MIN/MEAN/COUNT/sigma) at
``<layer>/overviews/index.gti.fgb``. An earlier version indexed only the
overviews, by glob, and called that "the Collection".

**GDAL version.** Reading a ``*.gti.fgb`` file AS A RASTER is GDAL's GTI driver,
which is GDAL >= 3.9. WRITING the index is not: the index is the ordinary
`gdaltindex` vector tile index (a ``location`` field per tile), which every
GDAL writes and the GTI driver opens directly by that extension. So the rule
uses only the `gdaltindex` options every supported GDAL has, and runs on this
repo's GDAL 3.8.4 hosts too -- rather than failing ``rule all`` there, or
dropping the output. On a host older than 3.9 the Snakefile says so once per
run: the index is written and correct, but on that host it serves as a vector
tile index (``gdalbuildvrt -tileindex``, MapServer, QGIS), not as a GTI raster.
The newer `gdaltindex` options that would record GTI metadata in the index
(``-gti_filename``, ``-tr``, ...) are deliberately not used: the GTI driver
reads resolution and band layout from the tiles themselves.

The index is written beside and renamed over the previous one (`gdaltindex`
appends to an existing file, and a reader must never see a half-written one),
with absolute paths: it is a host-local file that is never synced. A layer
with no tiles of a kind has its stale index of that kind removed.
"""


def _gdal_version():
    """``(major, minor)`` of the host's GDAL, or ``None`` if not found."""
    try:
        text = subprocess.run(
            ["gdalinfo", "--version"], capture_output=True, text=True,
            check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    match = re.search(r"GDAL (\d+)\.(\d+)", text)
    return (int(match.group(1)), int(match.group(2))) if match else None


GDAL_VERSION = _gdal_version()
if GDAL_VERSION is None:
    raise WorkflowError(
        "gdalinfo not found: the tile indexes are written with gdaltindex "
        "(GDAL, a dependency of marine_bathymetry_store)")
if GDAL_VERSION < (3, 9):
    print(f"note: GDAL {GDAL_VERSION[0]}.{GDAL_VERSION[1]} < 3.9: the tile "
          "indexes are written and correct, but opening them as rasters "
          "(the GTI driver) needs GDAL >= 3.9; on this host they are vector "
          "tile indexes", file=sys.stderr)


def _gti_rule_shell():
    """One shell body for both kinds (see the module docstring)."""
    return (
        "{params.list_tool:q} {params.layer:q} --kind {params.kind} "
        "--optfile > {params.listing:q}; "
        "rm -f {params.tmp:q}; "
        "if [ -s {params.listing:q} ]; then "
        "gdaltindex -f FlatGeobuf -lyr_name tiles -write_absolute_path "
        "{params.tmp:q} "
        "--optfile {params.listing:q} && mv {params.tmp:q} {params.index:q}; "
        "else rm -f {params.index:q}; fi"
    )


rule gti_native:
    input:
        WORK / "catalog.done",
    output:
        touch(WORK / "gti_native.done"),
    params:
        layer=str(LAYER_DIR),
        kind="native",
        list_tool=LIST_TILES_TOOL,
        listing=str(WORK / "gti_native.lst"),
        tmp=str(WORK / "index-native.tmp.fgb"),
        index=str(LAYER_DIR / "index.gti.fgb"),
    shell:
        _gti_rule_shell()


rule gti_overview:
    input:
        WORK / "catalog.done",
    output:
        touch(WORK / "gti_overview.done"),
    params:
        layer=str(LAYER_DIR),
        kind="overview",
        list_tool=LIST_TILES_TOOL,
        listing=str(WORK / "gti_overview.lst"),
        tmp=str(WORK / "index-overview.tmp.fgb"),
        index=str(OVERVIEWS / "index.gti.fgb"),
    shell:
        _gti_rule_shell()
