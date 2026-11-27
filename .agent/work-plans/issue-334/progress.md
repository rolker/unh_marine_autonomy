---
issue: 334
---

# Issue #334 — Overview builder + coverage manifest: durability (fsync), manifest provenance, lock ergonomics, disk pre-flight

## Integrated Review
**Status**: complete
**When**: 2026-08-22 17:58 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))

**PR**: #336 at `d1694b7e`
**Sources**: 1 (Copilot @ `d1694b7e`, current). No human reviews, no
conversation comments, no prior local timeline — this entry creates it.
**Cross-source confirmations**: 0
**CI**: all pass (`build` success)

Five findings, **all valid**. Docs-only PR amending `uma-ADR-0013` D3. Note the
author of the amendment is the same agent triaging it; the findings below were
verified against the ADRs rather than taken on the reviewer's word, and two of
them contradict claims this agent made repeatedly in conversation.

### Findings
- [ ] (valid, Copilot) Bare `ADR-0001` reference in the Status block points at
      the **wrong namespace**: in this repo `ADR-0001` is the shared-scalar
      colormap decision; the immutability rule is the **workspace** ADR-0001.
      This document's own citation convention — extended one PR earlier
      (uma#330) to require a full link for workspace ADRs — forbids exactly this.
      Same mistake, same document, one PR later —
      `docs/decisions/0013-bounded-lod-navigation.md:10`
- [ ] (valid, **substantive**, Copilot) The coverage contract is left
      underspecified. D3 says a layer declares one coverage set and consumers do
      not scan; the amendment says a derived manifest covers only its sidecar and
      is not layer-wide — and #331 keeps native coverage in memory only. **A
      consumer can therefore obey neither rule and get complete coverage.**
      Specify the composition/ownership contract, or explicitly permit scanning
      for native coverage —
      `docs/decisions/0013-bounded-lod-navigation.md:204`
- [ ] (valid, Copilot) "generation time + source tile count" cannot make all
      content changes detectable: a source tile edited **in place** leaves both
      recorded values valid while the derived content is stale. Require a source
      fingerprint, or scope the guarantee to what count/time can actually catch —
      `docs/decisions/0013-bounded-lod-navigation.md:223`
- [ ] (valid, Copilot) The atomic-publish rule claims more durability than it
      delivers: without fsync of the file and its containing directory, a power
      loss can still leave a missing or partial `coverage.json` even though the
      rename was atomic. This is exactly the gap #334 tracks. Qualify as
      process-crash consistency, or make power-loss durability part of the rule —
      `docs/decisions/0013-bounded-lod-navigation.md:218`
- [ ] (valid, Copilot) **"No query path reads a manifest or overview level" is
      overbroad and false.** D8 restricts *safety* queries. Verified against the
      ADRs: `ADR-0010` D10 designs the voyage planner to plan "at
      coastal/approach levels … and drop to harbor level at route endpoints" and
      states "the level-aware walk exists in the query today";
      `ADR-0011` §4 anticipates level-aware queries fed by shallowest-preserving
      overviews. What was actually verified is narrower —
      `tile_io.cpp:238` excludes `overviews*` from `load()`/`loadWindow()`.
      Narrow the claim to safety queries —
      `docs/decisions/0013-bounded-lod-navigation.md:245`

### False positives
- None this round.

### Notes (not findings)
- Finding 5 corrects a claim this agent asserted repeatedly to the operator
  today as a safety argument ("no query path reads the sidecar"). D8's real,
  narrower claim — safety queries read to the finest available level, and
  `shallowestReliable()` scans all layers and levels — still holds, and nothing
  merged today violates it. The overbroad wording would have licensed a future
  reader to conclude something false.
- Finding 2 is the composition contract discussed with the operator earlier
  (native vs derived manifest ownership, and whether the public web view's
  inability to scan justifies making manifests mandatory). That discussion
  concluded the prerequisites are atomic tile writes (#256 / cube#135) and a
  shared-tile merge rule (#316) — the latter now partly settled by #339's
  contention counter. **Scope decision still open**: land the amendment stating
  only what is certain and defer composition to its own issue, or hold the PR
  until the contract is written.
