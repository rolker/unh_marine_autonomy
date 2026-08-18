# Agent Guide: unh_marine_autonomy

> Core autonomy framework for UNH marine robotics — mission management, control
> arbitration, and reliable command transport for uncrewed surface vehicles.

## Package Inventory

| Package | Language | Description |
|---------|----------|-------------|
| `command_bridge` | Python | Reliable command delivery over lossy radio/satellite links (send-until-ack with timestamp deduplication) |
| `helm_manager` | C++ | Control arbitrator ensuring single-active-mode command authority (standby/manual/autonomous) |
| `joy_to_helm` | Python | Converts joystick input to helm commands for manual piloting |
| `marine_autonomy` | C++/Python | Meta-package with launch files, geodesic utilities, and system configuration |
| `marine_autonomy_integration_tests` | Python (CMake) | Cross-package integration tests for mission and navigation flows |
| `marine_bathymetry_store` | C++ | Persistent multi-source bathymetric data store: GGGS-tiled, priority source layers, best-source / shallowest-reliable queries, per-tile GeoTIFF (ADR-0002 / #86); `s102_import` CLI fetches + converts NOAA S-102 tiles (#278) |
| `marine_interfaces` | C++ (IDL) | ROS 2 message definitions for helm commands, heartbeats, navigation, perception contacts, and sensor data (46 msg types) |
| `marine_sidescan_mosaic` | C++ | Georeferenced sidescan backscatter mosaicking, live **and** offline: the live node projects GCV port/stbd RawSonarImage samples to GGGS-tiled uint16 GeoTIFF tiles for CAMP / web display (#173 / #171 / #166); the offline chain archives Tier-1 `.sst1` (#208), builds the durable Tier-2 `flat` / `processed` stores with best-source compositing + registry (#184 / #253), DEM-orthorectifies the `processed` build against a bathy store (#297, `--bathy-store`), and folds overview pyramids (#188 / ADR-0011) |
| `marine_survey_index` | C++ | Offline survey indexer + query CLI: bags → per-GGGS-tile pass intervals in a regenerable SQLite sidecar; answers "which bags/time-ranges saw this location" (#258 stage 1 / #259; schema contract in `docs/survey_index_schema.md`) |
| `marine_tiled_raster_store` | C++ | Generic GGGS-tiled raster store core: band/dtype-parametrized `TiledRasterTile<T>` + per-tile GeoTIFF persistence, shared by bathymetry and sidescan (#172) |
| `mission_manager` | Python | Converts mission plans from CAMP GCS into navigation tasks and manages task execution |
| `mission_manager_interfaces` | C++ (IDL) | Service definitions for task manipulation (3 srv types) |

## Repository Layout

```
unh_marine_autonomy/
├── command_bridge/
│   └── command_bridge/         # Python: sender + receiver nodes
├── config/
│   ├── bootstrap.yaml          # Workspace bootstrap config
│   ├── layers.txt              # Layer order (underlay → core → ... → ui)
│   └── repos/                  # .repos files for all 6 workspace layers
├── docs/
│   ├── autonomy_modes.md       # Piloting mode state machine
│   ├── data_flows.md           # System data flow diagrams
│   └── interfaces.md           # ROS topic/service/action reference
├── helm_manager/
│   ├── src/                    # C++: lifecycle node, piloting mode logic
│   ├── launch/                 # helm_manager_launch.py
│   └── test/                   # GTest: arbitration, mode, conversion tests
├── joy_to_helm/
│   ├── joy_to_helm/            # Python: joystick-to-helm converter
│   └── launch/                 # joy_to_helm_launch.py
├── marine_autonomy/
│   ├── config/                 # operator.yaml, robot.yaml
│   ├── launch/                 # robot_core_launch.py, operator_core_launch.py
│   ├── marine_autonomy/        # Python: geodesic, nav, wgs84 utilities
│   ├── src/                    # C++: utils
│   └── docs/                   # system_architecture.md
├── marine_autonomy_integration_tests/
│   └── test/                   # Launch-based integration tests
├── marine_interfaces/
│   ├── msg/                    # 46 message definitions
│   └── bmr/                    # Bag migration rules
├── mission_manager/
│   ├── mission_manager/
│   │   ├── mission_manager/    # Python: core node, CAMP interface, coverage adapter
│   │   └── test/               # Unit tests
│   └── mission_manager_interfaces/
│       └── srv/                # 3 service definitions
├── PRINCIPLES.md               # Project principles
├── VISION.md                   # Strategic vision and objectives
└── README.md
```

## Architecture Overview

This repository provides the **robot-side autonomy stack** — everything needed to
receive missions, arbitrate control, and drive the vehicle. It is self-contained
and can be deployed to a robot without operator-side dependencies.

The internal command flow: the **mission manager** decomposes mission plans into
navigation tasks, which are executed by the navigation stack. The **helm manager**
arbitrates between manual (joystick) and autonomous control sources, enforcing
single-active-mode safety. The **command bridge** provides reliable command delivery
over lossy radio/satellite links.

The full system includes external components in other repositories:
- **CAMP** (ground control station, in `camp` repo) — operator UI for mission planning
- **unh_marine_navigation** — path planning and task execution
- **Platform drivers** — vehicle-specific hardware interfaces

Key architectural patterns:
- **Send-until-ack**: Command bridge uses timestamp-based deduplication for reliable
  delivery over unreliable radio/satellite links
- **Single-active-mode arbitration**: Helm manager gates commands — only the active
  piloting mode (standby/manual/autonomous) can reach the vehicle
- **Lifecycle nodes**: Helm manager uses ROS 2 lifecycle for controlled state transitions
- **Heartbeat status**: Nodes publish periodic heartbeat messages with key-value status

See [`docs/data_flows.md`](../docs/data_flows.md) for detailed data flow diagrams,
[`docs/interfaces.md`](../docs/interfaces.md) for the complete topic/service reference,
and [`docs/autonomy_modes.md`](../docs/autonomy_modes.md) for the piloting mode state machine.

## Key Files to Read First

1. `marine_autonomy/launch/robot_core_launch.py` — Robot-side launch: brings up command
   bridge, helm manager, mission manager, and platform sender
2. `marine_autonomy/launch/operator_core_launch.py` — Operator-side launch: command
   bridge sender, joystick, and joy-to-helm converter
3. `helm_manager/src/helm_manager.h` — Core arbitration logic (lifecycle node)
4. `marine_autonomy/config/robot.yaml` — Default robot parameters
5. `marine_interfaces/msg/Helm.msg` — Primary control message (throttle + rudder)
6. `docs/data_flows.md` — System-wide data flow documentation

## Build & Test

```bash
# From the layer workspace directory (e.g., layers/main/core_ws/)
colcon build --symlink-install
colcon test && colcon test-result --verbose

# Single package
colcon build --packages-select helm_manager
colcon test --packages-select helm_manager && colcon test-result --verbose
```

Known build requirements:
- `marine_interfaces` must build before `helm_manager`, `mission_manager`, and
  `marine_autonomy` (provides shared message types)
- `marine_tiled_raster_store` must build before `marine_bathymetry_store` and
  `marine_sidescan_mosaic` (both wrap its `TiledRasterTile` + GeoTIFF I/O)
  (provides the generic `TiledRasterTile<T>` + GeoTIFF persistence it wraps, #172)
- `mission_manager_interfaces` must build before `mission_manager`
- `marine_vertical_datum` must build before `marine_bathymetry_store` (#278:
  the S-102 importer's per-cell MLLW→ellipsoid shift, #274)
- Integration tests depend on `command_bridge`, `mission_manager`, `marine_interfaces`,
  and `marine_nav_interfaces` (from `unh_marine_navigation`)

## Cross-Layer Dependencies

| Package | Depends On | Layer | What It Imports |
|---------|-----------|-------|-----------------|
| `marine_interfaces` | `geographic_msgs` | underlay (`geographic_info`) | Geographic coordinate message types |
| `marine_interfaces` | `marine_acoustic_msgs` | system (apt) | Acoustic sensor message types |
| `mission_manager` | `marine_nav_interfaces` | core (`unh_marine_navigation`) | `TaskInformation`, navigation action types |
| `mission_manager` | `marine_nav_tasks` | core (`unh_marine_navigation`) | Task type definitions |
| `mission_manager_interfaces` | `marine_nav_interfaces` | core (`unh_marine_navigation`) | `TaskInformation` message for service definitions |
| `marine_autonomy_integration_tests` | `marine_nav_interfaces` | core (`unh_marine_navigation`) | Test fixtures for navigation flow |

### Manifest Repo Role

This repository also serves as the workspace **manifest repo** — the `config/` directory
defines the `.repos` files that pull in all project repos across 6 layers:

| Layer | Config File | Repos |
|-------|-------------|-------|
| underlay | `config/repos/underlay.repos` | geographic_info, vrx, norbit, ros2sonic, + 5 more |
| core | `config/repos/core.repos` | unh_marine_navigation, udp_bridge, marine_ais, + 3 more |
| platforms | `config/repos/platforms.repos` | mru_transform, ben_project11, + 5 more |
| sensors | `config/repos/sensors.repos` | unh_marine_radar, cube_bathymetry, + 4 more |
| simulation | `config/repos/simulation.repos` | unh_marine_simulation |
| ui | `config/repos/ui.repos` | camp, rqt_marine_radar, + 2 more |

## Common Pitfalls

- **Build order matters**: `marine_interfaces` and `mission_manager_interfaces` are IDL
  packages that generate code at build time. If you modify a `.msg` or `.srv` file,
  dependent packages need a rebuild.
- **Nested package structure**: `mission_manager` is a directory containing two separate
  packages (`mission_manager/mission_manager/` and `mission_manager/mission_manager_interfaces/`).
  The `--packages-select` flag takes the package name, not the directory path.
- **Config files are workspace-level**: Files in `config/` (layers.txt, bootstrap.yaml,
  .repos files) are consumed by the workspace infrastructure, not by ROS 2 nodes.
  Changes here affect all workspace users.
- **Legacy code**: Some launch files (`.launch` extension) use ROS 1 XML format and
  are no longer active. The current launch files use Python (`.py` extension).
- **Cross-store file-format coupling with no package dependency**:
  `marine_sidescan_mosaic`'s `BathyDem` (`sidescan_tier2_processed --bathy-store`,
  #297) reads `marine_bathymetry_store`'s value tiles **directly off disk** —
  ADR-0006 D9 requires the decoupling, so there is deliberately no
  `marine_bathymetry_store` entry in `package.xml` or `CMakeLists.txt` and no
  compiler check on the format. The contract it assumes:
  `<store_root>/<layer>/<level>_<row>_<col>.tif`, 2-band `Float64`, band 0 =
  **WGS84 ellipsoidal height, up-positive** with `NaN` for no data, band 1 =
  1-sigma vertical uncertainty. Changing any of those in the bathy store silently
  breaks this reader (wrong placements, not a build failure), so change them
  together and re-run `test_bathy_dem` / `test_tier2_processed_dem`, which author
  their own value tiles against the same contract. The layer directory names are
  configuration (`--bathy-layers`), not code — ADR-0010 D3's `survey/`→`processed/`
  re-classification is a flag change here.
