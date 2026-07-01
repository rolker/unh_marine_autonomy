---
issue: 253
---

# Issue #253 — sidescan_tier2_processed --accumulate: composite into an existing store

## Local Review (Post-PR)
**Status**: complete
**When**: 2026-07-01 06:20 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: changes-requested
**PR**: #254 at `bfc5531`
**Mode**: post-PR
**Depth**: Standard (reason: merge logic + file-reload durability)
**Must-fix**: 2 | **Suggestions**: 3

### Findings
- [ ] (must-fix) Reload failure → silent data loss: if `loadTile` throws (corrupt/partial/old-format/transient IO), the code only warns, then `saveTiles` OVERWRITES the tile with current-run-only data, dropping all prior accumulated coverage on that tile. Violates "never lose data". Abort the run (non-zero exit) or write to temp / preserve original on reload failure. Cross-confirmed Lens A+B. — `sidescan_tier2_processed.cpp:261-264` (effect at `:273`)
- [ ] (must-fix) `writeRegistry` overwrites `registry.json` with only the current run's single source, but `foldTile` preserves each existing cell's original source band — so multi-run `--accumulate` with distinct `--source-id`s yields tiles with mixed source indices and a registry listing only the last. registry.cpp's own TODO(#179, ADR-0005 D8) says reimport must MERGE append-only. Masked today (all per-bag runs use source-id=1) but the feature enables silent provenance corruption. At minimum merge the registry or reject a source-id absent from the existing one. — `sidescan_tier2_processed.cpp:278`
- [ ] (suggestion) `saveTile` is non-atomic (`driver->Create` truncates in place); a crash/disk-full mid-write leaves a corrupt tile replacing the good one — compounds the reload-failure loss and breaks the idempotency claim across a crash. Fix in shared I/O: write temp + `rename()`. (Pre-existing `marine_tiled_raster_store` code; separate follow-up.) — `marine_tiled_raster_store/src/tile_io.cpp:132`
- [ ] (suggestion) No concurrency guard: two `--accumulate` runs on one store dir race (lost update / corrupt tile). Sequential-only is neither documented in usage nor enforced. Add a store lockfile or document it. — `sidescan_tier2_processed.cpp:242-267`
- [ ] (suggestion) Test gap: `foldTile`'s `if (q==0) continue` no-data guard is redundant with the strict-`>` test, so `FoldTileKeepsBetterIncumbentAndSkipsNoData` doesn't distinguish the skip path. — `test_processed_accumulator.cpp:114`

Sound (verified): tile is square 960×960 so foldTile bounds correct; merge is order-independent + idempotent (strict `>`, incumbent-wins-ties); grids snapshotted before fold → no iterator invalidation; saveTiles dirty-only leaves untouched tiles alone; `--accumulate` default-off preserves prior behavior. ADR-0006 D5/D7 (cross-pass best-source composite) — now actually implemented.

## Address Findings
**When**: 2026-07-01 06:45 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

- [x] (must-fix) Reload failure → data loss: now ABORTS (exit 1) before any save; corrupt-tile test confirms the tile is preserved, not overwritten.
- [x] (must-fix) Registry source-id mismatch: guard refuses (exit 2) with a re-run hint, before decoding; same-source re-accumulate unchanged (exit 0).
- [ ] (suggestion) `saveTile` non-atomic (temp+rename) — deferred to a `marine_tiled_raster_store` follow-up issue (shared I/O, pre-existing).
- [ ] (suggestion) concurrency lockfile + test distinguishing the no-data-skip path — hardening follow-ups, not blocking.
