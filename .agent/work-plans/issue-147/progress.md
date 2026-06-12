---
issue: 147
---

# Issue #147 — Bathymetric store Phase 2: import (bag-replay, GeoMapSheet, GeoTIFF) with per-day epoch model

## Corpus Inventory
**Status**: complete
**When**: 2026-06-11 21:55 -0400
**By**: Claude Code Agent (Fable 5)

Surveyed `~/data/logs` (144 GB) for ROS-bag M3 multibeam coverage to drive the
Phase-2 bag-replay importer.

### M3 detection coverage (ROS bags)

| Day | Source | Sessions | M3 det msgs | Hours |
|-----|--------|----------|-------------|-------|
| 2026-06-09 | `gabby/logs/bizzy_m3/bag_2026-06-09T14.51.50_m3_detections` | 1 | 55,100 | 1.6 |
| 2026-06-10 | `gabby/logs/bizzyboat_sonar/2026-06-10T17-57-25+00-00` | 1 | 104,979 | 2.4 |
| 2026-06-11 | `gabby/logs/bizzyboat_sonar/2026-06-11T{16-30-24,19-29-20}+00-00` | 2 | 253,044 | 3.7 |

Total: **3 days / 4 sessions / 413k SonarDetections messages**. June 11's two
same-day sessions are a natural test case for the within-epoch cross-session
fusion rule (ADR-0002 A1.2); the three days exercise the multi-day epoch model.

### Replayability

- June-9 standalone bag is self-contained: `/bizzy/sensors/m3/detections`
  (SonarDetections) **and** `/bizzy/sensors/m3/soundings` (PointCloud2),
  `/bizzy/odom`, `/tf` (188k msgs), `/tf_static`, sound speed.
- June-10/11 sonar-recorder bags carry detections + `/bizzy/odom` + `/tf` +
  `/tf_static` (no pre-projected soundings topic).
- 98 of 101 sonar-recorder sessions (Apr 6 – Jun 8) contain **no** M3 topics —
  M3 recording in bags begins 2026-06-09. Earlier M3 data lives in QINSy
  projects on mercat (`mercat/QPS-Projects`, not ROS bags) and would enter the
  store as processed-layer products (BAG/GeoTIFF export), not via bag replay.

### Open verification items (next step)

- [ ] Confirm `/tf` in these bags carries the earth-frame chain needed to
  georeference a cold CUBE replay (cube pose source = TF + odom per cube#31/#33).
- [ ] Confirm which input `cube_bathymetry` consumes (SonarDetections vs
  PointCloud2 soundings) and whether sound-speed is applied upstream or needed
  at replay.
