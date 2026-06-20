# Progress — Issue #179: ADR-0005 Cross-Store Multi-Platform Provenance & Source Registry

## Local Review (Pre-Push) — 2026-06-20

**Reviewer:** sandboxed sub-agent running `review-code` (pre-push, deep), worktree-scoped.
**Subject:** `docs/decisions/0005-multi-platform-provenance-registry.md` on `feature/issue-179` (base `jazzy`).
**Lenses:** Claude Adversarial ×2 (Lens A logic/consistency + Lens B systemic/safety). No static profile (`.md`). Context: ADR-0001, ADR-0002 read for consistency.

**Verdict:** changes-requested → **all findings addressed in-text** (no redesign).

### Blocking (cross-confirmed by both lenses where noted) — RESOLVED

1. **Per-cell `source-id` contradicted ADR-0002 D5** (Lens A+B). 0002 D5 rejected a per-cell source band on a single-source-per-tile premise; multi-platform fusion makes `source-id` non-constant within a tile. → **Fixed (D2):** added an explicit *amends ADR-0002 D5* paragraph defining the per-cell `source-id` band, its dtype handling (native `uint16` in backscatter; `Float64`/parallel tile for bathy migration #178), and content-hash-payload membership.
2. **Distributed `source-id` allocation could collide and was baked into immutable tiles** (Lens A+B). → **Fixed (D4):** rewrote allocation to be **collision-safe by construction** via origin-namespaced ids (high bits = origin, low bits = local seq); v1 single-allocator safety asserted; D7 merge stated append-only *because of* the D4 invariant.
3. **Safety: registry priority could defeat the costmap shallowest-reliable path** (Lens B). → **Fixed (D5):** added a **safety carve-out** — registry priority governs the default/visualization query only; the ADR-0002 D7 shallowest-reliable navigation mode is never overridden (returns shallowest reliable across all sources, conservative no-data/stale policy, Simulation-First validation). Reinforced in D8.

### Decision taken during triage
- **D5 arbitration = priority-first** (Roland, 2026-06-20): `sensor_class` is the primary key, quality breaks ties *within* a class. Resolved the internal inconsistency the review flagged (the text had said quality-first but the example was priority-first). Only the numeric priority table remains deferred.

### Suggestions — RESOLVED
- Reference direction: ADR-0005 now **owns** the `source-id` definition (D2); 0006 references up.
- Re-arbitration-without-reimport guarantee **scoped to stores with a Tier-1 archive** (backscatter); bathy re-imports until it grows one (D6).
- Source-retention caveat added (D3); "append-mostly" tightened to "append-only via D4" (D7); ADR-0006 reference marked "(proposed)".
- ADR-0006 cross-references aligned (D7/D8): per-cell `source-id` band noted as the 0005 D2 amendment; arbitration stated as priority-first + origin-namespaced.

## Local Review (Pre-Push) — round 2 — 2026-06-20

Re-review (sandboxed sub-agent) confirmed **all three round-1 blockers CLOSED**. Fresh adversarial pass found:

- **N1 (blocking, introduced by the round-1 edits) — RESOLVED.** D4 defines a 64-bit origin-namespaced `source-id`, but D2 put it in a `uint16`/`Float64` per-cell band — neither holds 64 bits (`Float64` = 53-bit exact mantissa → silent aliasing). → **Fixed:** two-level id — the per-cell band is a compact **local source index** (narrow type, exact); the wide `source-id` lives in the **registry** as the global/sync identity (D2/D4).
- **N2 (arbitration semantics) — RESOLVED via Roland's input.** Maturity/recency composition is **query-mode-specific**, not a single global rank: live/working mode = recency-first (newest wins; covers operator display + a producer like CUBE that read a `processed` prior and wrote a newer result); curated `processed` mode = deliberate composition (priority-first within a set; cross-layer/recency policy deferred to post-processing); navigation-safety = the carve-out (priority-agnostic). D5 rewritten.
- **N3 (wording) — RESOLVED.** D5 no longer says priority "generalizes/replaces" ADR-0002 D3; it **adds** sensor_class priority *within* each maturity layer.

ADR-0006 (#180) review: no blockers, 8 hygiene suggestions — applied on its branch.
