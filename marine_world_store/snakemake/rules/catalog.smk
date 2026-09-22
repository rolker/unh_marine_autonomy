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

`regenerate_catalog` writes ONLY changed Items -- design section 9's replica
rule, so an unchanged store leaves every file's bytes and mtime alone and a
replica sync moves nothing.
"""


rule assemble_coverage:
    input:
        WORK / "overviews.done",
    output:
        touch(WORK / "coverage.done"),
    params:
        layer=lambda wildcards: str(LAYER_DIR),
    shell:
        "mws_assemble_coverage {params.layer}"


rule catalog:
    input:
        WORK / "coverage.done",
    output:
        touch(WORK / "catalog.done"),
    shell:
        "mws_regenerate_catalog --quantity depths"
