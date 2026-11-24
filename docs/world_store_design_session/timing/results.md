# World-store timing measurements (A: CUBE lab box load, B: import scaling)

Written 2026-10-01 13:21 -04:00. Everything under this directory is scratch output; no tracked file, ~/data/world, ~/data/logs or /mnt/nadata path was written.
Host: Intel i7-10875H (8 physical cores / 16 threads, laptop, boost 5.1 GHz), 31 GB RAM, loadavg about 1-3 during runs (other agents idle).
Binaries used were the already-installed ones (no build): ui_ws `sidescan_probe`, core_ws `survey_index_query`, sensors_ws `import_bag`.
Repo heads: marine_perception_tools d41e63c, cube_bathymetry f3ff624.

Legend: MEASURED = number printed by a command (`/usr/bin/time -v`, `date +%s.%N`, the tool's own log). EXTRAPOLATION = my arithmetic on measured inputs.

---

## A. CUBE lab box load (survey explorer)

### A1. The load path (marine_perception_tools/src, ui_ws)

Button press runs `SidescanViewerWindow::runCubeLab()` (sidescan_viewer_window.cpp:1279); the work runs on a QtConcurrent worker (1363-1400).

1. Index query: `bridge_->queryBox(south,west,north,east,"mbes-bathy")` (sidescan_viewer_window.cpp:1290) -> `SurveyIndexBridge::queryBox` (survey_index_bridge.cpp:68): `tilesForBoundingBox` at every level + `marine_survey_index::queryPasses` on survey_index.db. The index is **tile-granular** (L14 tiles), so it returns every pass that touched any tile under the box, not only pings inside the box.
2. Coalesce rows into one window per physical pass: `coalescePasses` (pass_coalesce.hpp:54, 5 s gap rule); loop at sidescan_viewer_window.cpp:1298.
3. Per pass, serially: `load_cloud_passes` (mbes_pass_loader.cpp:132) -> `read_mbes_window` (mbes_pass_loader.cpp:181; mbes_window_reader.cpp:62). Inside it:
   - /tf_static scan from bag start (mbes_window_reader.cpp:128-135) and /tf seek to window start - 15 s then scan (137-152): each is its own `rosbag2_cpp::Reader` open.
   - A third Reader open + seek for the detections topic (179-188).
   - Per message: `deserialize<SonarDetections>` (198), TF lookup (209), `project_ping` = cube::DetectionsProjector + ErrorModel (222; mbes_projection.cpp:39), then `lift_sounding_to_world` per beam (228-236).
   - There is no cache on this path (the bag-index cache, session_index_io, belongs to the scrub viewer, not to the lab).
4. Box clip in each bag's own world frame via the earth anchor (mbes_pass_loader.cpp:207-248); cross-bag reprojection at 270-291.
5. Concatenate all passes (sidescan_viewer_window.cpp:1376-1386), then `run_cube` (cube_lab.cpp:190): spread each sounding over nodes within its influence radius and `node->insert` (346-371), then `queueFlush` + `extractNodeRecord` per node (389-390).
6. (Not timed here) mesh build, point-cloud upload, in the GUI thread after the ticket returns.

### A2. How I timed it without the GUI

No CLI or test runs the whole chain on a real bag. The GUI cannot be driven without drawing a box (there is no box CLI option), and a build of a small harness was ruled out. So I emulated stages 1-3 with existing binaries that call the **same library code**:

- `plan_box.py`: runs `survey_index_query --box ... --sensor mbes-bathy --json` (same `queryPasses` call) and re-implements `coalescePasses` (5 s gap) to get the window list.
- `run_boxA.sh`: for each window runs `sidescan_probe --mbes-window <bag> <t0_ns> <t1_ns>`, which calls exactly `read_mbes_window` (everything in step 3 up to and including projection and lift) and prints its own in-process milliseconds. Per-window process start (about 0.15 s, dynamic loading) is **not** in the "in-process" figure but is in the "wall" figure.
- Box clip, concatenation and `run_cube` could not be run (see A5).

Boxes (50 m x 50 m = 0.00045 deg lat x 0.000615 deg lon):
- **Box C ("typical, lightly covered")**: lat 43.00198 to 43.00243, lon -71.36417 to -71.36355 (centre about 43.0022 N, 71.3639 W, mid-lake Massabesic). 16 index rows -> 10 coalesced passes in 4 bags (2026-06-17, 06-23, 06-24, 07-23), 398 s of recording, 4,714 pings read, 1,067,123 soundings produced before the clip.
- **Box H ("heavily covered")**: lat 42.99593 to 42.99637, lon -71.36231 to -71.36169. 70 rows -> 14 passes in 5 bags, 1,874 s of recording, 55,509 pings, 13,316,510 soundings before the clip.
  Chosen from nav_track cells with several distinct bags, then checked with the index.

Bags are read from the **NAS** (CIFS mount `//nadata.ccom.nh/MAP2026ASV` at /mnt/nadata/map2026asv; the survey index stores those paths). Bag format is mcap; the 2026-07-23 bag has 81 split files, the others 1-2.

### A3. Measured times

Box C, whole path stages 1-3, sequential passes (as the lab does):

| stage | run 1 | run 2 (page cache dropped with posix_fadvise first) | run 3 (immediately after) |
|---|---|---|---|
| index query (CLI wall incl. process start) | 0.02 s | 0.02 s | 0.02 s |
| read_mbes_window, sum of 10 in-process times | 15.1 s | 12.9 s | 12.6 s |
| same, sum of wall incl. 10 process starts | 15.3 s | 13.1 s | 12.8 s |
| fastest / slowest single pass | 0.48 s / 3.8 s | 0.49 / 2.1 s | 0.46 / 2.0 s |

Box H: run 1 = 32.2 s in-process (32.5 s wall), run 2 = 30.8 s (31.1 s); 14 windows from 0.4 s to 12.3 s; peak RSS 454 MB for one window's 3.3 M soundings (the GUI holds all passes at once, so about 13.3 M soundings, order 1 GB).
Per-window tables: boxC_run*.log, boxH_run*.log.

**Cache effect: none visible.** The cold (cache dropped) and warm runs agree within 0.3 s. Per pass the reader pulls 50-265 MB (strace of read() bytes, 3 passes tried) through three reader opens, and the CIFS client does not keep those pages (fincore showed under 1 MB resident per file after a run). There is no session cache on this path, so a second box load costs the same as the first.

**Where the read time goes** (the stage that dominates):

1. Per-pass fixed cost, not per-ping cost. Marginal cost per ping, measured by growing one window 1 ping -> 599 pings in the 2026-06-17 bag (1 ping 414 ms, 599 pings 496 ms): about 0.14 ms/ping (about 0.6 microseconds per sounding) for deserialise + project + lift together. A 1,450-ping window in the 16 GB bag costs about 1.6 s, mostly bag-open overhead.
2. Phase split of one pass (`phases.sh`: strace -ttt on the three reader opens, which inflates times a little). Summed over the 10 box C passes: /tf_static phase 5.4 s, /tf phase 5.8 s, detections phase 4.7 s (total 15.8 s under strace). Each phase includes a Reader open; the detections phase includes seek + read + deserialise + project, and for the small bags it is only 0.15-0.2 s.
3. gdb stack sampling (45 attach-once samples per case, `gdb_probe_*.txt`):
   - 2026-07-23 bag (81 mcap files, local copy): 81% of samples inside `rosbag2_cpp::SequentialReader::seek` -> `MCAPStorage::get_metadata` -> `read_metadata` -> yaml-cpp parsing metadata.yaml (32 KB, 948 lines). Every `seek()` re-parses the whole metadata.yaml; the lab does two seeks per pass. 15% projection (cube::ErrorModel).
   - 2026-06-24 bag (16 GB, 1 mcap): 79% of samples in the reader open path (libmcap IndexedMessageReader / summary + chunk index), 7% projection, 3% TF buffer, 3% message iteration.
   - Deserialisation (CDR) did not appear as a top-of-stack hotspot in either.
4. NAS versus local: copying the 2026-07-23 bag (4.24 GB) to local disk and re-running its 5 box-C windows gave 7.0-7.2 s local versus 8.9-9.4 s NAS (two passes each). The NAS adds about 25% there; the rest is CPU in the bag-open and metadata code, so a local copy would not fix it.

So for a typical box the read stage is **about 13-15 s, over 90% of it per-pass bag-open overhead** (three Reader opens + two seeks per pass: yaml-cpp parse of metadata.yaml, mcap index parse, TF prepass), and only about 1-2 s is actual ping processing. Cost scales with the **number of passes** (and bag layout), not the number of soundings in the box.

### A4. CUBE stage (proxy, not measured)

`run_cube` could not be run, so here is only a proxy from B: the importer's single-bag stack samples put about 80% of its 0.47 ms/ping (236 soundings/ping) in CUBE insertion and influence-radius work, about 1.6 microseconds per sounding at 1 m cells (import_bag, includes the reference-surface gate; lab run_cube is similar code, different loop).
For box C, at most 1.07 M soundings reach run_cube (before the clip, so a safe upper bound): at most about 1.7 s. For box H at most 13.3 M soundings: at most about 21 s, in practice much less after the clip. **How many soundings survive a 50 m clip is the missing number**: it needs the clip + run_cube harness.

### A5. What is missing for a complete headless run

A 40-line harness that links the already-built static libs (build/marine_perception_tools/libsidescan_core.a, libsurvey_index_bridge.a, libtuner_core.a) and calls `SurveyIndexBridge::queryBox` -> `coalescePasses` -> `load_cloud_passes(passes, clip)` -> `run_cube` with std::chrono around each. I did not build it (rule). With it you also get the post-clip sounding count, the clip cost, and the real run_cube time at the lab's default cell size.
Also not measured: GUI-side mesh build / upload, and the lab running next to a live GUI.

---

## B. Depth-tile import scaling across cores

### B1. Setup

Invocation copied from unh_echoboats_project11/scripts/build_bathy_store.sh (read only, not run), run directly as the importer binary (`run_import.sh`):

```
import_bag -o <scratch>/depths --bs-store <scratch>/bs -d /bizzy/sensors/m3/detections --odom-topic /bizzy/odom -r 1.0 \
  --base-link-frame bizzy/base_link --level-frame bizzy/base_link_north_up --tide-frame bizzy/map_tide \
  --platform bizzyboat --sensor kongsberg-m3 --campaign timing-test \
  --reference-store <scratch>/refstore \
  --backscatter-correction empirical --backscatter-curve .../bizzyboat_project11/config/m3_angular_response_curve.csv  <bag>
```

`--reference-store` pointed at a **copy** of ~/data/world/depths/reference (57 MB) in scratch, so the world store was never opened for writing. Each process got its own fresh `<scratch>/stores/<tag>/`. `import_bag` has no time-range option (only `-l` ping count), so whole bags were used; they take 1-2 minutes each.

Bags (all /mnt/nadata/map2026asv/logs/gabby/logs/bizzyboat_sonar/..., Massabesic campaign, similar ping counts):

| tag | bag | size | detections (pings) |
|---|---|---|---|
| b1 | 2026-06-16T14-17-37+00-00 | 1.80 GB | 169,166 |
| b2 | 2026-06-29T13-30-00+00-00 | 2.33 GB | 150,750 |
| b3 (baseline) | 2026-06-16T19-47-06+00-00 | 1.55 GB | 132,910 |
| b4 | 2026-06-26T13-57-35+00-00 | 1.07 GB | 133,422 |

For context, the Massabesic window 2026-06-12 to 2026-07-01 holds 37 bag directories with 11,030,586 M3 detections in their metadata.yaml, dominated by a few 7-18 GB bags.

`run_set.sh` drops the bags' page cache (read-only posix_fadvise) before each set, launches the processes together, polls MemAvailable every 2 s (abort under 4 GB; lowest seen 23.6 GB), and waits by PID. Pings = importer's "Offline projection: N pings projected" line.

### B2. Measured: per-process results (full tables incl. all sets: analysis_tables.md; logs under logs/)

NAS = read from /mnt/nadata over CIFS (cache dropped first). LOCAL = copy of the bag on the local nvme (cache dropped first). Each bag was run alone first, then the sets.

| run | bag | pings | wall s | user s | sys s | %CPU | max RSS MB | pings/min (own wall) |
|---|---|---|---|---|---|---|---|---|
| NAS alone | b3 (baseline) | 132,910 | 75.0 | 62.3 | 1.0 | 84 | 334 | 106,314 |
| NAS alone | b1 | 169,166 | 94.2 | 79.4 | 1.3 | 85 | 515 | 107,726 |
| NAS alone | b2 | 150,750 | 91.5 | 72.6 | 1.5 | 80 | 743 | 98,820 |
| NAS alone | b4 | 133,422 | 71.6 | 62.6 | 0.8 | 88 | 240 | 111,869 |
| NAS 2 at once | b1 / b2 | | 104.2 / 99.8 | 86.7 / 77.8 | | 84 / 79 | | 97,408 / 90,595 |
| NAS 2 at once | b3 / b4 | | 82.9 / 78.3 | 69.0 / 68.3 | | 84 / 88 | | 96,242 / 102,187 |
| NAS 4 at once | b1 / b2 / b3 / b4 | | 114.7 / 110.9 / 93.3 / 88.4 | 94.1 / 83.9 / 74.5 / 74.9 | | 83 / 77 / 80 / 85 | | 88,461 / 81,560 / 85,491 / 90,568 |
| LOCAL alone | b3 | 132,910 | 66.4 | 64.7 | 0.8 | 98 | 334 | 120,172 |
| LOCAL alone | b1 / b2 / b4 | | 87.6 / 73.1 / 61.8 | 85.5 / 70.5 / 59.9 | | 98 / 98 / 97 | | 115,894 / 123,701 / 129,578 |
| LOCAL 2 at once | b1 / b2 | | 90.3 / 82.0 | 87.8 / 79.1 | | 98 | | 112,403 / 110,278 |
| LOCAL 2 at once | b3 / b4 | | 67.9 / 67.5 | 65.9 / 66.0 | | 98 | | 117,498 / 118,580 |
| LOCAL 4 at once | b1 / b2 / b3 / b4 | | 96.3 / 88.1 / 75.2 / 75.0 | 93.7 / 85.0 / 73.0 / 73.5 | | 98 | | 105,367 / 102,691 / 106,003 / 106,738 |

Baseline, one bag alone, NAS cold: 132,910 pings, 75.0 s wall, 62.3 s user, 1.0 s sys, peak RSS 334 MB, 106,314 pings/min. Repeated with the cache warm on the NAS: 62.8 s; from local disk with cache dropped: 66.4 s (98% CPU); local warm 65.7 s.
So the NAS costs the single process about 9 s of 75 (12%): the process sits at 80-88% CPU waiting on reads. Copying the bag NAS -> local took 13.7 s (about 113 MB/s, 1 GbE speed).

### B3. Measured: scaling (sets run one after the other, never overlapping)

| set | procs | total pings | makespan s | total pings/min | speedup vs the same bags run alone (sum of solo walls / makespan) | efficiency |
|---|---|---|---|---|---|---|
| NAS 2 (b1+b2) | 2 | 319,916 | 104.2 | 184,198 | 1.78x | 89% |
| NAS 2 (b3+b4) | 2 | 266,332 | 82.9 | 192,825 | 1.77x | 88% |
| NAS 4 | 4 | 586,248 | 114.7 | 306,543 | 2.90x | 72% |
| LOCAL 2 (b1+b2) | 2 | 319,916 | 90.3 | 212,547 | 1.78x | 89% |
| LOCAL 2 (b3+b4) | 2 | 266,332 | 67.9 | 235,409 | 1.89x | 94% |
| LOCAL 4 | 4 | 586,248 | 96.3 | 365,120 | 3.00x | 75% |

Single process: about 106k pings/min (NAS) and 122k pings/min (local, pings-weighted over the four bags).
What the loss is made of: (a) **load imbalance**: the makespan is set by the largest bag (b1); (b) **CPU slowdown of every process when others run**: summed user CPU for the same work rose 6.5% at 2 processes (166.9+131.9 vs 156.0+124.6 s) and 15.9% at 4 (325.2 vs 280.6 s, local). The CPU is a laptop part that drops boost clocks and shares cache/memory bandwidth under load. Judged on CPU time alone the 4-process efficiency is about 86% (280.6 / 325.2), the remainder of the 75% is imbalance. (c) NAS reads are not the limit at 4 readers: 4 processes pulled about 6.8 GB in 115 s (about 59 MB/s).
Memory is not a constraint at this size: peak RSS 240-760 MB per process on single bags (the owner's 5 GB figure applies to the whole campaign's resident tile set; not measured by me).

### B4. Where one process spends its time (gdb stack sampling, `gdb_samples.txt`)

45 samples of the main thread of one b3 import (local copy, run from 6 s to about 58 s of 66 s; 300 ms gap between attaches; coarse, expect roughly +-10 points):

| category | samples | share |
|---|---|---|
| CUBE insertion: ImportAccumulator::addBatch -> GeoMapSheet -> GeoGrid::insert (cell iteration, node insert) | 29 | 64% |
| CUBE per-batch bounds: gridIndicesForSoundings / Parameters::influenceRadius (pow) | 8 | 18% |
| projection (DetectionsProjector / ErrorModel) | 3 | 7% |
| GeoSounding copy / conversion | 3 | 7% |
| tile management inside addBatch (TiledRasterTile construct; blunder gate / reference prior) | 2 | 4% |
| bag read + CDR deserialise | 0 | 0% |

So roughly 80-85% of single-process time is CUBE insertion, which is the part an area split can divide.
An independent bound on the **not-divisible-by-area** part: `import_bag --depth-adaptive --level-plan-out` runs a recon-only pass (read + deserialise + project + count into a grid + spill 2 GB to disk, no CUBE). On b3 (local) its "projected 132,870 pings in 18.1 s", versus 64.8 s for the same phase in the full import: **at most 28% of the work is read + project (+ count + spill)**, probably about 20%. The gdb samples say 13% (small sample). Combined with the 0.14 ms/ping figure from A, I use 20% (range 13-28%) below.

### B5. Extrapolation to a 33-bag, 12 M-ping rebuild (EXTRAPOLATION, not measured)

Inputs (measured): owner's single-process rebuild 84 min (I did not run the full campaign; my single-bag rates, 106-122k pings/min, would give 98-113 min for 12.0 M pings, so the owner's 84 min is the faster base and I used it); CPU slowdown per process f = 1.0 (1 proc), 1.065 (2), 1.159 (4) measured; f at 8 = 1.36 (straight-line extension of the 4-process trend) and f at 12 = 1.62 (assumes the 8 physical cores give about 1.25x more throughput from the extra hyperthreads; both are assumptions, I could not run more than 4 processes). Model, with a = share of a single process that is read + project, imb = load imbalance of an area split:

