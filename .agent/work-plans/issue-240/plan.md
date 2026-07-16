# Plan: SonarInfo message prototype (CameraInfo analog for acoustic sensors)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/240

## Context

`marine_acoustic_msgs` deliberately left the semantics of intensity samples open
(`SonarImageData.dtype` gives bit width, not meaning; upstream marine_msgs #18/#46
are still open). Separately, the full radiometric GeoCoder backscatter correction
(cube#81's deferred follow-up) needs the **transmit pulse length** to compute the
ensonified area, and no existing message carries it — the M3's N/78 datagram
reports signal length per TX sector, but `kongsberg_em_bridge` skips the field
because there is nowhere to put it.

`SonarInfo` addresses both: a per-sensor, latched, bag-recorded metadata message —
the acoustic analog of `sensor_msgs/CameraInfo` — prototyped locally in
`marine_interfaces` for eventual upstreaming to `marine_acoustic_msgs`.

Field-collection driver: a survey opportunity is expected the week of 2026-07-20,
so the wire format needs to land now; the `kongsberg_em_bridge` producer is the
immediate follow-up (separate issue in `marine_tools`).

## Approach

1. **`msg/SonarInfo.msg`** in the existing `marine_interfaces` package (no new
   package). Field set = the #240 strawman plus a new **acquisition-settings
   block**:
   - `float32[] pulse_lengths` — per TX sector (parallel to how `PingInfo` does
     beamwidths); single-sector sonars publish one element.
   - `float32[] bandwidths` — required to recover the effective
     (post-matched-filter) pulse length of FM pulses (review round-1 addition).
   - `uint8[] tx_signal_types` (CW / FM up / FM down) — determines whether
     `pulse_lengths` is the physical or effective length.
   - Flagged as candidates for per-ping placement (`PingInfo`) if upstreamed.
   - Every enum gets `*_UNKNOWN = 0` so a default-constructed message is honest
     (deviation from the strawman, which had e.g. `NONE = 0` for TVG); the
     strawman's normalization bool became tri-state `angular_normalization`,
     and NaN sentinels are a stated producer obligation (review round-1).
   - Publish contract = latched + republish-on-change **+ slow heartbeat**
     (≤ 10 s) so every rosbag2 split segment captures one (review round-1).
2. **`CMakeLists.txt`** — add the message to `MSG_FILES`.
3. **ADR-0009** — the interface decision: topic/QoS pattern (latched
   `transient_local`, republish-on-change), intrinsic-vs-per-ping split, the
   CameraInfo relationship, raw-vs-corrected framing (working decision:
   as-published semantics + applied corrections; revisitable), upstream intent.
   Cites the NIWA/BWSG report and marine_msgs #18/#46.
4. Build + lint (`marine_interfaces` only), open PR (`Closes #240`), comment on
   #240 documenting the acquisition-settings addition to the strawman.

## Out of scope (follow-ups)

- `kongsberg_em_bridge` publishing `SonarInfo` (new issue in `marine_tools`).
- Adding the topic to the platform bag-record list (`unh_echoboats_project11`).
- Consumers (CUBE reading the ARA curve from `SonarInfo`).
- Upstreaming to `marine_acoustic_msgs`.

## Verification

- `marine_interfaces` builds (rosidl generation) in the issue-240 worktree.
- Package lint tests pass (`test.sh marine_interfaces`).
- `ros2 interface show marine_interfaces/msg/SonarInfo` renders the message.
