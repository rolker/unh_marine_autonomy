# Per-sensor acoustic metadata: the acoustic analog of sensor_msgs/CameraInfo.
#
# Published on a parallel latched (transient_local, depth 1) topic alongside a
# sonar data stream (marine_acoustic_msgs RawSonarImage / SonarDetections /
# ProjectedSonarImage), with header.frame_id matching that stream, and recorded
# in bags so offline processing has the sensor metadata too. Publishers MUST
# re-publish whenever any field changes (e.g. an operator range-scale change
# altering the pulse length).
#
# SonarInfo complements marine_acoustic_msgs/PingInfo — it does not restate it.
# Genuinely per-ping quantities (frequency, sound_speed, beamwidths) stay in
# PingInfo; this message carries what PingInfo lacks: acquisition settings,
# intensity semantics, and the correction state of the stream.
#
# This message describes the stream AS PUBLISHED: the native semantics of the
# intensity samples plus the corrections already applied to them, so a consumer
# knows the current state and can undo or extend the correction chain.
#
# Local prototype for eventual upstreaming to marine_acoustic_msgs
# (apl-ocean-engineering/marine_msgs). See ADR-0009.

std_msgs/Header header             # frame_id = sensor frame of the data stream

string sonar_model                 # e.g. "kongsberg-m3"

# Cross-reference to an external calibration record (e.g.
# marine_mbes_backscatter_store SourceRecord.calibration_ref).
# Empty / zero time if uncalibrated.
string calibration_ref
builtin_interfaces/Time calibration_time

# ---------------------------------------------------------------------------
# Acquisition settings
#
# Sonar settings required for radiometric correction (GeoCoder-style
# ensonified-area computation) that PingInfo does not carry. One element per
# transmit sector, in sector order; single-sector sonars publish one element.
# Empty if the sonar does not report them. These change with operator settings
# (range scale / mode) — republish on change. Candidates for per-ping placement
# (PingInfo) when upstreamed.
# ---------------------------------------------------------------------------

# Transmit pulse (signal) length in seconds. For FM pulses this is the
# transmitted sweep length; the effective length after matched filtering is
# shorter (~1/bandwidth) — tx_signal_types tells consumers which case applies.
float32[] pulse_lengths

uint8 SIGNAL_TYPE_UNKNOWN = 0
uint8 SIGNAL_TYPE_CW = 1
uint8 SIGNAL_TYPE_FM_UP = 2
uint8 SIGNAL_TYPE_FM_DOWN = 3
# Parallel to pulse_lengths. Empty if not reported.
uint8[] tx_signal_types

# ---------------------------------------------------------------------------
# Intensity semantics
#
# What the per-sample intensity values REPRESENT — the ambiguity
# marine_acoustic_msgs left open (apl-ocean-engineering/marine_msgs#18, #46) —
# decomposed into three independent axes. SonarImageData.dtype gives the bit
# width; these fields give the meaning.
# ---------------------------------------------------------------------------

uint8 QUANTITY_UNKNOWN = 0
uint8 QUANTITY_RAW_ADC = 1         # raw converter counts
uint8 QUANTITY_AMPLITUDE = 2       # linear amplitude / magnitude (~pressure)
uint8 QUANTITY_POWER = 3           # power / intensity (~pressure squared)
uint8 intensity_quantity

uint8 INTENSITY_SCALE_UNKNOWN = 0
uint8 INTENSITY_SCALE_LINEAR = 1
uint8 INTENSITY_SCALE_DB = 2
uint8 intensity_scale

uint8 REFERENCE_UNKNOWN = 0
uint8 REFERENCE_UNCALIBRATED_RELATIVE = 1
uint8 REFERENCE_DB_RE_1UPA = 2     # absolute, dB re 1 micropascal
uint8 intensity_reference

# Linear map from a raw sample value to the quantity/scale/reference above:
# physical = raw * scale + offset. scale = 0 means no mapping provided.
float32 scale
float32 offset

# ---------------------------------------------------------------------------
# Correction state
#
# Corrections already applied to (or available for) the published stream.
# ---------------------------------------------------------------------------

uint8 TVG_UNKNOWN = 0
uint8 TVG_NONE = 1                 # no time-varying gain applied
uint8 TVG_20LOGR = 2
uint8 TVG_40LOGR = 3
uint8 TVG_CUSTOM = 4
uint8 tvg_model

# Absorption coefficient used by the applied TVG. NaN if unknown / no TVG.
float32 tvg_absorption_db_per_km

# Transmit source level. NaN if relative / uncalibrated.
float32 source_level_db

# True if an angular-response normalization has been applied to the stream.
bool backscatter_angle_normalized

# Empirical mean angular-response curve for this sonar (ARA/AVG, e.g. the
# cube_bathymetry#81 M3 curve): |angle from nadir| bin centres in degrees,
# ascending, with mean backscatter level relative to nadir. Parallel arrays;
# empty if unavailable.
float32[] angular_response_angle_deg
float32[] angular_response_db_rel_nadir

# Combined transmit/receive beam-pattern gain vs angle. Parallel arrays;
# empty if unavailable.
float32[] beam_pattern_angle_deg
float32[] beam_pattern_gain_db
