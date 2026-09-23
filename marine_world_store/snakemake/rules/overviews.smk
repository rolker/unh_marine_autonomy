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

"""One checkpoint per level: prune, then list every parent the level below supports.

A level's parents cannot be enumerated until the level below it exists, because
a derived tile is itself a contributor to the next fold. That is what a
Snakemake `checkpoint` is for -- a DAG whose shape depends on a previous step's
output.

The enumeration is `build_depth_overview_parent --list-parents`, not Python
here: the parent/child mapping is GGGS, whose column counts vary by latitude
band, and a second implementation of that arithmetic in a workflow file would
be a second thing to get wrong with no tests on it. The listing names each
parent's CHILDREN too, which is what lets a parent's job take the child tiles
as its inputs without this file knowing which tiles those are.

Before listing, the same checkpoint prunes the level (`--prune`): a derived
tile that a native tile now covers, or whose children are all gone, is never
listed, so no job would ever be scheduled to remove it. The finest level's
checkpoint also removes every derived level outside the configured range
(`--remove-level`), which no prune would take: those tiles still have
children.

Every file a rule reads or writes is a real input or output -- never a stamp
standing in for one -- so Snakemake's mtime and input-set triggers see a
changed, added or vanished tile.
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


def _derived_levels_outside_range():
    """The levels ``overviews/`` holds tiles at that this run does not build."""
    built = set(_levels())
    found = set()
    if OVERVIEWS.is_dir():
        for path in OVERVIEWS.glob("*_*_*.tif"):
            if _TILE_NAME.fullmatch(path.name):
                found.add(int(path.name.split("_", 1)[0]))
    return sorted(found - built)


def _listing(level):
    """``{parent_name: [child paths]}`` from level ``level``'s checkpoint."""
    listing = checkpoints.list_parents.get(level=level).output[0]
    parents = {}
    with open(listing) as handle:
        for line in handle:
            fields = line.rstrip("\n").split("\t")
            if fields and fields[0]:
                parents[fields[0]] = [LAYER_DIR / child for child in fields[1:]]
    return parents


def parent_tiles(level):
    """The derived tiles the DAG builds at ``level`` (its jobs' outputs)."""
    return [OVERVIEWS / f"{name}.tif" for name in _listing(level)]


def _list_inputs(wildcards):
    """
    What a level's listing depends on: the tiles that can change it.

    The native tiles at the child level and at the level itself (a native
    tile arriving at a parent's index removes that parent), and the derived
    tiles the level below produced. A tile added or removed changes this SET,
    which reruns the listing; one rewritten reruns it by mtime.
    """
    level = int(wildcards.level)
    inputs = native_tiles(level + 1) + native_tiles(level)
    if level < int(FINE_LEVEL) - 1:
        # Tiles AND records: a missing one is rebuilt only if asked for.
        inputs += [path for tile in parent_tiles(level + 1)
                   for path in (tile, tile.with_suffix(".json"))]
    return inputs


checkpoint list_parents:
    """Prune, then enumerate the parents at one level, with their children."""
    input:
        _list_inputs,
    output:
        WORK / "parents_{level}.tsv",
    params:
        layer=str(LAYER_DIR),
        tool=OVERVIEW_TOOL,
        # Derived levels this run does not build: at or finer than
        # fine_level (nothing native lies below them), or coarser than
        # min_level. Removed with the first (finest) listing.
        remove_levels=lambda wildcards: " ".join(
            str(level) for level in _derived_levels_outside_range()
        ) if int(wildcards.level) == int(FINE_LEVEL) - 1 else "",
    shell:
        "for stale in {params.remove_levels}; do "
        "{params.tool:q} --remove-level {params.layer:q} $stale || exit 1; "
        "done; "
        "{params.tool:q} --prune {params.layer:q} {wildcards.level} && "
        "{params.tool:q} --list-parents {params.layer:q} {wildcards.level} "
        "> {output:q}"


def _children(wildcards):
    """The child tiles one parent folds, from its level's listing."""
    name = f"{wildcards.level}_{wildcards.row}_{wildcards.col}"
    children = _listing(int(wildcards.level)).get(name)
    if children is None:
        raise WorkflowError(
            f"{name} is not a parent the listing for level {wildcards.level} "
            "schedules")
    return children


rule build_parent:
    """Fold ONE parent tile. Per-tile atomic, so `-j` may run many at once."""
    input:
        _children,
    output:
        OVERVIEWS / "{level}_{row}_{col}.tif",
        OVERVIEWS / "{level}_{row}_{col}.json",
    params:
        layer=str(LAYER_DIR),
        tool=OVERVIEW_TOOL,
        record=REFRESH_TOOL,
    shell:
        # Recorded as built, with the mtime the build gave it (see
        # fingerprint_sidecar): the next pre-step then leaves it newer than
        # the children it absorbed, even if it came out byte for byte the same.
        "{params.tool:q} {params.layer:q} "
        "{wildcards.level} {wildcards.row} {wildcards.col} && "
        "{params.record:q} --record {output[0]:q}"


def overview_tiles(wildcards=None):
    """Every derived tile the DAG builds, every level."""
    return [tile for level in _levels() for tile in parent_tiles(level)]


def overview_products(wildcards=None):
    """
    Every derived tile AND its per-tile record, every level.

    Both, because a job's output is only rebuilt when something asks for it:
    with the tiles alone as the manifest's inputs, a deleted record was never
    requested, never rebuilt, and the catalog then refused the tile as having
    no lineage.
    """
    return [path for tile in overview_tiles()
            for path in (tile, tile.with_suffix(".json"))]


rule overview_products_present:
    """
    Ask for every derived tile and record, so a missing one is rebuilt.

    Snakemake rebuilds a job's missing output only when a job it is PLANNING
    asks for it. Each level's listing asks for the level below's products,
    but nothing asks for the coarsest level's except the manifest, and a job
    whose own output already exists is not re-planned after a checkpoint.
    This one's output is deleted when the Snakefile loads, like the listings,
    so it is planned every run.
    """
    input:
        overview_products,
    output:
        touch(WORK / "products.done"),
