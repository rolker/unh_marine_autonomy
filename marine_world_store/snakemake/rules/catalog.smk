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

"""Assemble the coverage manifest, then rewrite only the changed Items.

Two steps, in this order, and both AFTER every parent is folded.

`assemble_coverage` is a single serialised step by design. The per-parent
writer leaves a per-tile record rather than touching a shared coverage.json,
because parallel writers would race over that one file, and the only lock that
would fix it is one that serialises the DAG back into the batch build the
per-parent mode exists to replace. uma-ADR-0013 D3 wants the manifest; this is
where it is written, once, from records each written by exactly one process.
Its inputs are the derived tiles themselves, so a rebuilt, added or pruned
tile reassembles it.

`catalog` builds the overview tiles' Items and writes ONLY changed ones --
design section 9's replica rule, so an unchanged store leaves every file's
bytes and mtime alone and a replica sync moves nothing. It runs over THIS
layer (`--layer-dir`): the first version ran `mws_regenerate_catalog` with no
root at all, which regenerated whatever tree $WORLD_STORE_ROOT, the config
file or the default named -- not the layer this DAG had just built.
"""


def native_items(wildcards=None):
    """
    The native tiles' Items in the layer: what the adapter/link step wrote.

    An Item file is ``<collection id>-<level>_<row>_<col>.json``; it is a
    NATIVE tile's when that tile is in the layer itself. The overview Items
    are this rule's own output, so they are not its input.
    """
    items = []
    for path in sorted(LAYER_DIR.glob("*-*_*_*.json")):
        tile = path.name.rsplit("-", 1)[-1][: -len(".json")] + ".tif"
        if (LAYER_DIR / tile).is_file():
            items.append(path)
    return items


rule assemble_coverage:
    input:
        overview_tiles,
    output:
        OVERVIEWS / "coverage.json",
    params:
        layer=str(LAYER_DIR),
        overviews=str(OVERVIEWS),
        tool=ASSEMBLE_TOOL,
    shell:
        "mkdir -p {params.overviews:q} && "
        "{params.tool:q} {params.layer:q}"


rule catalog:
    input:
        OVERVIEWS / "coverage.json",
        native_items,
    output:
        touch(WORK / "catalog.done"),
    params:
        layer=str(LAYER_DIR),
        tool=CATALOG_TOOL,
    shell:
        "{params.tool:q} --layer-dir {params.layer:q}"
