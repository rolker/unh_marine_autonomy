# ADR-0003: Bridgeable Device Control Protocol & Libraries (`marine_control`)

## Status

Proposed (2026-06-13). Tracked by
[rolker/unh_marine_autonomy#140](https://github.com/rolker/unh_marine_autonomy/issues/140)
(umbrella); this ADR is authored under
[#149](https://github.com/rolker/unh_marine_autonomy/issues/149).

The full design and work breakdown are in the umbrella issue body; this ADR
records the load-bearing architecture decisions and their rationale so they
survive the issue. It is a **cross-cutting** decision in the sense ADR-0001
establishes for this repo's `docs/decisions/`: it spans a new standalone
`marine_control` repo, `rqt_operator_tools`, and the first adopters
(`garmin_sidescan` / `marine_tools`, the segmentation→costmap tuning in
`unh_marine_perception`, and the collision-monitor / e-stop params).

> **Numbering note.** The umbrella issue originally called this "ADR-0002." That
> number was taken by the bathymetric data store
> ([ADR-0002](0002-bathymetric-data-store.md)) before this was written, so the
> design of record is **ADR-0003**. Issue #140 has been updated to match.

## Context

The operator station talks to the boat over `udp_bridge`, which carries **only
pub/sub topics — not services** (`udp_bridge`'s `doc/qos_design.md`). ROS 2
parameters and `rqt_reconfigure` are **service-based**, so every parameter and
dynamic-reconfigure panel is **invisible topside**. Anything an operator must
view or change from the station has to be expressed as a **topic-based,
bidirectional** contract instead: a `state` topic (device→operator, "here is
every setting and its current value/range") and a `change` topic
(operator→device, "set this setting to this value").

That contract already exists, informally, as `marine_radar_control_msgs`
(`RadarControlSet` / `RadarControlValue`), reused by the Garmin sidescan driver
and rendered by **two separate** rqt plugins (`rqt_marine_radar` and the
sidescan panel). So the pattern is **real, proven, and already duplicated** —
worth promoting to a first-class, reusable mechanism rather than copied a third
time.

Two field lessons motivate the specifics:

1. **The 2026-06-10 deployment (#250)** is the worked example of getting the
   bridge/QoS wiring wrong: a control contract is useless if `state` and
   `change` are not *both* bridged in the right directions with the right QoS.
   The sidescan controls were not fully bridged, so the panel could not drive
   the device topside.
2. **No UI dependency may reach the boat.** The radar pattern entangles message
   shapes with rqt rendering across repos. The boat-side stack must never pull
   Qt to publish a control set.

## Decision

Build a generic, bridge-correct device-control mechanism — a message package
plus a device-side library, with the UI strictly separated. The decisions below
are the ones future work must not silently re-litigate.

### D1 — Topic-based bidirectional contract, never services

Device control is a pair of topics, not a service or a parameter:

- **`state`** (device→operator): the full set of controls, each with its
  current value, type, range/enum, and metadata. Self-describing, so the UI is
  generic.
- **`change`** (operator→device): a request to set one control. **Fire-and-
  forget**; success is observed by the next `state` echo, not by a return code
  (there is no return code over a topic).

This is non-negotiable because `udp_bridge` cannot carry services. Any future
"just use parameters" temptation fails topside.

### D2 — Leave `marine_radar_control_msgs` / `rqt_marine_radar` untouched

The radar messages and plugin are used by other consumers. Migrating them is out
of scope and not worth the churn. **Accept exactly one duplicate** (radar) and
make everything new use `marine_control`. Do not retrofit radar onto the new
contract.

### D3 — New generic `marine_control_msgs`, modeled on the radar messages

A `ControlSet` / `ControlValue` pair modeled closely on `RadarControlSet` /
`RadarControlValue` so the mental model transfers, with deliberate additions the
radar messages lack:

- **Value types**: `FLOAT`, `INT`, `BOOL`, `STRING`, `ENUM` (radar was
  effectively float/enum-only).
- **`units`** (e.g. `m`, `dB`, `Hz`) — for correct display and operator intent.
- **`step`** — granularity for sliders/spinners.
- **`group`** — UI grouping of related controls.
- **`description`** — human text, mirrors a parameter descriptor's description.
- **`read_only`** — display-only telemetry vs settable controls in one set.
- **`stamp`** on the set — staleness detection (a frozen panel must be
  detectable; ties to the heartbeat in D5).

Exact field layout is defined in the follow-on message-definition work, bounded
by these requirements.

### D4 — Standalone `marine_control` repo (msgs + device lib, **no Qt**); UI lives in `rqt_operator_tools`

Following the ADR-0001 / `marine_colormap` precedent:

- **`marine_control`** (new standalone repo): `marine_control_msgs` + the
  device-side library. **No Qt, no rqt, no UI** dependency — `ament_export`
  stays clean so the boat links only messages + the helper lib.
- **`marine_control_widgets`** (Qt lib) + a generic **`rqt_marine_control`**
  plugin live in **`rqt_operator_tools`**.

The split is load-bearing: it is the structural guarantee that **no UI
dependency reaches the boat** (the D-stated invariant). The boat publishes a
`ControlSet`; the operator station renders it.

### D5 — QoS contract baked into the device library

The library owns the QoS so adopters cannot get #250 wrong:

- **`state`**: `RELIABLE` + `VOLATILE` + a periodic **heartbeat** republish.
  **Never `TRANSIENT_LOCAL`** — latched state survives across the bridge in
  confusing ways and a late joiner must get fresh state from the heartbeat, not
  a stale latched sample.
- **`change`**: fire-and-forget; the operator confirms by watching `state`
  change (D1).

### D6 — Parameter-descriptor binding (one declaration, two surfaces)

A single `declare_parameter(name, default, descriptor)` on the device, bound
through the library, yields **both** the local `rqt_reconfigure` panel (services,
on-boat) **and** the remote `ControlSet` entry (topics, topside). The
`ParameterDescriptor` (range, step, description, read-only) is the single source
of truth; the library translates it to a `ControlValue`. Adopters declare
parameters once; they do not hand-maintain two representations.

### D7 — Bridge wiring is part of adoption, both directions

Adopting a device is not done until its `state` **and** `change` topics are
bridged the right way in the platform config (e.g. `bizzyboat.yaml`):
`state` boat→operator, `change` operator→boat. This closes the #250 gap and
**supersedes the partial approach in `marine_tools#31`**. An adoption PR that
ships the messages but not the bridge wiring is incomplete.

### D8 — Adoption phasing

Sequence the first adopters so the mechanism is proven on the simplest case
first:

1. **Sidescan** (`garmin_sidescan`): publish `ControlSet` via the lib; render
   with the generic widget; fix the bridge wiring (D7).
2. **Costmap tuning**: segmentation→costmap parameters via descriptor binding
   (D6).
3. **E-stop / collision-monitor params**: **safety-gated** — change requires
   confirmation and a change audit (who set what, when), beyond the plain
   fire-and-forget of D1.

## Consequences

- One reusable contract replaces ad-hoc per-device control plumbing; new
  tunable devices get a topside panel for free by declaring parameters with
  descriptors (D6).
- The boat-side dependency graph stays Qt-free (D4), enforced structurally by
  repo boundaries.
- The radar duplicate (D2) persists indefinitely; that is an accepted cost, not
  a TODO.
- E-stop control (D8.3) introduces a safety-critical write path over a
  fire-and-forget topic; the confirmation + audit requirement is a hard gate on
  that adoption, called out so it is not treated like the others.
- `udp_bridge` gains a recurring, well-defined small-payload load per controlled
  device (state heartbeat); bounded and far below the monolithic-grid problem of
  ADR-0002's D6.

## References

- [unh_marine_autonomy#140](https://github.com/rolker/unh_marine_autonomy/issues/140)
  — umbrella issue with full design + sub-issue breakdown.
- [#149](https://github.com/rolker/unh_marine_autonomy/issues/149) — this ADR.
- [ADR-0001](0001-shared-scalar-colormap.md) / #137 — standalone-repo + format
  precedent.
- [ADR-0002](0002-bathymetric-data-store.md) — adjacent cross-cutting decision;
  source of the `udp_bridge` payload lesson (#250).
- `udp_bridge` `doc/qos_design.md` — why pub/sub-only and the QoS basis for D5.
- `marine_radar_control_msgs` / `rqt_marine_radar` — the proven pattern this
  generalizes.
- marine_tools#31 — the partial sidescan-controls-over-the-bridge attempt this
  supersedes (D7).
