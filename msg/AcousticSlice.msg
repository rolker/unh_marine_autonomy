# Used as part of the AcousticeLayer message.
# Range of samples from a single beam of a single ping,
# typically from a single beam echo sounder, meeting the
# criteria for inclusion in an AcousticLayer.

std_msgs/Header                        header

# Pose of the transducer at the time the ping was sent.
geometry_msgs/Pose            pose

marine_acoustic_msgs/PingInfo ping_info
geometry_msgs/Vector3         beam_unit_vec

float32                       minimum_range
float32                       maximum_range

float32                       center_of_mass_range

float32[]                     intensitites
