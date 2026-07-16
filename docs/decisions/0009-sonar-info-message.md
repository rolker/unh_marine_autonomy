# ADR-0009: `SonarInfo` — Per-Sensor Acoustic Metadata Message

## Status

Proposed (2026-07-16). Tracked by
[rolker/unh_marine_autonomy#240](https://github.com/rolker/unh_marine_autonomy/issues/240).

Relates to **ADR-0006/0007** (backscatter stores — `calibration_ref` is the hook
into their provenance records) and supersedes the CSV/parameter delivery of the
angular-response curve from
[cube_bathymetry#81](https://github.com/rolker/cube_bathymetry/issues/81)
(the curve rides in the message instead, once producers/consumers exist).

## Context

Two long-standing gaps in `marine_acoustic_msgs`
([apl-ocean-engineering/marine_msgs](https://github.com/apl-ocean-engineering/marine_msgs))
motivate a companion per-sensor message:

**1. Intensity semantics were deliberately left open.** `SonarImageData` carries
`dtype` (UINT8…FLOAT64) plus raw bytes — bit width, but nothing about what a
sample *represents*. `SonarDetections.intensities` is documented "usually
uncalibrated and crude."
[marine_msgs#18](https://github.com/apl-ocean-engineering/marine_msgs/issues/18)
("rife with confusion": linear amplitude vs power vs raw ADC counts vs dB;
relative vs absolute) and
[marine_msgs#46](https://github.com/apl-ocean-engineering/marine_msgs/issues/46)
(require normalized radar echoes?) are both still open. The in-message fixes
proposed there (rename a field to `relative_intensity_dB`; mandate
normalization) bake **one** representation into a **shared** message — wrong
across sonars. Multibeam backscatter calibration is genuinely hard (see the
NIWA/BWSG report,
<https://niwa.co.nz/static/BWSG_REPORT_MAY2015_web.pdf>); the message layer
should *declare* semantics per sensor, not legislate them globally.

**2. Radiometric correction inputs have nowhere to live.** The full GeoCoder
backscatter chain (Fonseca & Calder) needs the **ensonified area**, which
depends on the **transmit pulse length** — deferred as a follow-up when
cube#81 landed the empirical angular-response first cut. No
`marine_acoustic_msgs` message carries pulse length: `PingInfo` has only
`frequency`, `sound_speed`, and beamwidths. The Kongsberg M3's Raw Range &
Angle (N/78) datagram reports signal length per transmit sector, and
`kongsberg_em_bridge` walks straight past it (`em_datagrams.py` unpacks
`tx_delay`/`centre_frequency` at sector offset +8, skipping `siglen` at +4)
because there is nowhere to publish it. Surveys are therefore not collecting
data we are already receiving.

## Decision

Add **`marine_interfaces/msg/SonarInfo`** — the acoustic analog of
`sensor_msgs/CameraInfo`: per-sensor, slowly-changing metadata on a parallel
topic, frame-aligned with the data stream and recorded in bags.

### Topic / QoS pattern

- Published on a sibling topic next to the data stream (e.g. `.../sonar_info`
  beside `.../detections`), `header.frame_id` matching the stream.
- **Latched**: `transient_local`, depth 1, so late joiners capture it.
- **Republish on change AND on a slow heartbeat** (period ≤ 10 s recommended):
  any field change (e.g. an operator range-scale change altering pulse
  length) emits a fresh message immediately. The heartbeat exists because
  latching alone does not survive **rosbag2 bag splitting** — the recorder
  captures the latched value when the subscription is made but does not
  re-persist it into later split segments, so a change-only publisher would
  leave every segment after the first without any `SonarInfo`. At heartbeat
  rates the bandwidth cost is negligible even with the curve arrays populated.
  Consumers associate a ping with the most recent `SonarInfo` at or before
  its stamp.

### Field set (see the message for authoritative comments)

- **Identity / calibration hook**: `sonar_model`, `calibration_ref` (points at
  e.g. `marine_mbes_backscatter_store` `StoreMetadata.calibration_ref` —
  store-level per the ADR-0005 #248 amendment / ADR-0007 A.3),
  `calibration_time`.
- **Acquisition settings** *(new relative to the #240 strawman)*:
  `pulse_lengths` + `bandwidths` + `tx_signal_types` (CW / FM up / FM down),
  one element per transmit sector — the GeoCoder ensonified-area inputs.
  Bandwidth is required alongside pulse length because an FM pulse's
  *effective* (post-matched-filter) length is ~1/bandwidth; without it the
  ensonified area of a chirp is uncomputable from the bag. Arrays mirror how
  `PingInfo` handles per-sector/per-beam quantities.
- **Intensity semantics**, decomposed into the three independent axes
  marine_msgs#18 conflates: `intensity_quantity` (raw ADC / amplitude /
  power), `intensity_scale` (linear / dB), `intensity_reference`
  (relative vs absolute-re-1 µPa **only** — whether values are dB or linear
  is carried entirely by `intensity_scale`, keeping the axes orthogonal),
  plus a raw→physical `scale`/`offset` map. `SonarImageData.dtype` (bytes) +
  `SonarInfo` (meaning) together are unambiguous, with no field renames
  forced on the shared messages.
- **Correction state**: `tvg_model` + `tvg_absorption_db_per_km`,
  `source_level_db`, `angular_normalization` (tri-state), the empirical
  angular-response curve (cube#81's ARA/AVG curve as data), and a beam-pattern
  curve.
- **Curve TL provenance** *(added by
  [#268](https://github.com/rolker/unh_marine_autonomy/issues/268), same-day
  addendum before any curve-bearing bags existed — no migration rule
  needed)*: `angular_response_tl` (tri-state `UNKNOWN`/`TL_IN`/`TL_REMOVED`)
  + `angular_response_absorption_db_per_m`, mirroring the estimator's
  `AngularResponseCurve` from
  [cube#87](https://github.com/rolker/cube_bathymetry/issues/87) — a tier-2
  curve is a TL-removed residual whose consumer must add back
  `40·log10(R) + 2αR` first. Consumers reject a non-empty curve with
  `UNKNOWN` provenance rather than guess the TL model. Delivery arc:
  producer
  [marine_tools#71](https://github.com/rolker/marine_tools/issues/71),
  consumer
  [cube#102](https://github.com/rolker/cube_bathymetry/issues/102)
  (curve presence auto-enables the correction; explicit config overrides).

### Design rules

- **Complement, don't restate, `PingInfo`**: genuinely per-ping quantities
  (frequency, sound speed, beamwidths) stay in `PingInfo`.
- **Raw-plus-applied framing** *(working decision — revisit before
  upstreaming)*: `SonarInfo` describes the stream **as published** — the
  native semantics of the samples plus the corrections already applied — so a
  consumer knows the current state and can undo or extend the chain. The
  alternative (describing a corrected/target representation) was considered
  and set aside for now.
- **Honest defaults, scoped by type.** Every enum has `*_UNKNOWN = 0` (a
  deviation from the strawman, which had `NONE = 0` for TVG), and the
  strawman's `backscatter_angle_normalized` bool became the tri-state
  `angular_normalization` — for these fields a default-constructed message
  makes no false claims ("not applied" is an assertion; "unknown" is the
  honest default). Float fields cannot get the same guarantee: rosidl
  defaults them to 0.0, a plausible physical value, so the NaN-if-unknown
  sentinels are a **producer obligation** (set NaN explicitly), stated in the
  message's conventions block rather than relied on from defaults.
- **Blended correction model** (option b from #240): one empirical
  angular-response curve carries the whole correction for now; a separate
  "processing parameters" message may split out later.
- **Pulse length placement**: per-ping placement (a `PingInfo` extension) is
  arguably more correct since pulse length tracks operator settings; it lives
  here for now because `marine_acoustic_msgs` is consumed as an installed
  binary and cannot be extended locally, and latched republish-on-change
  captures setting transitions in the bag. Flagged in the message as a
  candidate to migrate when upstreaming (where
  [marine_msgs#24](https://github.com/apl-ocean-engineering/marine_msgs/issues/24)
  and
  [marine_msgs#36](https://github.com/apl-ocean-engineering/marine_msgs/issues/36)
  already propose `PingInfo` additions).

### Home and upstream intent

Prototyped in the existing `marine_interfaces` package (no new package — the
endgame is upstreaming to `marine_acoustic_msgs`, so a dedicated local package
would be throwaway). This ADR intentionally proves the wire format with **no
producers or consumers** in the same change.

## Consequences

- **Producers** (follow-ups, separate issues): `kongsberg_em_bridge` parses the
  N/78 `siglen` field and publishes `SonarInfo` (`marine_tools`);
  `garmin_sidescan` later. The platform bag-record topic list gains the
  `sonar_info` topics (`unh_echoboats_project11`).
- **Consumers** (follow-up): the CUBE estimator reads the angular-response
  curve from `SonarInfo` instead of the `--backscatter-curve` CSV, retiring
  the cube#81 delivery mechanism.
- Offline GeoCoder-style processing gains its missing ensonified-area input
  from bags recorded on future surveys; past bags remain without pulse length.
- Upstreaming will re-open the per-ping-vs-per-sensor split for the
  acquisition block; the raw-vs-corrected framing decision must be finalized
  before proposing there.
- **Bag-migration debt is accepted**: bags recorded in the interim carry the
  `marine_interfaces/msg/SonarInfo` type name. When the message upstreams
  (and/or `pulse_lengths` migrates into `PingInfo`), a
  `marine_interfaces/bmr/` `MessageUpdateRule` must be added so those bags —
  the data of record — remain replayable. The package already carries
  `bmr/from_marine_msgs.bmr` as precedent.
- One more message in `marine_interfaces`'s already-broad surface; acceptable
  given the explicit upstream exit path.
