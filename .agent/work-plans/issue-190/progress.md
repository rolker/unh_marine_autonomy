# Progress — Issue #190 (ADR restructure: split backscatter store)

## Local Review (Pre-Push)

**Reviewer**: fresh-context sub-agent (dual-lens, docs-only)
**Scope**: `jazzy..HEAD`, three commits, docs-only in `docs/decisions/`
(new `0007-mbes-backscatter-store.md`; `0006` narrowed to sidescan; `0005` wording).
**Verdict**: `approved-with-suggestions`

This is an architecture-of-record change only — no code/build/static analysis.
Both lenses applied. The split is complete and internally consistent; the design
is sound and well-defended. All findings below are suggestions; **no must-fix**.

---

### Lens A — Internal coherence & completeness of the split

**Result: clean. No contradictions left by the split.**

- **No leftover "one store" claims.** Grep across `docs/` for `one backscatter
  store` / `single backscatter store` / `EM2040` returns only correctly-contextualized
  hits: the historical-note in ADR-0005 D1 (`the original wording put EM2040 and
  Garmin in a single backscatter store`), ADR-0006's amendment banner (describing
  what it *used* to claim), and ADR-0007's Context (`ADR-0006 was originally written
  as the single backscatter store`). The live decision text in all three now says
  *family of sibling stores*. ADR-0005 D1 axis-1 rewritten correctly.
- **EM2040 / M3 no longer land in the sidescan store.** ADR-0006 D8 and its Status
  banner now route `mbes-backscatter` to ADR-0007's store; ADR-0006 "ingests
  `sensor_class: sidescan` sources" only. ADR-0005 D3 sensor_class table lists
  `sidescan`, `mbes-backscatter`, `mbes-bathy` as distinct arbitration classes.
- **ADR-0007 ↔ ADR-0006 responsibilities are cleanly disjoint and the
  sibling/fusion relationship is stated symmetrically.** ADR-0006 D8: cross-class
  arbitration is "across the two sibling stores at the fusion / query — or
  central-server — layer ... *not* inside this store (ADR-0007)". ADR-0007 D8 says
  the mirror image: "Neither store arbitrates across the other's class internally
  (ADR-0006 D8 says the same from its side)." Both cite ADR-0005 D5/D7. Symmetric.
- **ADR-0005 D6 re-arbitration taxonomy now covers all three stores correctly.**
  Rewritten to three bullets: sidescan (slant Tier-1 → re-project, no bag reread),
  MBES single-tier (bags ARE the archive → re-run CUBE pass), bathy (no in-store
  archive → re-import). The closing sentence correctly narrows the *no-bag-reread*
  guarantee to the sidescan Tier-1 only. This is the strongest part of the edit.
- **No orphaned MBES text in narrowed ADR-0006.** Checked D11 (node topology:
  gabby/salmon/dev) and D12 (package placement / phasing) — both are sidescan-only;
  no M3/MBES/EM2040/Norbit leftovers (grep clean). Consequences "Positive" bullet
  re-scoped to "multi-platform (sidescan-class)". D2/D7 Tier-1/slant language all
  still coherent for a sidescan-only store.
- **Cross-references resolve.** ADR-0007 → ADR-0002/0005/0006 all exist; issues
  #180/#179/#178/#190/#147/#175 and cube#52/#15 all referenced consistently.
  Verified cube#52 = **CLOSED/merged** (matches ADR-0007's "(merged)" claim, line 18).
  cube#15 correctly described as "slope correction currently disabled". ADR-0002's
  existing inbound link to `0006-multi-platform-backscatter-store.md` still resolves
  (filename unchanged — see suggestion A1).

**Suggestions (Lens A):**

- **A1 (cosmetic).** The *file* is still named `0006-multi-platform-backscatter-store.md`
  while the *title* is now "ADR-0006: Sidescan Backscatter Store". The mismatch is
  harmless and arguably correct — renaming a committed ADR file breaks the stable
  URL/anchor that ADR-0002 (and external refs) link to, and ADR convention is to keep
  the original slug. The amendment banner already disambiguates. Recommend **leave as
  is**; optionally note in the banner that the filename slug is retained for link
  stability. Not a defect.
- **A2 (minor symmetry).** ADR-0007's Status line says "Sibling to **ADR-0006**";
  ADR-0006's *banner* names ADR-0007 but its Status header does not carry a reciprocal
  "Sibling to ADR-0007" line. Optional one-line addition for symmetry.

---

### Lens B — Architectural soundness (adversarial)

**Result: design is sound and the sharp edges are already acknowledged in-text.**

- **"Intensity rides the winning CUBE hypothesis as a Welford passenger" (D2) — sound,
  and the failure mode IS acknowledged.** Tying backscatter association to the depth
  data-association is defensible: the beams that contribute a node's depth are the
  beams whose footprints actually struck that ground cell, which is exactly the set a
  backscatter cell estimate needs. The named failure mode — **a beam that is a valid
  depth member but an intensity outlier** (e.g. a fish/kelp/transient specular return
  that is geometrically on-bottom but radiometrically anomalous; or a bad-AGC ping) —
  is real: depth-gating will *not* reject it, so it pollutes the Welford mean. ADR-0007
  handles this honestly: D2 explicitly declines a second W&H DLM on intensity (no
  per-beam obs-noise model for AGC dB → inverse-variance update would be false
  precision), and D4 routes the *spread* into the **dispersion / texture band** — which
  is exactly where an intensity outlier surfaces. So the ADR does not claim outlier
  rejection for intensity; it claims depth-consistent association and books the residual
  as dispersion. That is the correct disposition. **Suggestion B1**: D2's bullet
  "outlier rejection comes free and is consistent between the two products" slightly
  over-reads — it's free for *depth-geometry* outliers, not *intensity* outliers; worth
  one clause cross-pointing to the D4 dispersion band so the limit isn't lost.
- **"Deferred-settled angle correction" (D3) — sufficient stats suffice; slope
  correctly characterized as output-stage.** Carrying per-beam `{raw intensity, grazing
  angle}` (plus the already-present slant range / footprint geometry) on the hypothesis
  is exactly the GeoCoder input set: footprint-area, incidence/Lambert, and residual
  beam-pattern corrections are all functions of grazing/incidence angle + range +
  settled depth/slope. Deferring to node-output (once depth + neighbour-derived slope
  settle) is the right ordering — you cannot compute incidence live because slope isn't
  known until neighbouring nodes settle. The cube#15 slope dependency is correctly
  framed as an **output-stage** need ("the slope that feeds incidence-angle correction
  here is the same quantity #15 must provide — at output, not in the live insert"),
  which sidesteps re-enabling the disabled live `Node::insert` slope path. Architecturally
  clean. One residual: D3 keeps per-beam records on the hypothesis "until output" — for
  a long-dwell / high-overlap node that per-beam list is unbounded in principle
  (**Suggestion B2**: note a cap / reservoir, or that draft uses live-settled and only
  processed carries full per-beam — D7 already implies the latter; make it explicit).
- **"Single-tier; bags are the archive" (D1) — equivalent to sidescan Tier-1 for
  re-arbitration EXCEPT the cold-storage case, which is under-acknowledged.** For the
  depth-refine / re-arbitrate use case D1 IS equivalent: re-running the CUBE pass over
  the soundings bags regenerates both products against refined bathy, and the bags hold
  strictly more than a slant Tier-1 would. The case it loses is the one the prompt
  flags: **bags aged off to cold storage / deleted**. ADR-0006 D11 explicitly says its
  Tier-1 archive "lets bags move to cold storage" — i.e. sidescan can *discard the bags*
  and still re-arbitrate from Tier-1; the MBES store **cannot** — if the bags are gone,
  the single-tier store is frozen (no re-correction, no re-arbitration). ADR-0007 D1
  asserts "the soundings bags already are the bottom-agnostic source of truth" and the
  Consequences lean on bag-reprocessing, but **nowhere states the dependency: the MBES
  store's re-derivability is contingent on bag retention.** ADR-0005 D3 has the matching
  hook ("Re-import-from-source presumes the original source is retained; source retention
  is a ... responsibility, out of scope"), but ADR-0007 should name it locally.
  **Suggestion B3 (strongest suggestion):** add one line to D1 or Consequences/Risk —
  "this trades the sidescan store's bag-independence (ADR-0006 D11 cold-storage) for no
  redundant Tier-1; the MBES store's durability/re-derivability is therefore contingent
  on soundings-bag retention (cf. ADR-0005 D3)." It's a real asymmetry the split creates
  and the ADR currently states only the upside.
- **float-vs-uint16 value tile (D6) — proposed position is right.** Sidescan's `uint16`
  intensity is detection-grade display data; MBES backscatter carries an estimate
  *variance* band (D4) and mirrors the bathy store's `Float64` depth/uncertainty
  semantics, so a float value tile is the consistent choice — uint16 would throw away
  the precision the variance band exists to express. Correctly flagged "ratify in review";
  the proposed position is defensible. (The source-index band stays `uint16` per ADR-0005,
  unaffected.)
- **package placement (D9) — proposed position is right.** Accumulation logic in
  `cube_bathymetry` (where the hypothesis/grid machinery lives, sensors_ws, already feeds
  bathy) + store/tile IO in `core_ws` reusing `marine_tiled_raster_store`, with the
  one-way dependency `cube_bathymetry → core store` (never the reverse) is the same
  layering ADR-0002 D9 already commits to. Sound. "new package vs thin instantiation"
  is genuinely a code-time call; correct to defer.
- **Safety — correct and unweakened.** ADR-0007 Consequences keeps backscatter a
  non-autonomy product ("not a navigation or costmap input ... nothing here routes
  backscatter into Nav2") **and** correctly carves out that the *bathy half* of the same
  node output IS subject to ADR-0002's Safety-First costmap path ("The *bathy* half of
  the node output is subject to ADR-0002's Safety-First costmap path; the *backscatter*
  half is not"). It also gates any future texture/shadow→autonomy signal behind its own
  Safety-First review. Nothing in the three diffs touches or weakens the ADR-0005 D5
  shallowest-reliable carve-out (D5 text unchanged in this branch; ADR-0007 cites it
  correctly as out-of-scope). Confirmed clean.

**Suggestions (Lens B):** B1 (D2 outlier-claim over-reads — cross-point to D4 dispersion),
B2 (D3 unbounded per-beam record on hypothesis — note a cap or draft/processed split),
B3 (**strongest** — D1/Consequences should name the bag-retention dependency that the
single-tier choice creates; ADR-0006 D11 advertises the opposite for sidescan).

---

### Must-fix vs suggestion

- **Must-fix:** none.
- **Suggestions:** A1 (filename/title note — recommend leave as is), A2 (reciprocal
  sibling line in 0006 Status), B1 (qualify D2 outlier claim), B2 (bound D3 per-beam
  carry), B3 (state the bag-retention dependency in ADR-0007 D1/Consequences — the one
  asymmetry the split introduces that the ADR currently presents one-sided).

**Recommendation:** `approved-with-suggestions`. The restructure achieves its stated
scope completely and the new ADR-0007 is architecturally sound. B3 is the only finding
with design substance and even it is a documentation gap, not a design flaw — safe to
push and address in review, or fold in now.
