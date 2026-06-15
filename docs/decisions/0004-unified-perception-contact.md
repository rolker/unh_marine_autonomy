# ADR-0004: Unified Perception Contact Message (`marine_interfaces/Contact`)

## Status

Proposed (2026-06-14). Authored under
[rolker/unh_marine_autonomy#156](https://github.com/rolker/unh_marine_autonomy/issues/156)
/ PR [#161](https://github.com/rolker/unh_marine_autonomy/pull/161), which define
the message. This ADR records the load-bearing design decisions so they survive
the issue; the full field-by-field rationale is in the #156 body.

## Context

There is no ROS standard for a marine contact/target object. `vision_msgs`
covers per-image **detections** but is too thin to be a unified object (single
bbox, no graduated kinematics, no shape variety). The marine domain needs one
object that spans **all sensors** (sonar, radar, lidar, camera) and **all
kinematic states** (a static seabed object through a moving vessel), plus a
**human-curation** layer (the 2026-06-12 debrief's target-marking need; gates the
draw-a-box producer in rqt_operator_tools#59 and feeds an L3 contact/target
manager).

A pre-existing `marine_interfaces/Contact` (legacy AIS vessel message) was
**orphaned** — no publishers, and CAMP's live AIS path already ran on
`marine_ais_msgs/AISContact`. Its only references (camp `ais/*`) were dead code,
removed in [rolker/camp#93](https://github.com/rolker/camp/issues/93). That frees
the `Contact` name.

## Decision

### D1 — Model on the Autoware object pattern; kinematic state and shape are fields, not types

A single `Contact` whose modality and kinematic status are **fields**, so one
message spans static↔moving and every sensor. This is Autoware's
`DetectedObject`/`Kinematics`/`Shape` insight, which is the right pattern for a
unified object. We do **not** split into separate detect/contact message types.

### D2 — Model our own; do not adopt Autoware as a dependency

Autoware's model is automotive (closed classification enum, map-frame, no
geo/curation) and a heavy dependency. We reuse the *pattern*, not the package,
and add the marine/curation block.

### D3 — Package `marine_interfaces`, name `Contact`; reuse the freed name

The messages live in the existing `marine_interfaces` interfaces package (not a
new `_msgs` package). The new object takes the `Contact` name, reusing it after
the legacy cleanup (camp#93). Redefining `Contact` is a **breaking** change,
accepted because the legacy message had no remaining consumers.

### D4 — Reuse standard types; encode "unknown" by convention, not flags

Compose from `std_msgs`, `geometry_msgs/{PoseWithCovariance,TwistWithCovariance,
Polygon,Vector3}`, `geographic_msgs/GeoPose`, and `diagnostic_msgs/KeyValue`.
Nothing is re-invented. Specifics:

- **Covariance "unknown" = `covariance[0] == -1`** (`sensor_msgs/Imu`
  convention) — no `has_*` flags. Known-stationary (twist 0 + real cov),
  unknown-velocity, and moving all fall out of twist+covariance.
- **`orientation_availability` enum is kept** — not covariance-expressible;
  `ORIENTATION_SIGN_UNKNOWN` is the sidescan-shadow case.
- **`class_id` is an open-vocabulary `string`** (`vision_msgs` approach), not a
  closed enum; YOLO labels map straight in.
- **`attributes` uses `diagnostic_msgs/KeyValue`**, not the local
  `marine_interfaces/KeyValue` clone — the standard type is a free dependency
  (core `common_interfaces`, already in the `core_ws` build via `udp_bridge`) and
  interoperates, whereas the clone is a distinct, non-interoperable DDS type.
  Package-wide standardization on it is tracked in #158.
- **`geo_pose` unset convention**: it is a derived/resolved value, so it needs a
  way to say "not yet resolved" that an all-zero `GeoPose` cannot give ((0,0,0)
  is a valid location). Unresolved = `position.latitude` is NaN; orientation
  unknown = all-zero quaternion. Same unknown-by-convention spirit as the
  covariance sentinel, not a `has_*` flag.

### D5 — A curation / provenance block, distinct from the sensor observation layer

`Contact` carries `source`, `origin_kind` (AUTO/HUMAN), `status` (PROPOSED/
CONFIRMED/REJECTED — the MCM detect→confirm lifecycle), `note`,
`observation_ids`, and free `attributes`. Per-sensor observations
(`vision_msgs` detections, radar/sonar returns, AIS) and tracks **project up**
into a `Contact` and are referenced via `observation_ids`; their full-fidelity
domain messages (e.g. AIS voyage/MMSI in `marine_ais`) are **kept, not folded
in**. `Contact` sits above the observation layer; it does not replace it.

## Consequences

- Redefining `Contact` is breaking; the legacy vessel message is retired. No
  in-tree consumer remained (camp#93 merged); the operator station is camp,
  already cleaned. gitcloud mirrors pick this up via reconciliation.
- `marine_interfaces` gains a `diagnostic_msgs` dependency (no new install cost).
- The first real producer is the radar tracker
  ([rolker/unh_marine_radar#13](https://github.com/rolker/unh_marine_radar/issues/13)),
  which migrates off `marine_interfaces/Detect`. `Detect` retirement follows in
  [#162](https://github.com/rolker/unh_marine_autonomy/issues/162), gated on that
  migration.
- The contact/target manager (store, CRUD, CAMP rendering) is L3 and out of scope
  here.
- No convention tests land with the definition (IDL has no logic; the package has
  no `test/` dir); round-trip/convention tests land with the first producer.

## References

- [#156](https://github.com/rolker/unh_marine_autonomy/issues/156) /
  PR [#161](https://github.com/rolker/unh_marine_autonomy/pull/161) — this design.
- [rolker/camp#93](https://github.com/rolker/camp/issues/93) — legacy `Contact`
  dead-code removal that freed the name.
- [rolker/unh_marine_radar#13](https://github.com/rolker/unh_marine_radar/issues/13)
  — first producer migration.
- [#162](https://github.com/rolker/unh_marine_autonomy/issues/162) — `Detect`
  retirement; [#158](https://github.com/rolker/unh_marine_autonomy/issues/158) —
  `KeyValue` standardization.
- [ADR-0001](0001-shared-scalar-colormap.md) — format precedent.
- Autoware `autoware_perception_msgs` — the object pattern this generalizes.