- Design 1, "every worker reads and projects all 33 bags, keeps only its area": time = 84 x (a + (1-a) x imb / K) x f(K).
- Design 2, "project every bag once, in parallel by bag, into area-partitioned spill files, then one CUBE worker per area": time = 84 x (a/K + (1-a) x imb / K) x f(K). The spill I/O is **not** included (the recon pass wrote 2.1 GB for 133k pings, so about 190 GB for the campaign at the importer's current 64 B/sounding spill format).
- optimistic: a=0.13, imb=1.0, f x0.95. central: a=0.20, imb=1.15, f as above. pessimistic: a=0.28, imb=1.3, f x1.10.

| workers | design | optimistic min | central min | pessimistic min |
|---|---|---|---|---|
| 2 | every worker reads+projects all bags | 48 | 59 | 74 |
| 2 | project once, partition by area | 42 | 50 | 60 |
| 4 | every worker reads+projects all bags | 32 | 42 | 55 |
| 4 | project once, partition by area | 23 | 27 | 33 |
| 8 | every worker reads+projects all bags | 26 | 36 | 50 |
| 8 | project once, partition by area | 14 | 16 | 19 |
| 12 | every worker reads+projects all bags | 26 | 38 | 54 |
| 12 | project once, partition by area | 11 | 13 | 15 |

Reading it: with the simplest design (every worker reads everything) the redundant read+project share puts a floor of about 35-40 minutes around 8 workers and gains nothing beyond; the 30-60 minute target is reachable with 4 workers (about 42 min central, 32-55 range). Pre-projecting once pushes it to 16-27 min at 4-8 workers before spill costs.

What the extrapolation ignores:
- **CUBE depends on sounding arrival order.** Per-node order is preserved if each worker reads bags in the same order and owns whole tiles, but a sounding's influence reaches into neighbouring tiles, so a real splitter needs margins (halo) around each area and a merge of tile edges; this duplicates a little work and adds a merge step. I did not verify that results stay identical, nor how the blunder gate (reference prior, predicted surface across tile edges) behaves at borders.
- Memory: I measured 0.24-0.76 GB per process on one bag; a worker holding its share of the campaign's tiles is not measured. At the owner's 5 GB per full-campaign process, 8-12 workers each holding only 1/K of the area should fit in 31 GB, but that is not tested.
- Load imbalance: Massabesic effort is uneven (one 17 GB bag has 1.68 M pings; the three largest bags hold about 40% of pings), so equal-area splits will not be equal work; I used 1.0-1.3.
- The 16 "cores" are 8 physical cores with hyperthreading; time at 12 workers is only barely better than 8.
- Disk and NAS: input is about 128 GB of bags (the 37 bag directories in the window, summed from their file sizes) if read once, but 33 bags x K workers if every worker reads everything (a few hundred GB over a 1 GbE share at about 110 MB/s: at 8 workers that alone is hours unless the bags are on local disk).
- Tile writing, the end-of-run build step, the per-pass level planning of depth-adaptive mode (the recon run spent 140 s after the pass on planning for one bag) are not included.

### B6. Surprises

- The single import process runs at 98% CPU from local disk, 80-88% from the NAS, so the owner's 98% figure suggests those bags were local or already cached (not checked).
- Bag reading and CDR deserialisation are invisible in the importer's samples; CUBE insertion is about 80%, which is why area splitting looks attractive. Projection + read are still 20%+ of the single process, which is what limits the naive design.
- 4 processes slowed each other's CPU by 16% even with 12 free threads (laptop boost and cache sharing).
- In A, the lab load time depends almost entirely on the **number of passes** and on how rosbag2 opens the bag. Each pass re-parses metadata.yaml in every `seek()`, and every Reader open scans the mcap summary; there is no cache. A cheap fix is to cache the open reader / TF buffer per bag across passes, but that is a design question and I changed nothing.
- The index is tile-granular, so the lab reads every ping of a pass that crosses any index tile under the box (box C: 4,714 pings read to fill a box that holds a fraction of that), then discards outside the clip.

### B7. Cleanup

Scratch stores (stores/), the reference copy (refstore/) and the local bag copies (localbag/: the four B bags, 6.3 GiB, plus the 4.24 GB 2026-07-23 bag used in A) were deleted. Kept: scripts (plan_box.py, run_boxA.sh, phases.sh, evict.py, run_import.sh, run_set.sh, phaseB1.sh, phaseB2.sh, analyze.py, extrapolate.py), logs, gdb samples, tables.
Side effects outside scratch: none written. The only non-read actions on the shared data were `posix_fadvise(DONTNEED)` on read-only file descriptors of the bags I read (drops cached pages; changes no data). git status of the workspace is clean.
