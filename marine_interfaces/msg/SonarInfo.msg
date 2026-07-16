# Per-sensor acoustic metadata: the acoustic analog of sensor_msgs/CameraInfo.
#
# Published on a parallel latched (transient_local, depth 1) topic alongside a
# sonar data stream (marine_acoustic_msgs RawSonarImage / SonarDetections /
# ProjectedSonarImage), with header.frame_id matching that stream, and recorded
# in bags so offline processing has the sensor metadata too.
#
# Publish contract: publishers MUST re-publish whenever any field changes
# (e.g. an operator range-scale change altering the pulse length) AND at a
# slow heartbeat (period <= 10 s recommended). The heartbeat is load-bearing:
# rosbag2 does not re-persist a latched message into new segments when a
# recording splits, so change-only publishing would leave later segments with
# no SonarInfo at all. Consumers associate a ping with the most recent
# SonarInfo at or before its stamp.
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
# Conventions for absent / unknown values:
# - Enum fields: *_UNKNOWN = 0, so a default-constructed message is honest.
# - Float fields marked "NaN if unknown": producers MUST set NaN explicitly —
#   the rosidl default (0.0) is a plausible physical value, not a sentinel.
# - Arrays: empty = not reported. Parallel arrays MUST be equal length when
#   both are non-empty; consumers MUST verify before parallel indexing.
# - calibration_ref/calibration_time: empty string / zero time = uncalibrated.
#
# Local prototype for eventual upstreaming to marine_acoustic_msgs
# (apl-ocean-engineering/marine_msgs). See ADR-0009.

std_msgs/Header header             # frame_id = sensor frame of the data stream

string sonar_model                 # e.g. "kongsberg-m3"

# Cross-reference to an external calibration record (e.g.
# marine_mbes_backscatter_store StoreMetadata.calibration_ref).
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

# Transmit pulse (signal) length in seconds. For CW this is the effective
# pulse length; for FM it is the transmitted sweep length, and the effective
# length after matched filtering is ~1/bandwidth — consumers need
# tx_signal_types and bandwidths to tell the cases apart.
float32[] pulse_lengths

# Transmit bandwidth in Hz. Parallel to pulse_lengths. For FM pulses this is
# required to recover the effective (post-matched-filter) pulse length; for CW
# it is informative. Empty if not reported.
float32[] bandwidths

uint8 SIGNAL_TYPE_UNKNOWN = 0
uint8 SIGNAL_TYPE_CW = 1
uint8 SIGNAL_TYPE_FM_UP = 2
uint8 SIGNAL_TYPE_FM_DOWN = 3
# Parallel to pulse_lengths. Empty if not reported — consumers MUST then treat
# every pulse as SIGNAL_TYPE_UNKNOWN (no CW assumption).
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

# Relative vs absolute only — whether the values are dB or linear is carried
# entirely by intensity_scale, keeping the axes orthogonal.
uint8 REFERENCE_UNKNOWN = 0
uint8 REFERENCE_UNCALIBRATED_RELATIVE = 1
uint8 REFERENCE_ABSOLUTE_1UPA = 2  # absolute, referenced to 1 micropascal
uint8 intensity_reference

# Linear map from a raw sample value to the quantity/scale/reference above:
# physical = raw * scale + offset. scale = 0 means no mapping provided and
# offset is then meaningless (consumers ignore it).
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

# Absorption coefficient used by the applied TVG. NaN if unknown / no TVG
# (producers MUST set NaN explicitly; see conventions above).
float32 tvg_absorption_db_per_km

# Transmit source level. NaN if relative / uncalibrated (producers MUST set
# NaN explicitly; see conventions above).
float32 source_level_db

# Whether an angular-response normalization has been applied to the stream.
# Tri-state so a default-constructed message claims nothing.
uint8 ANGULAR_NORMALIZATION_UNKNOWN = 0
uint8 ANGULAR_NORMALIZATION_NOT_APPLIED = 1
uint8 ANGULAR_NORMALIZATION_APPLIED = 2
uint8 angular_normalization

# Empirical mean angular-response curve for this sonar (ARA/AVG, e.g. the
# cube_bathymetry#81 M3 curve): |angle from nadir| bin centres in degrees,
# ascending, with mean backscatter level relative to nadir. Parallel arrays;
# empty if unavailable.
float32[] angular_response_angle_deg
float32[] angular_response_db_rel_nadir

# Transmission-loss provenance of the curve above (cube_bathymetry#87): a
# TL_REMOVED (tier-2) curve is a residual and the consumer must add back
# 40*log10(R) + 2*alpha*R (alpha = angular_response_absorption_db_per_m)
# before subtracting it; a TL_IN (tier-1) curve is subtracted as-is.
# Consumers MUST NOT apply a non-empty curve whose provenance is UNKNOWN —
# guessing the TL model mis-corrects every sample it touches.
uint8 ANGULAR_RESPONSE_TL_UNKNOWN = 0
uint8 ANGULAR_RESPONSE_TL_IN = 1
uint8 ANGULAR_RESPONSE_TL_REMOVED = 2
uint8 angular_response_tl

# Absorption used when the curve's TL was removed. NaN if unknown or TL_IN
# (producers MUST set NaN explicitly; see conventions above). Deliberately
# dB per METRE — unlike tvg_absorption_db_per_km above — because consumers
# apply this alpha verbatim in 40*log10(R) + 2*alpha*R with R in metres
# (cube_bathymetry#87 never recomputes it).
float32 angular_response_absorption_db_per_m

# Combined transmit/receive beam-pattern gain vs angle. Parallel arrays;
# empty if unavailable.
float32[] beam_pattern_angle_deg
float32[] beam_pattern_gain_db
