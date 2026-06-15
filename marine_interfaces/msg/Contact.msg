# A unified, sensor-agnostic perception contact: one message spanning all
# sensors (sonar, radar, lidar, camera) and all kinematic states (static
# seabed object <-> moving vessel), with a human-curation layer. Modeled on
# the Autoware object pattern (kinematic state and shape are fields, not
# separate message types). Per-sensor observations (vision_msgs detections,
# radar/sonar returns, AIS) project up into a Contact and are referenced via
# observation_ids; their full-fidelity domain messages are kept, not folded in.

std_msgs/Header header   # stamp + frame_id (the frame the kinematics live in)

string id                # stable id (may hold a UUID); used for CRUD / sync /
                         # "revisit X". String for operator-facing/edited contacts.

# --- unified perception core (composed) ---
float32          existence_probability  # objectness in [0, 1]; human-drawn = 1.0
Classification[] classification         # hypotheses; human = single @ 1.0
Kinematics       kinematics
Shape            shape

# --- archival geo (resolved/derived) ---
# kinematics is a local metric frame, so the archival pose carries its own
# geographic orientation. Explicitly a derived value, for persistence and
# operator<->boat sync (per the bathy-store pattern).
# Unset/unresolved convention (an all-zero GeoPose is NOT "unset" -- (0,0,0)
# is a valid location): geo_pose is unresolved when position.latitude is NaN;
# orientation is unknown when the quaternion is all-zero (0,0,0,0, i.e. not
# normalized). Mirrors the covariance[0]==-1 unknown convention above.
geographic_msgs/GeoPose geo_pose

# --- provenance / curation ---
string source                 # sensor+stream: "sidescan.port", "m3", "radar", "bow_cam"

uint8 ORIGIN_AUTO  = 0
uint8 ORIGIN_HUMAN = 1
uint8 origin_kind

uint8 STATUS_PROPOSED  = 0
uint8 STATUS_CONFIRMED = 1
uint8 STATUS_REJECTED  = 2
uint8 status                  # human-review lifecycle (MCM detect->confirm pattern)

string   note
string[] observation_ids      # refs to source vision_msgs / sonar / radar detections
diagnostic_msgs/KeyValue[] attributes
