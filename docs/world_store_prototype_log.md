# The World Store — Appendix B: prototype log (adoption decisions and spine decisions, 2026-09-18 → 09-21)

Verbatim copy of the working log kept beside the prototype (`~/data/world_proto/DECISIONS.md`) at rev 3. It is the evidence behind the design's *decided* sections: each component was built against real season data before it was kept. Scripts are numbered as in `~/data/world_proto/scripts/`. Process-derived details in here (readers, schemas, thresholds) are provisional by the governing principle recorded on 2026-09-21.

# World-store prototype — adoption decisions (uma#391)

Each component was prototyped on a copy of `~/data/world/depths/processed` on two platforms:
dev host (Ubuntu 24.04, GDAL 3.8.4) and a Ubuntu 26.04 container (GDAL 3.12.2, the lyrical/rolling platform).

| # | Component | Decision | Evidence |
|---|---|---|---|
| 1 | COG tile format | KEEP, via COG driver (writer: temp GTiff → CreateCopy) | 150 tiles both platforms, pixels identical (checksums), 240→119 MB, readers unchanged (GDALOpen/RasterIO), writer edit in marine_tiled_raster_store tile_io.cpp |
2026-09-18 09:31 -04:00
| 2 | GGGS as OGC TileMatrixSet 2.0 JSON | KEEP the full document (polar variableMatrixWidths included) as definition of record, generated from gggs/core.h constants. Polar tile placement (75N coalesce 3, 85N coalesce 9) is a PASS/FAIL test for every manifest/pyramid technology in #3. Mid-latitude variant only if a kept tool needs it (none yet; deleted). | GDAL 3.8+3.12 parse it; GPKG raster driver rejects variable widths (not needed: GPKG raster also refuses 2-band Float64 + NaN); tile lands at zoom 10 col 13419 row 7323 (= file 10_17252_13419, row flipped). Roland: polar deployments are first-class. |
| 3 | Manifest + pyramid addressing: GTI (GDAL>=3.9) vs STACTA (GDAL 3.8) | KEEP GTI, target Ubuntu 26.04 / lyrical (today's C++ compositing reader stays until the platform moves); DROP STACTA | **GTI PASS all**: level-10 window bit-exact; chained overviews bit-exact at L9 and L3 once every level index shares one level-0-aligned extent (-te); polar 75N tile read as 2880x960 and 85N as 8640x960 with correct values (GTI indexes footprints, no TMS needed); mixed-level index composites the finer tile over the coarser's NaN holes (74711 cells equal, 3320 filled); C++ open 66 ms. **STACTA FAIL**: reads level 10 exact and L9 via overview, but (1) polar: does the variable-width column lookup yet reads the tile as un-coalesced width -> 33% valid at 75N, wrong column at 85N (both 3.8 and 3.12); (2) sparse coverage: an absent tile inside the limits box is a fatal read error, and our coverage is 138 tiles in a 5953x12439 box; (3) needs a symlink tree in TMS numbering (rows flipped, columns x coalesce). |
| 4 | STAC as catalog + coverage manifest + fingerprint container | KEEP: STAC Items + Collection are the record; GTI derived from them. Replaces ADR-0013 D3 manifest, #389 source list, cube build_fingerprint.json; re-scope #389 first | pystac writes 153 Items + 1 Collection in 0.8 s (597 KiB JSON, 1.5 MB on disk); Items validate against the projection/file/processing/version schemas; a GTI index DERIVED FROM the STAC items (no gdaltindex) passes every component-3 check incl. polar -> STAC = source of truth, GTI = reader view. STACIT (GDAL 3.8 reader of STAC items) with RESOLUTION=HIGHEST reads level 10 bit-exact and tolerates sparse coverage (0 errors), but FAILS polar: places a 3x-wide-pixel tile at single width (33% valid), so no jazzy-era fallback either. GeoParquet: no Arrow driver in Ubuntu's GDAL on 24.04 or 26.04 -> JSON only. Gotchas: proj:transform is affine order not GDAL order; STACIT resolves relative hrefs against cwd, not the file. Fingerprint written into GDAL_METADATA at COG creation, read back by C++ gdalinfo, file stays COG-valid. |
| 5 | Snakemake as the regenerate walk, content fingerprint as trigger | KEEP: fingerprint refresh as a pre-step; build_depth_overviews gains a per-parent mode | Snakefile: leaves = 69 native tiles, 82 parents (levels 9..0) each depending on children's .fp sidecars, then STAC, then GTI-from-STAC. Full build 15 s (stand-in per-parent fold; the real build_depth_overviews is one batch call and would need per-parent work units to benefit). Nothing changed -> "Nothing to be done". Change ONE tile's content -> exactly 10 parents rebuilt (one per level) + STAC + GTI, 7.8 s. Touch all 69 tiles (mtime only) -> 0 rebuilds, PROVIDED fp.py runs as a pre-step and resets an unchanged tile's mtime to its sidecar's; as an in-DAG rule the fingerprint job cascades to 60 folds. Snakemake 9 checksums small inputs (touching .fp sidecars alone -> nothing to do). Snakemake 7.32 (26.04 apt) parses the same Snakefile; its state dir is not shared with 9 (not verified further). Gotcha: a container running as root left 182 root-owned files in the bind mount; run containers with -u. |

## Component 10 (git-annex) — design notes from the 2026-09-18 conversation, to shape the bench test
- Scope: sources/ only (bags, ENC, datum grids, priors); never the quantity stores.
- Boat use: gabby registers each FINISHED bag (metadata.yaml present) with `git annex add` on a timer, unlocked+thin so readers see plain files.
- CURRENT route: gabby → salmon, reached by two hostnames/IPs → TWO git-annex remotes to the SAME repo (same UUID, one location record): `salmon-wifi` (no cap) and `salmon-starlink` (`remote.salmon-starlink.annex-bwlimit`); the timed job tries wifi first and falls back to Starlink, no path check needed; salmon is a `transfer` repo pulled by this machine (`git annex get --from salmon`); this machine → NAS (directory special remote, importtree to keep the tree browsable). No cloud credentials on gabby or salmon.
- FUTURE (Wasabi is an idea, not the plan): this machine → Wasabi as a third backup (S3 special remote, custom host, chunked, full key ONLY here, embedcreds=no); optionally gabby → Wasabi over Starlink with a boat-only Put/Get-on-one-prefix key.
- Bandwidth: cap is a property of the salmon-starlink remote; Starlink cap sized to leave headroom for udp_bridge (helm admission floor); docked = the wifi remote succeeding; git-annex re-reads config per invocation (timer-driven, not the assistant daemon). If headroom is not enough, `tc` priority for the ROS bridge on the Starlink interface, annex cap = ceiling only.
- Retention: numcopies 2; gabby `source`, salmon `transfer`, this machine + NAS + Wasabi `backup`; `drop --auto` on gabby/salmon. Wasabi object lock = data-of-record copy. Check Wasabi terms: min storage duration, min billed volume, no egress fees.
- To verify on the bench: Wasabi region host string; annex.bwlimit vs annex.rsync-options on the 10.2024 build; S3 chunked resume over a dropped link; importtree over the existing /mnt/nadata/map2026asv tree without moving bytes.

## Component 6 (BlueTopo tile shape + cleaning marks) — reframed 2026-09-18 (Roland)
- NO cleaning process or tool exists in the workspace; Roland has only cleaned MBES data in Qimera, whose edits
  stay in its project / export formats — that is the workflow the store replaces, not a feed. Do NOT design the
  mark schema from Qimera.
- The prototype SYNTHESIZES marks from the real Massabesic blunder case: (a) an AUTOMATIC mark from the blunder
  gate (echoboats#488) over one tile; (b) one HAND-WRITTEN manual mark in the form a future explorer action would
  produce (polygon + time window + reason). Test both schemas against each other: BAG tracking list (node-level,
  edits to the product) vs MB-System .esf / Kluster status flags (sounding-level, edits to observations); see
  which one the link step wants.
- Rev 3 states the gap plainly: cleaning is an UNBUILT process; its manual tool is a survey-explorer feature
  (R23: explorer shows the processed rung with its source data); the schema must accept both a gate and a person.
- Consequence for the spine: marks applied at link time require observations to keep individual soundings →
  feeds "what an observation may bake".

## Component 7 (correction schema) — finding 2026-09-18: the RTK datum correction is per SITE and per DAY, derivable from the bags
`/bizzy/mavros/gps_rtk/send_rtcm` (mavros_msgs/RTCM) is recorded in 142 of the season's main bags. `scripts/rtcm_station.py`
decodes RTCM 1005/1006 (base ARP ECEF + ITRF-year field). Findings from one bag per period:
station 42 (Apr 24, Jul 21, Sep 2) 42.8627N 70.8903W h-10.3 (NH/MA coast); station 41 (Jun 15) 42.6300N 71.2715W (Lowell MA);
station 4017 (Aug 5, Lewes DE) 38.7875N 75.1615W; stations 645/649 (Aug 21) 39.9827N 75.2220W (Philadelphia), 4 cm apart =
network-RTK virtual reference switching. ITRF-year field = 0 (unspecified) in ALL → the frame is NOT in the stream; it comes
from each caster's published station table (MaCORS = NAD83(2011) epoch 2010.00; DE/PA networks UNVERIFIED).
Schema consequence: the datum correction is DERIVED from the source (decode base ECEF → match caster station → caster frame),
not hand-entered; the only curated input is a casters→frame table. `.par` has no keyword for this (SONAROFFSET/NAVFORMAT are
not it) → first Q2 extension. Also: a correction scoped "platform for a date range" (mounting) has no .par home either.

## Component 7 (correction schema) — DECIDED IN CONVERSATION 2026-09-18 (Roland: "sounds good")
- DROP MB-System `.par` as the FORMAT (covers ~1/3 of the season's 46 rows); keep a few keyword NAMES for kinds.
- The season's list sorts into FIVE mechanisms; only the last two are correction records:
  1. READER bugs (57x beamwidth, TPU formulas, tide terms): fixed by regenerate; the code version in the fingerprint is the record.
  2. GEOMETRY: dated platform-description (URDF/tf_static) revisions with a validity interval, applied at link time instead of the
     bag's tf_static (Kluster vessel-file pattern; the "platform for a date range" scope .par lacks).
  3. DATUM: derived per bag from recorded RTCM (base ECEF → caster station → caster frame) + one curated casters→frame table;
     EGM96 round-trip and EKF-origin-Z windows = per-interval flags on a nav source.
  4. GATES = automatic cleaning marks (component 6).
  5. Correction records proper, as STAC Items in a `corrections` Collection: applies_to (bag id | platform+interval),
     kind, parameters, evidence, reviewer, superseded_by. Kinds: per-bag facts (M3 +6.03 s skew, walking-offset bag,
     missing topic, lake datum 52.3→48.88), and DECODE REVISIONS = WRITER bugs keyed by producer+version range (sidescan
     sample0/sample-rate/frequency) — one record per defect, deterministic, applied by the observation builder; not
     silently in code. LOSS (dropped M3 beams, AML NUL, missing topic) = provenance notes on the source, not corrections.
- NEW REQUIREMENT for rev 3: bags must record producer/driver versions (rosbag2 custom metadata; survey experiment 8);
  fallback = infer from date + deployment log (fragile).
- Prototype: write the season's genuine correction Items + one geometry revision, point the link step at them for one
  Massabesic bag, rebuild a tile, compare BY VALUE with the retrofitted bag's tile → proves "bags never touched".

## water/ — first product DECIDED 2026-09-18: surface sound-speed series with gap fill
Per platform per survey day, derived (sources: raw AML sentences since 07-29 + temperature). Each sample: value,
status {measured | estimated-from-T | interpolated}, uncertainty → flows into sounding uncertainty (outer-beam
refraction). Lake: S=0 so T suffices (worked at Massabesic during brownouts). Sea: S open (~1.3 m/s per PSU) —
candidates: cast-of-the-day fixed S, regional climatology, uma#300 inversion. Fill process versioned + regenerable;
observation builder reads the filled series at link time. Prototype after component 9: one Massabesic brownout day.
Rev 3: one paragraph under water/.

## Open rulings for Roland (from the corrections list)
1. Which nav source was primary in Aug (SBG per PR#341 06-27 vs FCU per PR#463 08-25)? Decides the relink trajectory.
2. RESOLVED 09-18: ellipsoidal_fix_node LANDED in rolker/seafloor_echoboat_project11 (echo_helm, commit e491740 2026-08-21, hardened a80e095/f7de3df 08-23, branch jazzy); wired on BizzyBoat in core_launch.py + bizzyboat.yaml (6a91ccc 08-21, fcu position -> mavros/global_position/global_ellipsoidal). So the +0.626 m EGM96 bias is a per-interval nav-source flag: bags BEFORE 2026-08-21 carry it (mechanism 3), bags after do not.
1. RESOLVED 09-18: SBG primary 2026-06-27 (fb4a7ae) -> 2026-06-29 ~11:45 EDT (field commit 34bf84c after the operator's 10:57 decision; reason in the config comment: SBG driver does not apply the IMU->base_link rotation to attitude, ~2 deg roll offset in sea_surface_layer). Bags 06-27, 06-28, first 06-29 outing = SBG-sourced; all others FCU with SBG fallback (sub-second blips only). The active_sensor topics were recorded only from 08-20 (e8b3b58): 25 bags show fcu, 151 earlier bags have no such topic -> the window is a DECLARED interval record (mechanism 3), not derivable per bag. Scan detail: logs/active_sensor_scan.md. Truncated/unreadable bags to mark in the sources catalog: 2026-06-08T15-08-16, 2026-06-23T13-03-52, 2026-08-03T19-22-07.

## NEW SPINE DECISION 0 (added 2026-09-18, Roland: "sounds good"): the store has NO defined reference frame
ROS NavSatFix "WGS84" = a datum ensemble (realizations differ by up to ~2 m, no epoch); RTK positions were NAD83(2011)
epoch 2010.0 labelled WGS84. Store declares nothing: registry `datum` empty, coverage manifests + today's STAC Items say
EPSG:4326 (same ambiguity), TMS says CRS84. GGGS L10 cells are 0.9 m → a 1 m frame offset moves a sounding one cell;
NH drifts ~2 cm/yr in ITRF → without an epoch the seafloor slides through the grid.
Decision to take (BEFORE source identity, ahead of depth fold): ONE horizontal+vertical frame + reference epoch for the
whole collection. Candidate: ITRF2020 @ 2020.0 (own PPP base produces it; ≈ WGS84(G2139) to cm; PROJ has the
time-dependent NAD83(2011) transformation; NOAA's 2022 frames derive from ITRF2020@2020.0). Obligations: every source
carries frame+epoch (derived: RTCM decode; declared: products); every ingest transforms into the store frame
(mru_transform#47 = the live instance; importers = the product instance) and names the transformation in provenance;
the declaration is written in STAC (specific EPSG, not 4326), coverage manifests, TMS crs, registry.

## SPINE DECISION 0 — DECIDED 2026-09-18 (Roland: "I think ITRF2020 makes sense")
Store frame = ITRF2020, reference epoch 2020.0, ellipsoidal heights in the same frame. Considered and not blocking:
all US products/CORS are NAD83(2011) (PROJ time-dependent transform, needed in one direction regardless); fixed epoch
⇒ plate-motion correction (~12 cm in NH for 2026 obs; every ingest carries the observation epoch); mru_transform#47
gains a PROJ dependency; NATRF2022 ≈ ITRF2020@2020.0 is a bet until NOAA publishes; verify the exact EPSG codes in the
PROJ database before writing them into STAC / TMS / registry. Goes into rev 3 as spine decision 1.
| 11 | rclone bisync + superset rule for store replication | KEEP: bisync moves bytes and resolves nothing; comparator decides by input-set superset; three constraints become design rules (write only changed Items; never sync derived GTI views; never rename store dirs) | Both platforms ship rclone 1.60.1 (no --conflict-resolve; 1.66+); 1.60 keeps both versions as `..path1/..path2` = the wanted "resolve nothing". S1 superset (gabby adds a bag, rebuilds; shore untouched): 114 files propagated, no conflicts — correct. S2 incomparable (each side corrects a different tile, both rebuild): 372 conflict pairs; comparator (18 lines over items.json native checksums) says WINNER=both, incomparable, serve designated — correct. BUT only 14 of 153 STAC conflicts differ in content: pystac rewrites every Item on every build and bisync 1.60 compares size+mtime (--checksum has no effect on its change detection) -> the catalog writer must write only changed Items (Snakemake-style), or the sync will flag the whole catalog every build. GTI .gpkg files are byte-nondeterministic across identical builds -> never sync derived views; sync the STAC record + tiles, regenerate GTI locally. S3 rename overviews/->pyramid/: 184 deletes + 184 copies, bisync has no rename tracking -> directory layout is part of the contract, never rename a store dir on one side. |
| 8+9 | Kluster-shaped observations (Zarr) + SBET-shaped trajectories (Parquet) + link at pose time | KEEP (reverses ADR-0006 D2 baked pose); trajectories must declare frame, height, attitude convention and geometry revision | Bag 2026-06-22 Massabesic, 20-min window: 62,802 pings @52 Hz, 254 beams padded (beam count varies 197–254 per ping), flags all DETECT_OK (driver dropped invalid beams then), 53 MB Zarr, GDAL 3.8 reads it. Trajectories: FCU 10 Hz, SBG 26.7 Hz, deliberate 120 s gap. Link (stand-in mean gridder): **A vs A2 (bag tf_static vs 06-27 geometry revision, applied at link time): median +0.133 m, p5–p95 0.123–0.143 over 54,671 cells = the known 13.5 cm M3 offset reproduced without touching the bag.** Gap: 7,648 pings (12.2 %) linked with no pose, excluded, never interpolated. A2 (EGM96 -0.626 applied) vs the REAL store: median −0.60 m over 54,794 cells → the store's Massabesic tiles CARRY the +0.626 m EGM96 bias (regen from pre-08-21 bags, uncorrected). FCU vs SBG: median −0.51 m vertical (matches echoboats#156's 0.59 m) but ±2 m spread + coverage mismatch → SBG attitude convention (NED vs ENU before use_enu on 06-27) must be DECLARED in the trajectory container; see heading check. Provenance written into each tile's GDAL metadata (observations id, trajectory source+frame, geometry id, corrections, gridder, store frame not yet applied). Source id = sha256 of the .mcap (PROVISIONAL, spine decision 2). Producer version: not in the bag. |
  8+9 heading check: FCU−SBG heading median −117°, p5/p95 −172°/+176°; roll FCU −0.33° vs SBG +1.47°; pitch +1.84° vs −0.85°; position agrees to 0.15 m horizontal, −0.52 m vertical after EGM96. ⇒ on 06-22 the SBG quaternion is NED/FRD (use_enu came 06-27) and the SBG→base_link mounting (echoboats#155, −0.577 m FLU + 180° roll, never applied) is missing. DESIGN RULE: a trajectory record declares its attitude convention (ENU/FLU vs NED/FRD) and the platform-geometry revision it was built with; a source without both is not a valid relink input. SBG relink deferred until those records exist.
| 7 | Correction records as a STAC `corrections` Collection, each Item OWNED by one builder (observations / trajectory / link), looked up by platform + topic/frame + time interval | KEEP: derived rules preferred (idempotent); EGM96 rule to be redone with the GPSRAW method | Archived M3 bags turned out to be RETROFITTED IN PLACE on 2026-07-02 (sonar bags 06-22, 06-24: new stamps + tf_static (-0.29,0,-0.28); no originals on the NAS; nothing in the bag says so; main bags still carry the install-log mounting). Test therefore: 20-min window of 06-24 (73,048 pings) as truth; local copy with the recorded faults re-injected (+6 s on 45,726 pings before a step, install-log mounting). Retrofit tool (--out, never in place) vs catalog path: STAMPS identical 73,048/73,048; TILES identical 43,217/43,217 cells, max |dz| 0.000. Both vs truth: exact except the 200 pings after the step, which the windowed rule over-corrects by 6 s — the same residue found in the archive (≈200 mis-stamped pings per skew step). Derived rule re-applied to the retrofitted bag measures 0 → IDEMPOTENT (a constant "-6 s" record would double-apply). Uncorrected faulted data vs truth: +0.133 m median (geometry) and up to 2.6 m, ~1,000 cells displaced (timing). EGM96 Item: my "derived" rule median(raw/fix − global) gave +0.87 m — WRONG, it measures the 0.89 m antenna lever arm (gnss_forward z = 0.890), not the geoid-model difference; the method must be echo_helm's (receiver undulation from GPSRAW alt_ellipsoid − alt vs EGM96-5). Trajectory identical across all paths, so the comparisons stand; absolute heights of this run are not. |
  (correction to the 8+9 row: "bag tf_static" there meant the MAIN bag's install-log value; the sonar bag had already been rewritten.)
| 6 | Cleaning marks: sounding-level records keyed by (source_id, receive_time_ns, beam); area marks resolved to soundings at mark time (polygon kept as intent/provenance); node-level (BAG tracking list) only for products that are never regenerated | KEEP | 06-24 window, 8.6 M soundings after de-duplication. Gate v1 (|z − cell median| > max(1 m, 5 MAD)) rejects 800; a synthesized manual mark rejects 4,341 in a 25 m box; a tracking list edits the 50 cells the gate changed most (0.02–1.37 m). Relink after a synthetic 1.1 m horizontal shift (order of the NAD83(2011)→ITRF2020 change): sounding-level marks select the SAME soundings (800/800, 4,341/4,341); the area mark applied by position selects 3,983 = only 3,070 of the person's 4,341 (71 %) plus 913 they never saw; the tracking list is STALE on 47 of 47 cells (3 land on empty cells). Mark files: 5 KB + 9 KB Parquet. Not tested: BlueTopo contributor band + RAT (needs a multi-source tile). Requirement for the spine: observations keep individual soundings with identity = receive time + beam (never the corrected stamp), exact duplicates dropped at build. |

## ARCHIVE FINDING 2026-09-18: 2026-07-02 rewrites of M3 sonar bags DUPLICATED detections
Bags rewritten 2026-07-02 (retrofit round 2) run at 35–61 Hz vs the M3's 25–29 Hz: 06-22 (42.5), 06-24 (42.9), 06-25 (61.1),
06-26 13-45 (60.3), 13-57 (35.3), 15-12 (44.3); plus 06-26 13-41 (39.9, rewritten 06-27). Sampled 06-22 and 06-24: nearly every
detection present twice (same receive time + stamp, one connection); a 20-min 06-24 window: 73,048 msgs = 36,524 pings × 2.
Bags as recorded and those rewritten 06-27 (06-12..06-19) run at normal rates; untouched 06-10, 06-11 have 0 duplicates.
Cause not established (the retrofit tool on our local copy did NOT duplicate). Impact: the 07-02 Massabesic rebuild counted
those soundings twice → depths ≈ unaffected, CUBE hypothesis weights/uncertainty understated on those days.
Originals: not on the NAS; Roland said "not now" before this was known.
Originals search 2026-09-18 (read-only, Roland approved): salmon /home/field/data/logs/gabby/logs/bizzyboat_sonar holds the SAME 07-02
rewrites as the NAS (identical sizes + mtimes); no .orig/backup bags anywhere under /home/field/data/logs. gabby unreachable (timeout on
gabby, no route on gabbyz) — still unchecked. Pattern: as-recorded bags are split into 5-min .mcap files; every rewritten bag is ONE file
(the rewrite merged the splits — candidate mechanism for the duplication, unverified). INDEPENDENT RAW RECORD: mercat's QINSy project
Massabesic2026/Database holds per-line QINSy .db files for 06-22..06-25 (e.g. 0276_260624_1613_Mainlines WGS84 - 0002.db) — the same M3
pings logged by QINSy, untouched by the ROS rewrites; proprietary format (Qimera/QINSy).
| 10 | git-annex for SOURCES on the dev machine: repo ~/data/annex_trial, NAS sonar folder as a read-only directory import remote (importtree), local copies as a cache | (pending: NAS trust policy) | Trial on the 06-26 sonar bags (9.2 GB): import 2 min 6 s (≈245 MB/s on large files; import copies content locally); re-import with nothing changed 0.7 s, reads no data; `metadata.yaml` kept in git, `.mcap` annexed (SHA256E keys = content identity); annexed files are read-only (append → Permission denied — an in-place retrofit would have failed); rosbags reads bags through the annex symlinks (observation build OK). Drop of a local copy REFUSED: git-annex keeps import remotes untrusted (semitrust has no effect); full trust is Roland's policy call (the auto-mode classifier denied me setting it). Throughput: local NVMe 3.2 GB/s (O_DIRECT) vs NAS 112–245 MB/s; the Python observation builder is CPU-bound at ≈66 MB/s, so ONE job gains little — the gain is parallel regenerate (8 cores ≈ 530 MB/s demand > NAS), repeated reads of a working set, and I/O-bound scans. Also: 06-26T13-57 sonar bag = 72,808 pings + 60,614 exact duplicates (45 %). |

## SPINE DECISION 1 — source identity — DECIDED 2026-09-21 (Roland)
Premise (Roland): bags are never rewritten, not even to fix writer bugs; a writer bug fix ships WITH a
decode revision that the store pipeline applies to the identified faulty bags (component 7's mechanism).
An identity therefore need not survive a rewrite — a rewrite is a new source.
A. CONTENT identity, not declared. Declared attributes (platform, recorder, start time, producer versions)
   are lookup metadata in the STAC Item, never the id. Rationale: the 07-02 rewrites were invisible under
   the declared name; content identity enforces the no-rewrite rule instead of trusting it.
B. Merkle-style bag id = sha256 over the sorted "<split filename>\t<file key>" lines of the *.mcap files
   only (file key = git-annex SHA256E). Filenames INCLUDED (split order is meaningful, rosbag2 names them
   deterministically). metadata.yaml EXCLUDED — verified 2026-09-21: `ros2 bag reindex -s mcap` leaves
   every .mcap byte-identical and regenerates only the yaml (28-split 2026-04-08 bag; a metadata-less bag
   reindexes fine, so truncated bags need no special case). Backups/.orig/logs in the dir excluded ("sensor
   data files only"). Single-file sources (.all, QINSy .db) = their file key. `mcap recover` rewrites the
   file → a NEW source with a lineage note, by construction.
C. Follows: no special case for truncated or in-progress bags (no id until closed). Reprocessing never
   changes a source id; it changes the observation FINGERPRINT (source id + decode/geometry/datum revs).
   Already-rewritten June M3 bags: ids as they are now + provenance note "rewritten 07-02, original not retained".

## SPINE DECISION 3 — rung axes — DECIDED 2026-09-21 (Roland)
The single ladder `published | reference | draft | processed` mixed two axes (origin/authority vs our processing
state) and made our own released product exist twice (processed + published). DECIDED: two recorded fields on
every Item — `origin ∈ {ours, external}` and `state ∈ {automatic, curated, released}`; a release is a state
change on a curated product, never a second copy. The four rung names survive only as labels for the common
cells (processed = ours/curated, draft = ours/automatic, published = released, reference = external/curated).
THE STORE OWNS NO PREFERENCE ORDER (Roland): "different consumers can decide how to combine sources" — CAMP
layer order (adjustable today), survey explorer (a different order for exploring than for collecting), a
future GeoZui4D capability to explore the stores in 3D with CAMP-like layer selection/ordering, and costmap
generation with its own default overridable by parameters — "I don't think the stores should decide how to
handle the layers." Consequence accepted: two consumers may show different data for the same tile, by design;
a consumer's ordering is an input to the fingerprint of anything it builds. Intra-cell ordering (two
external/released surfaces, two overlapping curated builds) is therefore ALSO the consumer's — not a store rule.
What the store must provide instead: per-Item origin, state, inputs/fingerprint, uncertainty, time, resolution
+ levels, and enumeration (STAC Collection/Item search) so every consumer can build its order. Layout: directory
keyed by STATE (`depths/curated/`, `depths/automatic/`, `depths/released/`); origin lives in the Item.
Replica superset rule (component 11) applies within a cell, unchanged.

## SPINE DECISION 3 — NAMING + LAYOUT settled 2026-09-21 (Roland)
- States: `draft | reviewed | published` (not automatic/curated/released — "curated" invokes museum displays;
  "reviewed" says what a person did; two of the original rung names kept, `reference` was the origin-axis rung).
- Origin directories inside each state, symmetric: `surveyed/` (our own surveys) and `imported/` (material
  received from outside). Not "ours". Required, not optional: consumers order by origin and R8 keeps source out of
  per-cell provenance, so origins cannot share one tile tree. Trajectories/observations have only `surveyed/`.
- Records category renamed `curation/` → **`revisions/`** (corrections, cleaning marks, geometry revisions, casters
  table). Q9's `curation/` retired for the same museum reason; `updates/` rejected: collides with ENC edition/update
  terminology under sources/enc and with "updating the store".
- Layout: depths/{draft,reviewed,published}/{surveyed,imported}/ (COG tiles + STAC Items + collection + GTI view).
- Correction to my example: the double-storage case was described as "our S-102 export"; we never discussed
  producing S-102 — only importing it. The realistic released product of ours is a BAG. Argument unchanged.

## NBS compile rule (Rice, Wyllie, Gallagher, Geleg, OCEANS 2023 — read 2026-09-21) → CONSUMER DEFAULT candidate
Location-based, node-level: survey quality score DECAYED by time (survey end − tile-generation reference) AND locality
(changeability); winner per node = (1) highest decayed score, (2) finest resolution, (3) least depth, (4) source name.
Two passes: within each bucket (qualified | unqualified | sensitive | precompiled), then across buckets per pipeline.
Then linear interpolation with conservative uncertainty; RAT flags bathymetry_coverage vs survey_coverage. Averaging in
the compile REJECTED by NBS (obfuscates lineage) — relevant to spine decision 2 (depth fold): our fold is an OVERVIEW,
not a compile; the decision must say why that difference makes a representative fold acceptable there.
Acquire compares stored metadata + a HASH of the bathymetry before re-downloading (= our content identity, decision 1).
"Engineered to restart from source data … removing undocumented and subjective work" = our R7. Score construction is in
Wyllie et al., US Hydro 2017 (not read). Withdrawal of a source: undescribed → shared open question for rev 3.

## VIEWS — decided in conversation 2026-09-21 (Roland)
Bathymetry ALIGNS WITH NBS where practical. The store holds non-bathy quantities too, so the model must separate
BATHY-SPECIFIC rules from what GENERALIZES. Consumer-side rule sets become named, documented **views**:
- **Navigation-surface view** — lineage: Shep Smith's navigation surface (UNH MSc 2003, the NBS ancestor) and Roland's
  work on CCOM's Chart of the Future. Safety-of-navigation semantics (NBS compile hierarchy, least depth, coverage/
  uncertainty carried). This view is how bathymetry enters the COSTMAP and chart display.
- Other views (geology, bottom classification, …) apply DIFFERENT rules over the same store — e.g. representative
  rather than shoalest statistics, backscatter/sidescan-led selection.
A view = selection + ordering + fold/statistic semantics + output datum, owned by the consumer side and versioned as a
fingerprint input; the store stays view-agnostic (decision 3). Implication for spine decision 2 (depth fold): fold
semantics are VIEW properties, so stored overviews should carry what every view needs (BAG VR RESAMPLED_GRID precedent:
MIN / MEAN / COUNT, +σ) rather than one folded value — to be measured, not assumed.

## Engineering data — 2026-09-21 (Roland): Kongsberg M3 `.all` files are ENGINEERING DATA, not store sources
They are kept and indexed (they already sit in the archived sonar tree on NAS/salmon and get annex keys + an Item),
but nothing in the store derives from them; catalogued with `role: engineering` so they are findable and excluded from
the source set that observations/products are built from. CORRECTS decision 1's "single-file sources (.all, …)" line:
the single-file rule (id = file key) still applies to any single-file source, but `.all` is not one. QINSy `.db` on
mercat: unchanged (an independent raw M3 record; not ruled on today).

## SCOPE FRAMING — 2026-09-21 (Roland): the stores are an EXPERIMENT in robot-centric capabilities, NOT a replacement
for the traditional hydrographic pipeline. QINSy `.db` files (mercat) belong to the PARALLEL traditional path (QINSy →
Qimera/CARIS-style processing → deliverables), which is sometimes used while the stores are being developed; they are not
store inputs. Consequences: the BAG/S-102 publish path is INTEROPERABILITY, not a core requirement; NOAA alignment means
lineage and format compatibility with that world, not competing with it; the "interactive cleaning editor" gap is less
urgent (the traditional path covers curated deliverables); rev 3's Purpose section must state this framing explicitly.

## Spine decision 2 EVIDENCE — MIN vs MEAN fold on the Massabesic depths (2026-09-21, scripts/12_fold_measure.py)
69 level-10 tiles (~0.9 m cells), 13.6 M cells; band 1 = bed ellipsoidal HEIGHT (+up, 17.5–50.5 m), band 2 = uncertainty
(median 0.15 m, p95 0.67 m); 1.28 M blunder cells masked (|h−45|>30 m or σ>10 m — the echoboats#488 gate problem, still in
the store). Gap = shoalest (MAX height) − mean, per parent cell with ≥ half its children:
  2×2 (1.8 m): p50 0.025 m, p95 0.17, p99 0.42; gap > σ in 0.4 % of parents; > 0.5 m in 0.6 %
  4×4 (3.6 m): p50 0.080 m, p95 0.40, p99 0.77; gap > σ in 14.5 %; > 0.5 m in 3.1 %
  8×8 (7.2 m): p50 0.185 m, p95 0.74, p99 1.28; gap > σ in 56.5 %, > 2σ in 18 %; > 0.5 m in 11.4 %, > 1 m in 2.3 %
Reading: one fold step is within uncertainty almost everywhere; by three steps a mean fold hides the shoalest depth by more
than its own uncertainty in half the cells — on a SMOOTH lake bed (rock/wreck areas would be worse). Supports: overviews
carry MIN + MEAN + COUNT (+σ); nav-surface view reads MIN; R19 (safety never from folded levels) stands on evidence.

## σ-FOLD CANDIDATE EVIDENCE — 2026-09-22 (mws_measure_sigma_fold, uma#397; the rule stays OPEN)
Spine decision 2 stored a σ band but never said how to fold one (rev 2's "mean and max of the children" is two
numbers). This is the measurement §7's open rule is to be decided from — 140 processed native depth tiles,
Massabesic and Shoals BLENDED (a deliberately mixed population), 3 fold steps. Truth = the population standard
deviation of the native cells under a parent; a rule COVERS a cell when its σ is at least that spread; the truth is
accumulated exactly through every step, so the candidates are scored against the data and not against a fold of
themselves. Each candidate is carried forward in its own right.
  step 1 (level 9): 3,459,403 parents (3,426,861 with a spread); mean true spread 0.080 m
    pooled σ 0.466 m, covers 99.99 %, σ/spread 13.6 | max_child 0.544 m, 99.77 %, 15.5 | mean_child 0.427 m, 99.60 %, 13.2
  step 2 (level 8): 886,810 parents (880,768); mean true spread 0.170 m
    pooled 0.554 m, 99.98 %, 5.93 | max_child 0.784 m, 99.61 %, 8.12 | mean_child 0.454 m, 98.50 %, 5.48
  step 3 (level 7): 230,283 parents (229,195); mean true spread 0.309 m
    pooled 0.696 m, 99.98 %, 3.45 | max_child 1.176 m, 98.15 %, 5.75 | mean_child 0.496 m, 84.66 %, 2.91
Reading: pooled never under-claims and over-claims least by step 3; max_child also covers but is the loosest number at
every step; mean_child is the only candidate whose coverage DEGRADES with fold depth (99.6 % → 84.7 %), i.e. by three
steps it under-claims the spread in one parent in six. All ratios sit far above 1 at step 1 because the native per-cell
σ (the ingest's own uncertainty, median 0.15 m on the Massabesic tiles above) dominates the spread BETWEEN neighbouring
cells — a property of the data, not of any rule.
RULE OPEN (Roland 2026-09-22): writers emit σ as nodata and record `sigma_fold: undecided`; the decision when it comes
is a new fingerprint, never a migration. The measurement recommends nothing, and a test asserts it does not.

## SPINE DECISION 2 — overview levels — DECIDED 2026-09-21 (Roland: "follow BAG VR's precedent")
1. A folded level stores MIN, MEAN, COUNT and σ (mean σ + max σ of the children) per parent cell — BAG VR
   RESAMPLED_GRID precedent — never one folded value. Views choose the band: nav-surface view reads MIN (+ max σ),
   non-navigation views read MEAN; COUNT = the parent's lineage / completeness.
2. R11 restated: the representative fold is what non-nav views read; the shoalest is STORED, not "possible later".
3. R19 on evidence: safety never decides from a folded level; a folded MIN may serve as a conservative screen only
   (shoaler-or-equal than truth); decisions resolve at native level. NBS's no-averaging rule applies to the COMPILE
   (our native level); folded levels are derived summaries with COUNT as lineage and are never the product.
Evidence: fold_measure_massabesic_2026-09-21.txt (mean hides shoalest by > σ in 56 % of 7.2 m cells).
Cost: build_depth_overviews → per-parent multi-band writer (component 5 per-parent mode); dev pyramid rebuild;
inform uma#389 (pyramid staleness) and uma#395 (LOD selection core).

## SPINE DECISION 4 — what an observation may bake — DECIDED 2026-09-21 (Roland: "it all makes sense")
RULE: an observation bakes only what is a pure function of the source bytes and a versioned decoder (bottom detection,
range/angle, amplitude/phase flag, ping counter, backscatter decode as a DECODE REVISION). Anything depending on a
revision record, a trajectory, or another store (timing offsets, attitude/position/heave, lever arms, datum, marks,
gates, sound-velocity correction, sidescan slant-range/altitude) is applied at LINK. Raw receive time stays raw
(identity). CACHE CLAUSE: expensive link-stage results (SVC, sidescan altitude) may be stored beside the raw variables,
tagged with the fingerprint of their inputs, never replacing them. CUBE per-sounding hypotheses are post-association
link outputs, not observations. Sidescan decided by principle; owed test = observation builder on one sidescan bag.
PROCESS (Roland 09-21): treat THIS MACHINE as the prototype for the new stores, build pieces targeting ~/data/world
right away, revise the design as pieces are tested. First tests: the sound-speed cast path (Shoals + Massabesic casts →
water/ → SVC at link with cache) and the sidescan path.

## REVISED 2026-09-21 (Roland): do NOT build in ~/data/world yet. The stores DEFAULT to ~/data/world but must never be
hard-coded to it; the prototype builds SOMEWHERE ELSE so nothing accidentally hard-codes the path. → build root = a
configurable parameter everywhere (env var / config), prototype target = ~/data/world_proto/store (or similar);
migration into ~/data/world happens by pointing the root at it, not by editing paths.

## SPINE DECISION 5 (features) — PARTIAL 2026-09-21 (Roland)
5.2 DECIDED: rasterization is ON DEMAND only ("as needed"), never a storage strategy for non-raster data. Reverses
ADR-0010 D2's rasterised chart layer as the record: the vector is the record, a raster is a derived view/product.
5.1 (category) OPEN — under discussion; 5.3 (shoreline) OPEN — needs source inventory per site (Shoals: ENC; Massabesic:
no chart → NHD/imagery/derived from topo-bathy + water level) and a nav-surface-view rule. Prototyping continues.

## Sound-speed cast path — TEST STARTED 2026-09-21 (Roland: process test only; cast FILE FORMATS NOT SET IN STONE —
he wants more thought on the files used for casts later). Readers are test adapters; the water/ profile Item schema is
PROVISIONAL. Prototype root = $WORLD_STORE_ROOT (default ~/data/world), here ~/data/world_proto/store — never hard-coded.
Inventory (NAS): Massabesic 1 cast 2026-06-16 (.asvp, AML A30894); 89 AML .asvp exports Jun–Jul; 44 raw .aml logs; QINSy
.svp exports (06-09 has a 1530.92 spike at 0.47 m → QC flags needed); Shoals 08-25/26 129 Valeport files (.bsvp + thinned
.asvp); 2024 DualDrix 4 Valeport casts local. surface_sound_speed/ tree = AML brown-out extraction.

## Sound-speed cast path — TEST RESULT 2026-09-21 (scripts/13_casts.py, 14_svc_link.py; prototype root ~/data/world_proto/store)
Process worked end to end: cast = single-file source (sha256 identity, Item from the .asvp header) → water/draft/surveyed
profile Item with per-sample flags → SVC at link via layered-Snell lookup → CACHE beside the observation keyed by the
fingerprint of (observation source_id, profile fingerprint, method, draft); raw variables untouched.
FINDINGS (Massabesic cast 06-16, obs window 06-22, 14.8 M soundings, draft ASSUMED 0.3 m — not in the bag):
1. The AML/Kongsberg .asvp export PADS the cast with a standard deep-ocean profile down to 12,000 m, and writes its first
   padding value (1595.07 m/s) AT the cast's bottom depth — it masquerades as a measured sample. QC must flag it (done:
   'padded'); the padding must NEVER be ray-traced. 16.9 % of soundings here reach below the 6.54 m measured cast.
2. Within the measured profile SVC vs straight-line (surface c) is millimetres: dz p95 < 1 cm at 0–6 m (near-isovelocity
   top layer, 1492.5 → 1488.5 m/s). Beyond it, the padding alone produces +0.26 m p50 / +0.44 m p95 at 10–30 m and up to
   1.2 m — an ARTIFACT. → the profile product needs an explicit EXTRAPOLATION rule (last value or gradient) flagged
   'estimated' with an uncertainty that grows with distance below the cast; cast depth vs survey depth is a real gap.
3. Cast surface c (1492.5, 06-16) vs the bag's transducer c (1489.2–1491.5, 06-22): 1–3 m/s in six days → the bag's
   surface series is the better surface value; prepend/replace the profile's top with it at link (water/ surface series).
Provisional schema and readers only (formats not settled). Follow-ups: transducer draft must be recorded (geometry
revision); Shoals Aug casts (Valeport, stratified salt water) are the case where SVC will actually move soundings.

## Vocabulary + owed study — 2026-09-21 (Roland)
- Say SOUND SPEED (scalar), never "sound velocity"; the ray-tracing step is the sound-speed correction. "Velocity" appears
  only when quoting another tool's field name (Kluster sound_velocity_correct, CARIS SVP editor, .svp extensions).
- OWED before settling cast-file details: closely study HydrOffice **Sound Speed Manager** (Masetti et al., CCOM/NOAA HSTB
  — the open-source cast tool: formats read/written, QC, extension below the cast, surface-speed handling, export to
  sonars, its DB of casts). Our profile product should align with it or explain why not.

## 2026-09-21 (Roland): cast-selection rules (which cast applies to which ping) are PROCESSING details with small blast
radius — they belong in the processing/view layer, not the store design; the store only needs casts enumerable by time and
position. Small-blast-radius details need not be decided up front. Contacts so far are HUMAN-generated and live in the
OPERATOR bags or related data (not in the boat's sonar bags) → the sidescan test must look there for the first features.

## GOVERNING PRINCIPLE — 2026-09-21 (Roland, mid-sidescan test)
"A lot of the details of how the data should be processed is still TBD as we experiment. Decisions that result from
processes that might change should be considered REVISABLE, and we should balance making the stores usable with
flexible enough to support research." → every decision in this log carries an implicit revisability class: STRUCTURAL
(identity, axes, categories, views, revisions-as-records) vs PROCESS-DERIVED (readers, schemas, QC rules, fold statistics,
cache methods, container formats) — the latter are provisional by default and change as processing is learned.

## Sidescan path — TEST RESULT 2026-09-21 (scripts/15_sidescan_obs.py, 16_sidescan_link.py, 17_contacts.py)
Decision 4's rule held for sidescan WITHOUT exception. Observation (10,399 pings × ≤2048 samples/side, 20-min 06-22
window, 75 MB): raw uint16 samples as recorded, PER-PING sample_rate / sample0 / samples_per_beam (range scale changed
mid-window: 30.5 vs 34.1 kHz, 2036 vs 2048 samples — these are NOT constants), the sonar's own nadir_depth (baked: a
measurement), the driver's reported sound speed (a PLACEHOLDER 1500.0, kept as recorded, never used). Link: sound speed
from the M3 transducer series in the same bag (1489.2–1491.5) — the placeholder would misplace the outer sample by up to
0.36 m at 50 m slant range → sidescan ranging is a LINK input, same as bathy; slant→ground flat-bottom with held nadir
(≤5 s; 10,398/10,399 fresh); water-column samples (slant ≤ alt) excluded (2.5 M); pose from fcu trajectory (9,359 pings;
the 120 s gap excluded, as in 8/9); NO sidescan mount in tf_static of this (rewritten) bag → ASSUMED at base_link, a
geometry revision is owed; 33.5 M samples placed, 255,530 level-10 cells, stand-in mean+count tiles under
store/sidescan/draft/surveyed/; ground-range CACHE keyed by fingerprint beside the observation (cache clause works).
Existing marine_sidescan_mosaic tier 1 (.sst1) bakes the earth→transducer pose — R15's dissolution confirmed as the
right direction; its DEM-orthorectification (tier2_processed) is the link-time step that would replace flat-bottom.
FIRST FEATURES: 47 HYPACK targets (Massabesic turret search, Aug 13–18, magnetometer context, no depth/notes) + 61 QINSy
targets (description + lat,lon only; no time, no datum statement) imported as vector records → features/reviewed/
surveyed/contacts/*.geojson (people marked them → reviewed). Both carry "WGS84 as labelled" positions (RTK datum finding).
No sidescan contacts exist yet on the NAS. A contact's natural record: position (+uncertainty), time marked, marked_by
(person/tool/auto), sensor context, description/class, provenance to the source line/ping, group. Supports 5.1 as
"quantity store with a vector container, same axes"; container (GeoJSON now) PROVISIONAL.

## CONTACTS ↔ STORE alignment — 2026-09-21 (Roland: the operator-marked contacts from OUR rqt sidescan plugin and OUR
Contact message are what matter; the store aligns with that design, and the contact design gets updated if the store work
shows it should. HYPACK/QINSy targets = foreign examples only.)
FOUND: marine_interfaces/Contact (ADR-0004: Autoware-style object; kinematics+shape as fields; geo_pose = DERIVED archival
value; curation block source/origin_kind/status/note/observation_ids/attributes; "unknown" by convention cov[0]=-1);
marine_contacts: ContactStore = CDR ContactArray persistence (forward-compatible with contact_manager uma#167) +
export_contacts_geojson; rqt_sonar_waterfall publishes /operator/sonar_waterfall/contacts. REAL DATA: 86 contacts,
operator bag 2026-06-29 (only day with any), boxes p50 11.5×5.2 m, all ORIGIN_HUMAN / STATUS_PROPOSED, source "sidescan",
frame bizzy/garmin_sidescan_port, geo_pose resolved (ellipsoidal ~55–60 m). Imported 1:1 by scripts/18_contacts_ros.py →
features/reviewed/surveyed/contacts (state DERIVED from origin_kind/status: human or confirmed → reviewed; auto+proposed →
draft; rejected kept with status).
GAPS in what the plugin publishes (86 of 86): observation_ids EMPTY (no provenance to the source pings → the contact cannot
be relinked when trajectory/geometry is revised; geo_pose is baked at mark time from TF); kinematics covariance = 0, not
-1 (claims zero uncertainty; violates ADR-0004 D4); id = per-session counter "sonar_waterfall-N" (not stable across
sessions — ADR says stable/UUID; the store had to prefix the bag id); classification/note/attributes empty; source
"sidescan" rather than "sidescan.port" (side only in frame_id).
DESIGN CONSEQUENCES (proposals for Roland; upstream interface → design thinking first, no implementation offered):
 1. observation_ids MUST carry the ping identities of the marked rows (sidescan source_id + receive_time_ns + side) — this
    is what makes a contact a deferred-pose object like a sounding; geo_pose stays the derived cache ADR-0004 says it is.
 2. Stable contact id (UUID or bag-id-prefixed) so contacts survive across sessions/days.
 3. Covariance −1 (unknown) or an estimate from box size + altitude; never 0.
 4. Container for features/contacts = the message itself: CDR ContactArray (the #167 manager's format) as the record +
    an Item; GeoJSON/GeoPackage DERIVED on demand (5.2). ⇒ spine 5.1 for contacts: a quantity store with a vector
    container, same axes, record = our message 1:1.
 5. Tension to resolve: ADR-0004 D5 puts curation (status/note) INSIDE the record, while the store keeps review as
    revisions-as-records + never rewrite. Reconcile by append-only ContactArray versions (each status change = a new
    fingerprinted version referencing the previous), not in-place edits — keeps history and the no-rewrite rule.

## CONTACTS — interim framing 2026-09-21 (Roland): a contacts REDESIGN is warranted but is a SEPARATE issue, after the stores
land and support where the redesign is expected to go. For now: sources (operator bags) are READ-ONLY; updates to contacts
are SEPARATE items (append-only); the result is some kind of DATABASE of contacts filterable by confidence, status, origin…
Question raised: tile pyramids as the organizing structure for contacts, or something better? (answer below in conversation:
records = append-only CDR ContactArray version files + Items; queryable DB = a DERIVED GeoPackage/SQLite index with an
R-tree + attribute columns, regenerated from the records; tile ids = a column for joins/replica sync, not the storage
structure; pyramid analogue for objects = display-time generalization, a view concern.)

## PURPOSE — Roland, 2026-09-21 (quote for rev 3's Purpose section, do not paraphrase away)
"This shows that we are designing a store that can serve as a marine robotics testing ground for some of NOAA's NBS
efforts but also go beyond to support alternative uses of sonars and related sensors including marine archaeology in
addition to some I've mentioned earlier." (Earlier-named uses: explore and work with collected data; no per-project/per-
purpose replication; nav info for robots; CUBE priming + costmap; fusing data kinds and passes; geology / bottom
classification views; the Massabesic turret search is itself an archaeology case.)

## Component 10 (git-annex) — KEY COMPARISON DONE 2026-09-21: salmon vs NAS sonar archive
No-content imports on both sides (salmon: /home/field/annex_logs, main ba22917; dev: ~/data/annex_trial, main 3b97441).
3,374 common paths — ALL 3,374 byte-identical (SHA256E keys / git blob SHAs), 0 different, 0 only-on-salmon; 43 files only
on the NAS = the m3_all/ engineering .all files (not in salmon's sonar tree). The 07-02-rewritten June M3 bags (06-22,
06-24, 06-25, 06-26 ×6) are IDENTICAL on both hosts → salmon is a faithful second copy of the rewritten state; no original
pre-rewrite bags exist on either (consistent with the 09-18 search). Dev import took ~27 min (08:52→09:19) for 231 GB.
Process note: my background "wait for the import" loop used `pgrep -f` with a pattern that matched its own command line
and spun for 3.5 h after the import had finished — use a `[x]` bracket in pgrep patterns, or wait on the PID.
Remaining for component 10: trust decision (salmon = verified 2nd copy, NAS not trusted — Roland 09-18) and step 2
(annex salmon's logs in place) — separate yes, tied to the boat→salmon transfer design.

## Shoreline prototype — RESULT 2026-09-21 (scripts/19_shoreline.py, 20_enc_coastline.py; spine decision 5.3 evidence)
Framing agreed (Roland, yes to all): the COASTLINE (imported: ENC COALNE at Shoals, USGS NHD polygon at Massabesic — no
chart exists for the lake) is a FALLBACK; the real product for the costmap is the NAVIGABLE-WATER BOUNDARY = depth contour at
the CURRENT water level; water level = a water/ series derived from trajectories (RTK height at base_link, ellipsoidal —
Massabesic 06-22: fcu 48.81 m [same +0.626 EGM96 bias as the tiles, cancels in depth], sbg 48.71, config full-pool 48.88).
Built under the prototype root: features/published/imported/shoreline/{nhd_massabesic_lake.gpkg (1 polygon, 33 rings),
enc_shoals_US4NH1BD.gpkg (19 COALNE, 17 LNDARE; the 1:20k US5NH1AG stops at 42.975 N, so Shoals comes from the 1:80k cell;
DSPM_VDAT=16, SDAT=12, edition 1 upd 8 of 2026-08-07)}, features/draft/surveyed/navigable_boundary/massabesic_contour_2.5m.gpkg.
MEASUREMENTS (Massabesic, 06-22 water level, native tiles, blunders masked):
 - shallowest surveyed depth p1 = 2.06 m; cells < 1 m: 0.01 % → a 1 m contour is NOT resolvable from our bathy; the boat
   never went shallower than ~2 m.
 - distance from the NHD shore to the nearest surveyed cell: p10 36 m, p50 116 m, p90 561 m; only 3 % of the shoreline
   has soundings within 20 m, 18 % within 50 m; 56 % has none within 100 m.
 - lake area within 20 m of shore: 111 ha, 98.6 % unsurveyed; within 50 m: 273 ha, 93.8 %; within 100 m: 509 ha,
   82.8 %; whole tiled lake ~1,394 ha (NHD polygon, approx.), 45 % unsurveyed.
 - the 2.5 m "contour" came out as 2,373 fragments: it mostly traces the COVERAGE EDGE, not an isobath — wherever the
   survey stopped at 2–3 m the contour follows the survey line ends.
DESIGN CONSEQUENCES: (1) a navigable boundary must carry a per-segment KIND — bathymetric contour | coverage limit |
coastline — because they mean different things to a costmap (a coverage limit is "unknown beyond", a contour is "shallow
beyond", a coastline is "land"); (2) "unsurveyed is lethal" currently makes ~99 % of the 20 m shore band and 45 % of the
lake lethal — the coastline fallback with a buffer is what makes the lake navigable near shore, and the number says the
proxy is costing most of the near-shore water; (3) the store's shoreline product = coastline (imported) + navigable
boundary (derived, kinds) + water level (water/, from trajectories); the costmap's rule for combining them is the
consumer's (uma#296); (4) Shoals has no navigable boundary yet (no shallow coverage); (5) next slice = segmentation-derived
shore (ORIGIN_AUTO) for a three-way comparison shore/coverage/segmentation.
CORRECTION (Roland 2026-09-21): Massabesic's near-shore gap is a SURVEY CHOICE (the submerged target was presumed offshore),
not a method limit; and Shoals DOES have shallow coverage around Appledore. Measured (20 Shoals tiles, ENC US4NH1BD
coastline of all islands in the bbox, 1,702 samples): shore → nearest surveyed cell p10 0 m, p50 369 m, p90 1,241 m;
27 % of the sampled coastline has soundings within 20 m (Appledore's shallow work), 63 % has none within 100 m (the other
islands); water within 20 m of the coastline 25 ha, 81 % unsurveyed. Bed heights up to −25.6 m ellipsoidal vs sea surface
≈ −28 m ± tide → soundings within ~0–2 m of the surface exist. Shoals navigable boundary needs a TIDAL water-level series
(water/, from the Aug trajectories) — buildable, not yet built.

## SPINE DECISION 5 — features — DECIDED 2026-09-21 (Roland: "Looks good")
5.1 Category: features are a quantity store with a VECTOR container, same axes (state × origin), same catalog; the
    container is process-derived/revisable (GeoPackage now; CDR ContactArray for contacts, per the interim framing).
5.2 Rasterization ON DEMAND only; the vector is the record (reverses ADR-0010 D2's rasterised chart layer as record).
5.3 Shoreline = THREE distinct records, never fused by the store:
    (1) COASTLINE — imported feature, published, a mapped snapshot at a nominal level with its own datum/uncertainty; the
        consumer's FALLBACK; meaning "land beyond here".
    (2) NAVIGABLE-WATER BOUNDARY — derived feature (draft/reviewed, surveyed) = depth surface at the CURRENT water level
        crossing a consumer-relevant depth; fingerprinted over tiles + level; with a per-segment KIND:
        bathymetric contour ("shallower than d beyond") | coverage limit ("unknown beyond") | coastline ("land beyond").
    (3) WATER LEVEL — a water/ series derived from trajectories (RTK height at base_link − height above waterline,
        ellipsoidal); lake level at Massabesic, tide (time series) at Shoals; an input to (2).
    Combination = the consumer's rule (nav-surface view for the costmap, uma#296: e.g. coverage limit → coastline buffer,
    not lethal). A segmentation-derived shore = a fourth record of the coastline kind (surveyed, draft, ORIGIN_AUTO).
ALL SIX SPINE DECISIONS NOW DECIDED (0 frame, 1 identity, 2 overviews, 3 axes, 4 bake, 5 features). Next = rev 3.
