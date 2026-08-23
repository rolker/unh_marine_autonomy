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

"""Pin the anti-entropy reconciliation contract (project ADR-0008).

These are the failure modes ADR-0008 exists to prevent, and they are all
silent: a stale tile overwriting a fresh one, or a late catalog deleting a
tile that was pushed after it was generated. Neither shows up as an error --
the display just goes quietly wrong. So the rules are pinned here rather than
trusted to review.

Mirrors the C++ tests in
marine_tiled_raster_store/test/test_tile_catalog.cpp.
"""

from marine_web_view.reconciler import is_valid_index, TileCatalogReconciler

# A real level-10 index observed on the simulator's coverage_catalog.
A = (10, 17801, 13988)
B = (10, 17801, 13989)
C = (10, 17802, 13989)


def test_mark_have_is_newest_wins():
    """A reordered older arrival must not lower the held version."""
    r = TileCatalogReconciler()
    r.mark_have(A, 100)
    r.mark_have(A, 50)
    assert r.version_of(A) == 100
    r.mark_have(A, 150)
    assert r.version_of(A) == 150


def test_drop_then_mark_have_re_adds():
    """A re-received tile returns after a prune -- intentional."""
    r = TileCatalogReconciler()
    r.mark_have(A, 100)
    r.drop(A)
    assert not r.has(A)
    r.mark_have(A, 100)
    assert r.has(A)


def test_invalid_indices_are_ignored():
    """Invalid indices must not enter the cache.

    In the C++ every invalid index collapses onto one map key, which would
    make them alias each other; here they are rejected outright.
    """
    r = TileCatalogReconciler()
    for bad in ((10, -1, 0), (10, 0, -1), (10, 10 ** 9, 0), (99, 0, 0),
                'nonsense', (1, 2)):
        assert not is_valid_index(bad)
        r.mark_have(bad, 100)
    assert len(r) == 0


def test_requests_missing_and_stale():
    """Request what we lack or hold at an older version -- and nothing else."""
    r = TileCatalogReconciler()
    r.mark_have(A, 100)
    r.mark_have(B, 100)
    to_request, _ = r.reconcile(
        [(A, 100), (B, 200), (C, 100)], generation=300)
    assert A not in to_request      # held at the catalog version
    assert B in to_request          # held, but stale
    assert C in to_request          # missing


def test_prune_is_timestamp_gated():
    """A late catalog must not delete a tile pushed after it was generated."""
    r = TileCatalogReconciler()
    r.mark_have(A, 500)             # pushed at 500
    _, to_prune = r.reconcile([], generation=400)
    assert to_prune == [], 'a catalog generated at 400 deleted a tile from 500'
    _, to_prune = r.reconcile([], generation=600)
    assert to_prune == [A], 'a tile absent from a newer catalog was not pruned'


def test_generation_zero_disables_pruning():
    """An unstamped catalog must prune nothing.

    Emergent from the gate rather than an explicit branch: `version <
    generation` is never true at generation 0. This is the sim-time-zero case
    -- better to keep a tile the source may still hold than delete on no
    evidence -- so it is pinned lest an optimisation "simplify" the gate away.
    """
    r = TileCatalogReconciler()
    r.mark_have(A, 0)
    r.mark_have(B, 5)
    _, to_prune = r.reconcile([], generation=0)
    assert to_prune == []


def test_reconcile_does_not_mutate():
    """reconcile() is pure -- the caller applies the result."""
    r = TileCatalogReconciler()
    r.mark_have(A, 100)
    before = len(r)
    r.reconcile([(B, 200)], generation=300)
    assert len(r) == before
    assert r.has(A) and not r.has(B)


def test_duplicate_catalog_entries_keep_newest():
    """A malformed catalog repeating an index must not oscillate."""
    r = TileCatalogReconciler()
    r.mark_have(A, 150)
    to_request, _ = r.reconcile([(A, 100), (A, 200)], generation=300)
    assert to_request == [A]
    to_request, _ = r.reconcile([(A, 200), (A, 100)], generation=300)
    assert to_request == [A], 'result depended on entry order'


def test_empty_catalog_prunes_everything_old_enough():
    """A source that holds nothing must empty the cache, subject to the gate."""
    r = TileCatalogReconciler()
    r.mark_have(A, 100)
    r.mark_have(B, 100)
    _, to_prune = r.reconcile([], generation=200)
    assert sorted(to_prune) == sorted([A, B])
