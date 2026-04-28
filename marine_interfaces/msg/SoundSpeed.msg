# Single-point sound speed measurement from a real-time sensor at a fixed
# installed location (e.g., AML SVS at the sonar head).
#
# This message is NOT intended to represent a sample from a sound speed
# profile (cast). A dedicated profile message type should be used for that
# case, since profiles have additional structure (per-sample depth and/or
# pose, shared cast metadata) that does not belong here.

std_msgs/Header header

# Speed of sound in water in m/s. NaN if unavailable.
float64 sound_speed

# Variance on sound_speed in (m/s)^2. 0 is interpreted as variance unknown.
float64 variance
