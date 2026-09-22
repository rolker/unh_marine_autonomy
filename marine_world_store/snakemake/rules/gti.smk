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

"""Regenerate the derived GTI index from the Collection.

Design section 7: STAC Items plus the Collection are the RECORD; a GTI index is
*derived* from them for readers. A derived index is never synced between
replicas -- it is regenerated locally, which is why this rule exists at all
rather than the index being a product the store ships.

GDAL >= 3.9 is what reads a GTI. The rule shells out to `gdal` rather than
building the index in Python so that the tool that will read it is the tool
that wrote it.
"""


rule gti:
    input:
        WORK / "catalog.done",
    output:
        touch(WORK / "gti.done"),
    params:
        overviews=lambda wildcards: str(OVERVIEWS),
        index=lambda wildcards: str(OVERVIEWS / "index.gti.fgb"),
    shell:
        # `gdaltindex -gti_filename` writes the GTI GeoPackage/FlatGeobuf the
        # readers open. Regenerated wholesale every time: the index is derived,
        # so there is nothing in it worth merging into.
        "gdaltindex -f FlatGeobuf -gti_filename {params.index} -overwrite "
        "{params.overviews}/*.tif"
