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

"""One checkpoint per level: fold every parent the level below now supports.

A level's parents cannot be enumerated until the level below it exists, because
a derived tile is itself a contributor to the next fold. That is what a
Snakemake `checkpoint` is for -- a DAG whose shape depends on a previous step's
output.

The enumeration itself is `build_depth_overview_parent --list-parents`, not
Python here: the parent/child mapping is GGGS, whose column counts vary by
latitude band, and a second implementation of that arithmetic in a workflow
file would be a second thing to get wrong with no tests on it. The same CLI
does the folding, one parent per invocation, so `-j` parallelises the level.

Native-wins is handled on the CLI side and needs no rule: a parent the compile
already covers is not listed and, if it were invoked anyway, writes nothing.
"""


def _levels():
    """Parent levels to build, finest first. Requires `fine_level`."""
    if FINE_LEVEL is None:
        raise WorkflowError(
            "fine_level is required to build overviews: --config "
            "fine_level=<the layer's finest native level>. "
            "`build_depth_overviews --dry-run <layer>` reports it."
        )
    return list(range(int(FINE_LEVEL) - 1, MIN_LEVEL - 1, -1))


checkpoint list_parents:
    """Enumerate the parents at one level, once the level below is built."""
    input:
        # Level fine-1 folds native tiles, so it waits only on the pre-step;
        # every coarser level waits on the level below it having been built.
        lambda wildcards: (
            [WORK / "fingerprints.done"]
            if int(wildcards.level) == int(FINE_LEVEL) - 1
            else [WORK / f"level_{int(wildcards.level) + 1}.done"]
        ),
    output:
        WORK / "parents_{level}.txt",
    params:
        layer=lambda wildcards: str(LAYER_DIR),
    shell:
        "build_depth_overview_parent --list-parents {params.layer} "
        "{wildcards.level} > {output}"


rule build_parent:
    """Fold ONE parent tile. Per-tile atomic, so `-j` may run many at once."""
    input:
        WORK / "parents_{level}.txt",
    output:
        touch(WORK / "parent_{level}_{row}_{col}.done"),
    params:
        layer=lambda wildcards: str(LAYER_DIR),
    shell:
        "build_depth_overview_parent {params.layer} "
        "{wildcards.level} {wildcards.row} {wildcards.col}"


def parents_at(level):
    """The parent stamps for `level`, read from its checkpoint's output."""
    listing = checkpoints.list_parents.get(level=level).output[0]
    names = [line.strip() for line in open(listing) if line.strip()]
    return [WORK / f"parent_{name}.done" for name in names]


rule level_done:
    """A level is done when every parent it listed has been folded."""
    input:
        lambda wildcards: parents_at(wildcards.level),
    output:
        touch(WORK / "level_{level}.done"),


rule overviews_done:
    """Every level, coarsest last."""
    input:
        lambda wildcards: [WORK / f"level_{level}.done" for level in _levels()],
    output:
        touch(WORK / "overviews.done"),
