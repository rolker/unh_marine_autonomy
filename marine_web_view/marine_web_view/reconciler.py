#!/usr/bin/env python3

# Copyright 2026 Roland Arsenault
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
#    * Neither the name of the Roland Arsenault nor the names of its
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

"""Anti-entropy reconciliation of a GGGS tile cache against a catalog.

Python port of `marine_tiled_raster_store::TileCatalogReconciler`
(`include/marine_tiled_raster_store/tile_catalog.hpp`, `src/tile_catalog.cpp`),
kept ROS-free so it is testable without a node. See project ADR-0008.

The consumer converges its cache to exactly the set a source advertises:
request what is missing or stale, prune what the source no longer holds. Two
rules make that safe, and both are easy to get subtly wrong:

* **Newest-wins** (D3) -- a reordered older arrival must not lower the held
  version, or a stale tile would overwrite a fresh one.
* **Timestamp-gated prune** (D4b) -- a held tile absent from the catalog is
  pruned only if its version predates the catalog's generation time, so a late
  or reordered catalog cannot delete a tile pushed after it was generated.

Note the gate's consequence at generation time zero: `version < generation`
is never true, so pruning disables itself entirely for an unstamped catalog.
That is the desired behaviour under simulated time starting at zero -- better
to keep a tile the source may still hold than to delete on no evidence -- but
it is emergent from the comparison rather than an explicit branch, so it is
pinned by a test.

Completeness is a precondition: prune-on-absence is only valid against a
*complete* catalog snapshot (D4a). A partial or paged catalog would read as
"the source dropped everything I cannot see".
"""

from marine_web_view import gggs


def is_valid_index(index):
    """Return True if (level, row, column) is a real GGGS grid."""
    try:
        level, row, column = index
    except (TypeError, ValueError):
        return False
    if level < 0 or level > gggs.MAX_LEVEL or row < 0 or column < 0:
        return False
    if row >= gggs.row_count(level):
        return False
    return column < gggs.column_count(level, row)


class TileCatalogReconciler:
    """Track which tiles the consumer holds, and at which versions."""

    def __init__(self):
        """Start with an empty cache."""
        self._cache = {}

    def mark_have(self, index, version):
        """Record that the consumer holds index at version.

        Newest-wins: an older version is ignored, so a reordered stale arrival
        cannot lower the held version. Calling this after drop() re-adds the
        tile, which is intentional -- a re-received tile returns. Invalid
        indices are ignored rather than collapsing to a single key.

        Cache growth is the caller's responsibility, exactly as in the C++.
        """
        if not is_valid_index(index):
            return
        key = tuple(index)
        held = self._cache.get(key)
        if held is None or version > held:
            self._cache[key] = version

    def drop(self, index):
        """Forget index (the consumer deleted it, e.g. after a prune)."""
        self._cache.pop(tuple(index), None)

    def has(self, index):
        """Return True if the consumer currently holds index."""
        return tuple(index) in self._cache

    def version_of(self, index):
        """Return the held version of index, or None if absent."""
        return self._cache.get(tuple(index))

    def __len__(self):
        """Return the number of tiles currently cached."""
        return len(self._cache)

    def reconcile(self, entries, generation):
        """Return (to_request, to_prune) against a complete catalog.

        Pure: does not mutate the reconciler. entries is an iterable of
        (index, version) pairs; generation is the catalog's generation time in
        the same units as the versions.
        """
        # A well-formed catalog has unique indices. If one repeats -- a
        # malformed source -- keep the newest so request/prune stay
        # conservative rather than oscillating on iteration order.
        catalog = {}
        for index, version in entries:
            if not is_valid_index(index):
                continue
            key = tuple(index)
            held = catalog.get(key)
            if held is None or version > held:
                catalog[key] = version

        to_request = [key for key, version in sorted(catalog.items())
                      if self._cache.get(key) is None
                      or self._cache[key] < version]

        to_prune = [key for key, version in sorted(self._cache.items())
                    if key not in catalog and version < generation]

        return to_request, to_prune
