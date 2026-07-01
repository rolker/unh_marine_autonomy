---
issue: 251
---

# Issue #251 — sidescan_mosaic_bag: O(n²) TF lookups → single interleaved pass + bounded cache

## Local Review (Post-PR)
**Status**: complete
**When**: 2026-07-01 06:20 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: changes-requested
**PR**: #252 at `615e155`
**Mode**: post-PR
**Depth**: Deep (reason: correctness-critical TF/FIFO ordering rewrite)
**Must-fix**: 1 | **Suggestions**: 3

### Findings
- [ ] (must-fix) `pending` FIFO is unbounded → RAM/OOM when the TF frontier stalls (early `/tf` end, long dropout, or static-only chain: nothing drains until EOF flush). Cross-confirmed Lens A+B. Cap the deque, force-draining oldest as no-tf. — `marine_sidescan_mosaic/src/sidescan_mosaic_bag.cpp:191,296`
- [ ] (suggestion) Frontier is `max` over all dynamic frames; if the earth→transducer chain frame lags the frontier by >3s guard, a ping is released and dropped as no-tf even though its bracket arrives later (also: >57s frontier jump can evict the bracket from the 60s window before drain). Two-pass was immune. — `sidescan_mosaic_bag.cpp:199-208,240`
- [ ] (suggestion) Head-of-line blocking: a single corrupt far-future ping stamp at the FIFO head blocks all draining until EOF flush. — `sidescan_mosaic_bag.cpp:197-204`
- [ ] (suggestion) `deserialize()` / `reader.open()` unguarded → one truncated message aborts the whole import (`std::terminate`); field bags can be truncated. Skip-and-count is more robust. — `sidescan_mosaic_bag.cpp:82-89,168`

Sound (verified): drain sign/off-by-one correct; frontier-underflow short-circuit correct; static transforms excluded from frontier; nadir snapshot-at-enqueue preserves semantics; EOF flush counts failed lookups; write/flush failure detected; arg parsing guarded.

## Address Findings
**When**: 2026-07-01 06:45 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

- [x] (must-fix) Unbounded `pending` FIFO → bounded at `kMaxPending=20000`; over-cap force-projects the oldest via `flush_front()`. Happy-path output unchanged (308471 pings, 15.6s, 40 MB). Pushed to PR #252.
- Suggestions (frontier per-frame lag, head-of-line blocking, unguarded deserialize) left as hardening follow-ups; noted, not blocking.
