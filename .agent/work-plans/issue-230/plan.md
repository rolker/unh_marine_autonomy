# Plan: SonarVisualizationTile transport + anti-entropy tile-sync (ADR-0008)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/230 (Part of #171; realizes I3 / #86 Phase 6; companion to ADR-0008)

## Context

ADR-0008 (merged) ratified the boat→operator live tile transport for GGGS-tiled
sonar coverage. This issue delivers its **contract artifacts** in
`marine_interfaces`: the quantized display-grade tile message plus the
anti-entropy catalog/request protocol. The producer (`cube_bathymetry` #78) and
the operator render/cache (`camp` #121) are separate downstream issues that
depend on these messages.

`marine_interfaces` is a pure `rosidl` package — `msg/` files registered in an
explicit `set(MSG_FILES …)` in `CMakeLists.txt` (not a glob), so new messages
must be added to that list. No existing GGGS/tile/`GridIndex` message exists;
these are net-new in a domain none of the in-flight churn (#158/#162/#167)
touches.

## Approach

**Messages** (six `.msg`, helper types embedded for reuse):

1. **`GridIndex.msg`** — `uint8 level`, `uint32 row`, `uint32 col`. The ROS
   mirror of `gggs::GridIndex`; embedded in tile, catalog entry, and request so
   the index format is defined once.
2. **`VisualizationBand.msg`** — one self-describing quantized band:
   `string name`, `uint8 dtype`, `float64 scale/offset/nodata`, `uint8[] data`
   (row-major raw bytes for the **dirty window**, length `=
   window_w·window_h·sizeof(dtype)`). Consumer dequantizes generically
   (`value = raw·scale + offset`). `dtype` constants **mirror
   `sensor_msgs/PointField`** (INT16=3, UINT8=2, …) rather than inventing a new
   enum (precedent-reuse — [[feedback_sensor_msg_conventions_tf_trademarks]]).
3. **`SonarVisualizationTile.msg`** — `std_msgs/Header header` (`stamp` = tile
   version time on the boat clock; `frame_id` = display-CRS tag), `GridIndex
   index`, `uint16 width/height` (cells per side), dirty sub-window `uint16
   window_col/window_row/window_width/window_height`, `VisualizationBand[]
   bands`. Band data covers **only the dirty window** (incremental patch — the
   bandwidth win on top of quantization+zlib, matching cube#70's incremental
   `~/tiles`); a full-tile update sets the window to the whole tile.
4. **`TileCatalogEntry.msg`** — `GridIndex index`, `builtin_interfaces/Time
   version` (per-tile version = latest-cell timestamp, ADR-0008 D3).
5. **`TileCatalog.msg`** — `std_msgs/Header header` (`stamp` = catalog
   generation-time, the prune gate), `TileCatalogEntry[] entries` (a
   **complete** snapshot — prune-on-absence depends on completeness, D4).
6. **`TileRequest.msg`** — `std_msgs/Header header`, `GridIndex[] tiles` (the
   operator's "need" list).

**Anti-entropy reconciler** (the testable heart of acceptance #2): a **ROS-free**
pure-logic library (proposed package `marine_tile_sync`) so the protocol is
proven deterministically without a GUI and is reusable by both `camp` (#121) and
the web viewer (#166):

7. **`TileCatalogBuilder`** (boat side) — snapshot a dirty/known set → catalog.
8. **`TileCatalogReconciler`** (operator side) — given local cache `{index →
   version}` + a received catalog, compute `{request-list, prune-list}`:
   request missing/stale; **prune-on-absence** but **timestamp-gated** (prune an
   absent tile only if older than the catalog's generation-time, D4 condition b).
9. **Unit tests** simulating loss / reorder / cold-start / **boat reset** (fresh
   boat → small catalog → operator prunes the rest), asserting convergence to
   exactly the catalog set and that a late/reordered catalog cannot delete a
   just-pushed fresh tile.

**Docs**: a `marine_interfaces` message-reference note for the new family, and
update the package's message inventory.

## Files to Change

| File | Change |
|------|--------|
| `marine_interfaces/msg/GridIndex.msg` | New |
| `marine_interfaces/msg/VisualizationBand.msg` | New (with PointField-mirrored dtype constants) |
| `marine_interfaces/msg/SonarVisualizationTile.msg` | New |
| `marine_interfaces/msg/TileCatalogEntry.msg` | New |
| `marine_interfaces/msg/TileCatalog.msg` | New |
| `marine_interfaces/msg/TileRequest.msg` | New |
| `marine_interfaces/CMakeLists.txt` | Append 6 entries to `set(MSG_FILES …)`; `builtin_interfaces` already in DEPENDENCIES |
| `marine_interfaces/package.xml` | No new deps (std_msgs/builtin_interfaces already present) — verify |
| `marine_tile_sync/` (new pkg) | Reconciler + builder lib + GTest (PR2) |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Don't invent message conventions; read precedent | `dtype` mirrors `sensor_msgs/PointField`; `Header`-carried version/gen-time, not bespoke fields |
| Robustness / do it with tests | Reconciler is ROS-free + unit-tested under loss/reorder/reset (acceptance #2) |
| Quality Standard — complete the lifecycle transition | Timestamp-gated prune handles the reorder/late-catalog edge case, not just the happy path |
| No trademarks / display-vs-store separation | Tile message carries no provenance — it is the display projection, never a store schema |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0008 (this companion) | Yes | D1 named bands; D2 dense-quantized, **no compression fields** in msg; D3 `builtin_interfaces/Time` version; D4 complete catalog + gen-time + timestamp-gated prune; D7 level in `GridIndex`; D8 no `source_index` |
| 0001 (colormap) | No | Bands are scalar fields; colormap is operator-side render (camp#121), not in the message |
| 0002 (bathy store) | Indirect | Tile identity = GGGS index matches the store's tiling |
| 0007 (MBES backscatter) | Indirect | `backscatter` band is display-grade (D10); durable home stays the store |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| `set(MSG_FILES …)` in CMakeLists | Rebase against #167/PR#169 if it lands first (same list) | Yes — sequence/rebase noted |
| New message family | Producer cube#78, consumer camp#121 build against these | No — downstream issues |
| New `marine_tile_sync` package | Workspace build order / `.repos` if standalone | Decided in Open Q (in-repo pkg = no `.repos` change) |
| Package gains messages | `marine_interfaces` has **no `.agents/README.md`** — gap, not created here | No — separate doc task |

## Open Questions

- [ ] **Reconciler scope.** Recommend #230 = messages (PR1) **+** ROS-free
  `marine_tile_sync` reconciler/builder lib with simulated-loss tests (PR2,
  stacked) — this is what satisfies acceptance #2 and gives cube#78/camp#121 a
  tested protocol to depend on. Alternative: ship messages-only here and put the
  reconciler in camp#121 (leaves acceptance #2 unmet in this issue). **Pick one.**
- [ ] **New package name/home.** If we build the lib: `marine_tile_sync` as a new
  package **inside `unh_marine_autonomy`** (no `.repos` change; recommended) vs.
  standalone repo. Confirm the name.
- [ ] **`GridIndex` naming.** Reusing the gggs type name as `marine_interfaces/GridIndex`
  is intentional (ROS mirror) — flag only if a different name is preferred to
  avoid confusion with `grid_map`.

## Estimated Scope

**Two stacked PRs** (if reconciler in scope): PR1 = six messages + CMakeLists +
docs (small, low-risk); PR2 = `marine_tile_sync` lib + GTest (medium), stacked on
PR1. Collapses to a single messages-only PR if the reconciler is deferred to
camp#121.
