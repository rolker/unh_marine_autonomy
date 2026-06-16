# Pose and velocity of a perception Contact, expressed in the parent
# Contact's header.frame_id.
#
# Covariance "unknown" convention (matches sensor_msgs/Imu): a leading
# covariance element of -1 marks the whole block as unavailable, so no
# separate has_* flags are needed.
#   pose.covariance[0]  == -1 -> pose uncertainty unknown
#   twist.covariance[0] == -1 -> velocity unknown
#   twist == 0 with a real covariance -> known stationary

geometry_msgs/PoseWithCovariance  pose

# Orientation availability is not covariance-expressible, so it is explicit.
# ORIENTATION_SIGN_UNKNOWN is the sidescan-shadow case: the axis is known but
# the direction is ambiguous.
uint8 ORIENTATION_UNAVAILABLE  = 0   # position only
uint8 ORIENTATION_SIGN_UNKNOWN = 1   # axis known, direction ambiguous
uint8 ORIENTATION_AVAILABLE    = 2
uint8 orientation_availability

geometry_msgs/TwistWithCovariance twist
