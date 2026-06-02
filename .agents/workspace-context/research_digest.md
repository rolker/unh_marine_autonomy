# Research Digest: Marine Robotics

<!-- Last updated: 2026-06-02 -->
<!-- If older than 30 days, consider running /research --refresh; entries older than 90 days should be flagged for review -->

## ROS 2 Autonomous Surface Vehicles (ASVs)

**Added**: 2026-02-27 | **Sources**: [VRX GitHub](https://github.com/osrf/vrx), [RoboBoat 2026](https://roboboat.org/programs/2026/), [ROS Maritime Working Group](https://github.com/ros-maritime/community)

Key takeaways:
- ROS 2 is the dominant framework for ASV software stacks, with competition teams (RoboBoat, RoboSub) building full autonomy pipelines on ROS 2 Jazzy
- The ROS Maritime Working Group maintains shared packages for USVs and UUVs covering controls, simulation, and drivers
- Reinforcement learning approaches for ASV decision-making and control are an active research area, with recent work on distributional RL for integrated planning and control

**Relevance**: Direct alignment with this project's ASV platforms (EchoBoats, BEN); ROS Maritime packages may offer reusable components for navigation and control

---

## VRX Simulation (Gazebo Harmonic + ROS 2 Jazzy)

**Added**: 2026-02-27 | **Sources**: [VRX 3.0 Release](https://github.com/osrf/vrx/releases), [VRX Wiki](https://github.com/osrf/vrx/wiki/)

Key takeaways:
- VRX 3.0 runs on Gazebo Harmonic and ROS 2 Jazzy by default — matches this workspace's target platform
- Provides realistic ocean surface dynamics, wind, and wave effects for USV testing
- Older Gazebo-based maritime simulators (UUVSim, DAVE, WAVE) are unmaintained and lack ROS 2 support
- SMaRCSim is a newer alternative providing maritime simulation modules, but VRX remains the most mature USV option

**Relevance**: VRX 3.0's Jazzy/Harmonic alignment makes it a natural simulation companion for this workspace's `ben_gazebo` and `unh_marine_simulation` packages

---

## Hydrographic Survey Automation

**Added**: 2026-02-27 | **Sources**: [Hydrographic Survey Equipment Market Report 2026](https://www.globenewswire.com/news-release/2026/01/27/3226189/28124/en/Hydrographic-Survey-Equipment-Market-Report-2026-4-47-Bn-Opportunities-Trends-Competitive-Landscape-Strategies-and-Forecasts-2020-2025-2025-2030F-2035F.html), [Next-gen AUVs (Hydro International)](https://www.hydro-international.com/content/article/the-revolutionary-capabilities-of-next-generation-autonomous-underwater-vehicles)

Key takeaways:
- Market growing from $3.22B (2025) to $3.44B (2026) at 6.8% CAGR, driven by autonomous survey vessels and AI-based mapping
- AUV swarm intelligence is being operationalized — coordinated fleets increase data collection efficiency by >50% vs single-vehicle missions
- Autonomous launch and recovery can reduce vessel-based operational costs by up to 30%
- AI-driven adaptive mission planning allows real-time survey adjustments based on sensor feedback

**Relevance**: Validates the trajectory of UNH CCOM's hydrographic survey automation work; swarm coordination and adaptive planning are potential research directions for `manda_coverage`

---

## Marine Perception: Sonar and Deep Learning

**Added**: 2026-02-27 | **Sources**: [Sonar-based Deep Learning in Underwater Robotics](https://arxiv.org/html/2412.11840v1), [AI-Driven Marine Robotics (arXiv)](https://arxiv.org/html/2509.01878v1), [Sonar AI Review (J. Field Robotics)](https://onlinelibrary.wiley.com/doi/10.1002/rob.70077)

Key takeaways:
- Sonar-based DL perception covers classification, object detection, segmentation, and SLAM — but limited training data and inherent noise challenge model robustness
- Multi-modal fusion (sonar + lidar + cameras) is the emerging standard for comprehensive marine perception
- Gazebo-based simulators (DAVE, Stonefish) support virtual sonar sensors including forward-looking sonar (FLS) and side-scan sonar (SSS) for synthetic training data generation
- Seabed-to-sky mapping systems combining sonar and lidar provide unified above/below waterline spatial awareness

**Relevance**: Directly relevant to `unh_marine_perception` and `unh_marine_radar` packages; synthetic sonar data from simulation could augment training pipelines

---

## MOOS-IvP and ROS 2 Integration

**Added**: 2026-02-27 | **Sources**: [ROS-IvP Paper](http://www.mjbays.com/OCEANS%20Paper%20-%20ROS-IvP.pdf), [CoUGARs AUV Platform](https://arxiv.org/html/2511.08822v1), [MOOS-IvP Project](https://oceanai.mit.edu/moos-ivp/)

Key takeaways:
- Custom software bridges between ROS 2 and MOOS middlewares enable using MOOS-IvP's proven marine autonomy behaviors with ROS 2's ecosystem
- MOOS-IvP's IvP Helm provides a large library of maritime-specific autonomy behaviors and behavior arbitration that ROS 2 alone doesn't match
- Recent low-cost AUV platforms (CoUGARs, <$3K/unit) demonstrate hybrid MOOS-IvP + ROS 2 architectures in production
- The combination leverages MOOS-IvP for mission planning/behavior arbitration and ROS 2 for communications, perception, and low-level control

**Relevance**: Relevant to `unh_marine_autonomy`'s mission management architecture; MOOS-IvP integration patterns could inform behavior planning approaches

---

## NOAA Coast Pilot — Structured Marine Data for World Generation

**Added**: 2026-03-12 | **Sources**: [Coast Pilot portal](https://nauticalcharts.noaa.gov/publications/coast-pilot/index.html), [Coast Pilot XML viewer](https://nauticalcharts.noaa.gov/publications/coast-pilot/xml2html.html?book=2), [Coast Pilot e-Publishing (PDF)](https://ocsdata.ncd.noaa.gov/media/noaa-industry-day/2017/01_03_Coast_Pilot_Software_Integration.pdf), [InPort metadata](https://www.fisheries.noaa.gov/inport/item/39970), [XML example (CP5 Ch6)](https://nauticalcharts.noaa.gov/publications/coast-pilot/files/cp5/CPB5_C06_WEB.xml)

Key takeaways:
- The U.S. Coast Pilot is a 10-volume NOAA publication supplementing nautical charts with data that is difficult to portray on a chart: bridge clearances, wharf dimensions, anchorage depths, prominent features, pilotage, weather, ice conditions, and facility descriptions
- **XML format available**: chapters are published as XML alongside PDF/HTML; URL pattern `files/cp{N}/CPB{N}_C{NN}_WEB.xml`; weekly updates
- **Geotagged features**: `<CP_GEO_LOC>` elements carry `lat_dec`/`long_dec` decimal coordinates, GNIS source IDs, feature class (Bay, Building, Tower, etc.), and state/county
- **Bridge data**: `<bridge bridgeid="...">` elements link to clearance values in surrounding text; vertical and horizontal clearances are described in prose, not structured attributes — NLP extraction needed
- **Wharf/facility dimensions**: lengths, depths alongside, and construction materials are embedded in `<paraText>` prose, not machine-structured fields
- **Volume 1** covers Maine to Cape Cod (including NH/Portsmouth); **Volume 2** covers Cape Cod to Sandy Hook — these are the primary volumes for UNH operations
- The main extraction challenge is that numeric data (heights, clearances, depths) is in natural-language paragraphs, not dedicated XML attributes; the `<CP_GEO_LOC>` tags provide spatial anchoring but the measurements require text parsing

**Relevance to `marine_charts_to_gazebo_world`** (rolker/unh_marine_simulation#40):
- **Bridge enrichment**: S57 BRIDGE features often lack vertical clearance (VERCLR); Coast Pilot provides specific clearance values that could fill these gaps
- **Building/wharf heights**: the package currently uses hardcoded heights (12m buildings, 2.5m wharves); Coast Pilot describes actual wharf lengths, depths, and sometimes heights
- **Anchorage areas**: could be rendered as marked zones in the simulation world
- **Prominent features**: Coast Pilot describes towers, tanks, stacks, and other landmarks with locations — could improve feature placement beyond what S57 provides
- **Implementation approach**: a Coast Pilot XML parser would need (1) XML download/cache by volume+chapter, (2) `<CP_GEO_LOC>` extraction for spatial indexing, (3) regex/NLP extraction of numeric values from `<paraText>` for bridge clearances, wharf dimensions, and facility heights, (4) spatial matching to existing S57 features by proximity

---

## Gazebo Fuel — Sharing Generated Worlds

**Added**: 2026-03-12 | **Sources**: [Fuel portal](https://app.gazebosim.org), [About Fuel](https://gazebosim.org/docs/latest/fuel/), [gz-fuel-tools](https://github.com/gazebosim/gz-fuel-tools), [Contributing a world](https://gazebosim.org/docs/latest/fuel_contributing_world/), [Importing a mesh to Fuel](https://gazebosim.org/api/sim/7/meshtofuel.html), [fuel_tools library](https://gazebosim.org/libs/fuel_tools/)

Key takeaways:
- Gazebo Fuel (app.gazebosim.org) is a community repository for sharing simulation **models** and **worlds**; both asset types are supported
- **Model upload**: via CLI (`gz fuel upload -m ~/path --header 'Private-token: <TOKEN>'`) or web UI drag-and-drop; requires `model.config` + `model.sdf` + meshes/textures in a standard directory layout; thumbnails (5 PNGs) recommended
- **World upload**: via web form only (`app.gazebosim.org/fuel/worlds/upload`); accepts name, description, tags, and world SDF file; no CLI world upload documented as of gz-fuel-tools 10
- **Authentication**: access tokens generated at `app.gazebosim.org/settings#access_tokens`; stored in `~/.gz/fuel/config.yaml` for automatic use
- **World referencing models**: worlds on Fuel typically reference models by Fuel URI (`https://fuel.gazebosim.org/1.0/<owner>/models/<name>`); Gazebo auto-downloads referenced models at runtime
- **Self-contained worlds**: worlds with inline `<model>` elements and local heightmaps work but require `GZ_SIM_RESOURCE_PATH` to be set for resource discovery — not ideal for Fuel distribution

Challenges for `marine_charts_to_gazebo_world` (rolker/unh_marine_simulation#42):
- **Terrain heightmaps are separate model directories**: the generator outputs terrain as `terrain_{name}/` with `model.config`, `model.sdf`, and `heightmap.png`; the world SDF references these via `<include><uri>model://terrain_{name}</uri></include>` — these would need to be uploaded as separate Fuel models first
- **Feature models are inline**: buildings, buoys, bridges, and shore constructions are generated as inline `<model>` elements in the world SDF (not separate model directories) — this is compatible with Fuel world upload but means thousands of inline models in a single SDF file
- **No mesh files**: all geometry is parametric (cylinders, cones, boxes, polylines) — no COLLADA/OBJ meshes, so the standard Fuel model structure (meshes/ directory) doesn't apply
- **Generated textures**: terrain textures are simple 16x16 PNGs embedded in the terrain model directory; these would transfer to Fuel with the terrain model upload
- **Reproducibility vs. sharing**: worlds are deterministically generated from S57 charts + parameters, so sharing the generation recipe (region bounds, flags) may be more useful than sharing the output — but pre-built worlds lower the barrier for users without ENC data access

Possible implementation approach:
1. Upload terrain heightmap models to Fuel as individual models (each with `model.config` + `model.sdf` + `heightmap.png` + textures)
2. Rewrite terrain `<include>` URIs in the world SDF to point to Fuel model URIs instead of local `model://` paths
3. Upload the world SDF via the web form or automate via Fuel REST API
4. Optionally factor out common feature types (buoy models, beacon models) into reusable Fuel models referenced by `<include>` instead of inline — this would reduce world SDF size and enable model reuse across worlds
5. Add a `--fuel-upload` flag or post-generation script to `marine_charts_to_gazebo_world`

Existing Fuel models relevant to `marine_charts_to_gazebo_world`:
- **Buoys** (13 models): VRX/RobotX marker buoys (`mb_marker_buoy_{red,black,green,white}`, `mb_round_buoy_{orange,black}` by OpenRobotics; `surmark950410`, `surmark46104{white,black}` by ngxingyu) — mesh-based, colored; could replace parametric cylinder+cone buoys
- **Coastal environment**: `Coast Waves`/`Coast Water` (6km wave visuals), `Dock` (MBZIRC), `Pier` (wooden pier), `Beach` (5.9km environment) — all by OpenRobotics
- **Structures**: `Water tower`, `Radio tower`, `Tower crane`, `Truss bridge`, `Office Building`, `Apartment`, `Depot` — generic but usable for landmark features
- **Bathymetric heightmaps**: `Monterey Bay` (GEBCO bathymetry, by jennuine), `Portuguese Ledge` (MBARI seafloor data, by OpenRobotics) — validates that sharing bathymetric terrain tiles on Fuel is an established pattern
- **Gaps — no Fuel models exist for**: navigation beacons, navigation lights/lighthouses, shore constructions (breakwaters, seawalls, groynes, rip-rap), pontoons/floating docks, mooring facilities (bollards, dolphins), piles/posts, or realistic S57 BOYSHP-accurate buoy shapes (conical, can, pillar, spar)

---

## ArduRover Failsafe Configuration for Over-the-Horizon ASV Operations

**Added**: 2026-03-31 | **Sources**: [Rover Failsafes](https://ardupilot.org/rover/docs/rover-failsafes.html), [Boat Configuration](https://ardupilot.org/rover/docs/boat-configuration.html), [Cylindrical Fence](https://ardupilot.org/rover/docs/common-ac2_simple_geofence.html), [QGC Safety Setup](https://docs.qgroundcontrol.com/Stable_V4.3/en/qgc-user-guide/setup_view/safety_ardupilot.html)

Key takeaways:
- **FS_ACTION** applies to radio, battery, and GCS failsafes: 1=RTL, 2=Hold, 3=SmartRTL→RTL, 4=SmartRTL→Hold, 5=Disarm, 6=Loiter→Hold. For over-the-horizon ASV with backseat driver, SmartRTL→Hold (4) is safest — tries SmartRTL path back, falls back to hold-position if path is too long
- **GCS failsafe** (`FS_GCS_ENABLE=1`, `FS_GCS_TIMEOUT=5s`): triggers when no MAVLink heartbeat received. Critical for backseat-driver architecture — if companion computer (running mavros/ROS) stops sending heartbeats, FCU acts autonomously
- **Battery failsafe** is two-stage: `BATT_FS_LOW_ACT` (warning, e.g., RTL) and `BATT_FS_CRT_ACT` (critical, e.g., Hold). Set voltage and/or mAh thresholds with `BATT_LOW_TIMER=10s` debounce
- **EKF failsafe** (`FS_EKF_ACTION`): when GPS degrades and EKF variance exceeds `FS_EKF_THRESH`, vehicle switches to Hold. With geofence enabled, this is critical — EKF failure means position is unknown
- **Geofence**: cylindrical fence (`FENCE_ENABLE=1`, `FENCE_RADIUS`). Breach action is RTL or report-only. Minimum radius 30m. Requires good GPS — do not disable GPS arming check with fence enabled
- **FS_OPTIONS bitmask**: bit 0 enables failsafe recognition in Hold mode — without this, a vehicle in Hold ignores further failsafes
- **Crash detection** (`FS_CRASH_CHECK`): switches to Hold or Hold+Disarm if roll/pitch exceeds `CRASH_ANGLE`. Useful for capsize detection on surface boats

**Relevance**: BizzyBoat currently has ALL failsafes disabled (`ARMING_CHECK=0, FS_ACTION=0`). For over-the-horizon ops, minimum recommended: GCS failsafe (SmartRTL→Hold), battery failsafe (two-stage), EKF failsafe, and a geofence. The GCS failsafe is the primary safety net when the ROS 2 companion computer fails.

---

## ArduPilot Lua Scripting for Onboard Failsafes

**Added**: 2026-03-31 | **Sources**: [Lua Scripts (Rover)](https://ardupilot.org/rover/docs/common-lua-scripts.html), [Lua Failsafe APIs discussion](https://discuss.ardupilot.org/t/lua-failsafe-apis/127439), [Lua API docs](https://github.com/ArduPilot/ardupilot/blob/master/libraries/AP_Scripting/docs/docs.lua), [GSoC 2026 Companion Health Monitoring](https://discuss.ardupilot.org/t/gsoc-2026-real-time-companion-computer-health-monitoring-failsafe-ram-yatishwar/143158)

Key takeaways:
- **Lua runs on the FCU** (Cube Orange), sandboxed, in parallel with flight code. Scripts on SD card under `/APM/scripts/`. Ideal for safety logic that must work when companion computer is dead
- **Key APIs**: `gcs:last_seen()` returns ms since last GCS heartbeat; `rc:has_valid_input()` checks RC signal; `battery:has_failsafed()` detects battery failsafe; `vehicle:set_mode(N)` changes flight mode; `arming:disarm()` disarms
- **No direct failsafe state bindings** — cannot query "is GCS failsafe active?" from Lua. Workaround: monitor `gcs:last_seen()` with custom timeout logic
- **Companion computer watchdog pattern**: poll `gcs:last_seen()` every 500ms; if exceeds threshold (e.g., 10s), call `vehicle:set_mode(HOLD_MODE)`. Provides defense-in-depth independent of built-in GCS failsafe
- **Limitations**: scripts run at low priority (not guaranteed schedule); memory constrained (43–204KB heap); cannot override core failsafes
- **GSoC 2026 project** (in progress): `AP_CompanionMonitor` C++ library monitoring CPU/RAM/disk via MAVLink `NAMED_VALUE_FLOAT` at 1Hz, with 3-tier state machine (HEALTHY→WARN→CRIT→LOST) and configurable heartbeat timeout. Not yet merged but indicates ArduPilot's direction

**Relevance**: Lua watchdog scripts provide a second safety layer for BizzyBoat. The companion computer (ROS 2/mavros) is a single point of failure — FCU's built-in GCS failsafe is first defense, Lua watchdog is second. A simple watchdog script monitoring `gcs:last_seen()` should be deployed before over-the-horizon ops.

---

## MAVROS 2 / ROS 2 Jazzy Integration Patterns and Pitfalls

**Added**: 2026-03-31 | **Sources**: [mavros Jazzy docs](https://docs.ros.org/en/jazzy/p/mavros/), [mavros README](https://github.com/mavlink/mavros/blob/ros2/mavros/README.md), [ArduPilot ROS 2 guide](https://ardupilot.org/dev/docs/ros.html), [BlueROV2 timesync discussion](https://discuss.bluerobotics.com/t/bluerov2-mavros-rtt-too-high-for-timesync-warning/21021), [ROS 2 Jazzy QoS docs](https://docs.ros.org/en/jazzy/Concepts/Intermediate/About-Quality-of-Service-Settings.html)

Key takeaways:
- **MAVROS vs DDS**: ArduPilot 4.5+ supports direct DDS interface (no mavros needed). Mavros is more mature for ArduRover with richer topic coverage. DDS is better for low-latency. Current BizzyBoat mavros setup is appropriate
- **Timesync**: `timesync_rate: 0.0` (disabled) is correct for ArduPilot — causes "RTT too high" warnings and clock drift. ArduPilot uses GPS-disciplined clock. BizzyBoat config already correct
- **GeographicLib datasets**: mavros_node **will not start** without geoid datasets. Run `install_geographiclib_datasets.sh` on every new machine. Document in deployment checklist
- **TF frames**: mavros translates NED↔ENU. Common error: disconnected TF trees. BizzyBoat disables mavros TF publishing (`global_position.tf.send: false`) to avoid conflicts with URDF — correct approach
- **QoS**: publisher/subscriber QoS must match or connection silently fails. Mavros defaults to RELIABLE/VOLATILE. Silent QoS mismatches are a common debugging headache
- **Stream rate**: call `mavros/set_stream_rate` at startup for desired telemetry rate. Without this, defaults may be too low for real-time control
- **setpoint_velocity**: `mav_frame: "BODY_NED"` for skid-steer boats commanding forward speed + yaw rate — correct for BizzyBoat

**Relevance**: BizzyBoat's mavros configuration is mostly correct. Key deployment checklist items: GeographicLib datasets installed, timesync disabled, TF publishing disabled, stream rate requested at startup. Main pitfall is silent QoS mismatches.

---

## CUAV C-RTK 2HP Dual-Antenna RTK GPS Configuration

**Added**: 2026-03-31 | **Sources**: [C-RTK2 HP guide (Rover)](https://ardupilot.org/rover/docs/common-cuav-c-rtk2-hp.html), [GPS for Yaw (Rover)](https://ardupilot.org/rover/docs/common-gps-for-yaw.html), [CUAV users manual](https://doc.cuav.net/gps/c-rtk-series/en/c-rtk2-hp/users-manual.html)

Key takeaways:
- **Single-unit dual-antenna**: C-RTK 2HP handles moving baseline internally (one module, two antennas). Simpler than dual-unit F9P setups
- **DroneCAN config** (recommended): `GPS1_TYPE=9`, `CAN_P1_DRIVER=1`, `GPS_AUTO_CONFIG=2`. Both CAN ports should connect via splitter
- **EKF3 for GPS heading**: `AHRS_EKF_TYPE=3`, `EK3_ENABLE=1`, `EK2_ENABLE=0`, `EK3_SRC1_YAW=2` (GPS only) or `3` (GPS with compass fallback)
- **Antenna offsets**: `GPS1_MB_TYPE=1` enables master-slave offsets. `GPS1_MB_OFS_X/Y/Z` define master position relative to slave. **Critical**: positive X = master forward of slave. BizzyBoat has fore/aft antennas — measure carefully
- **GPS position offsets** (`GPS1_POS_X/Y/Z`): antenna position relative to vehicle CG — different from MB offsets. Used for attitude-correction compensation
- **Validation**: GPS yaw only activates with RTK fixed (type 6), inter-antenna distance within 20% of configured, height differential within 20%. Failure = no heading
- **Minimum antenna separation**: 30cm (BizzyBoat baseline ~1.67m — well above)
- **Do NOT use `GPS_AUTO_SWITCH=2`** (Blend) with moving baseline
- **Heading accuracy**: 0.1° RMS at 1m baseline; BizzyBoat's 1.67m should be better
- **C-RTK 2HP includes RM3100 magnetometer** — could be enabled as compass fallback if calibrated
- **When GPS yaw lost** with `EK3_SRC1_YAW=2` (no fallback): **no heading source** — vehicle must hold position. This makes EKF failsafe critical

**Relevance**: BizzyBoat uses C-RTK 2HP via DroneCAN with compass disabled and GPS heading as sole yaw source. For over-the-horizon ops, lack of heading fallback is a risk — if RTK degrades (poor sky view, NTRIP dropout), vehicle loses heading. Consider enabling the built-in RM3100 as compass fallback (`EK3_SRC1_YAW=3`) for safety margin. Antenna offsets need careful measurement.

---

## Camera–IMU Time Synchronization & Temporal Calibration

**Added**: 2026-06-02 | **Sources**: [message_filters ApproximateTime (Jazzy)](https://docs.ros.org/en/jazzy/p/message_filters/doc/Tutorials/Approximate-Synchronizer-Cpp.html), [Kalibr cam-IMU wiki](https://github.com/ethz-asl/kalibr/wiki/camera-imu-calibration), [Online Temporal Calibration for VIO (arXiv:1808.00692)](https://arxiv.org/pdf/1808.00692), [Universal Online Temporal Calibration (arXiv:2501.01788)](https://arxiv.org/pdf/2501.01788), [Timestamp Offset under Interval Uncertainty (Voges 2018)](https://raphael-voges.de/publication/voges-2018-b/voges-2018-b.pdf), [ROS cam-IMU hardware sync](https://grauonline.de/wordpress/?page_id=1951), [depthai-ros HW timestamp #56](https://github.com/luxonis/depthai-ros/issues/56)

Problem: an image header stamp is "slightly off" relative to the IMU because each
sensor has distinct triggering, exposure/readout, transport, and clock-offset
delays (images take tens of ms to move; IMU < 1 ms). Fix by *type of* offset:

Key takeaways:
- **Characterize first** — cross-correlate camera-derived angular rate vs IMU
  gyro (Kalibr's initial-guess method) to measure the lag and see if it's a
  **constant** or **jitter/drift**.
- **Constant offset → subtract a fixed delta** from the image stamp, ideally in
  the driver or a thin relay node. Simplest and robust.
- **Fuse by interpolating the high-rate IMU to the (corrected) image time** — not
  the image to IMU time (the latter discards information).
- **ROS-level pairing**: `message_filters` **ApproximateTime** associates
  nearest-in-time messages (tune `setMaxIntervalDuration()` / inter-message
  bounds). Associates, does **not** correct the physical offset.
- **Unknown / drifting offset → estimate it online** as a state variable:
  VINS-Mono `estimate_td:1` (feature image-plane velocity model; consistent
  within ≈±75–85 ms), OpenVINS/MSCKF online temporal calibration.
- **Offline ground truth**: **Kalibr** (continuous-time B-spline, max-likelihood)
  estimates the cam↔IMU time offset + extrinsics to ~ms — use it to *measure* the
  constant for the fixed-offset correction above.
- **Best: hardware-trigger + hardware timestamps** (e.g. IMU emits `TimeReference`,
  camera reconstructs capture time). A device HW stamp beats host `Time::now()`,
  which bakes in transport delay.

**Relevance**: BizzyBoat fuses OAK camera imagery with IMU (SBG/mavros) for the
segmentation→costmap/reflex pipeline; `depthai-ros` has a documented history of
not surfacing the OAK device hardware timestamp into the ROS header
(luxonis/depthai-ros#56), so first verify whether image stamps are device-time or
host-time. Marine dynamics are slow, so a fixed tens-of-ms offset is cm-level on
the perception projection — usually tolerable, and a *constant* offset is nearly
free to correct. Most relevant to the `unh_marine_perception` pipeline and any
future visual-inertial work on the EchoBoat platform.
