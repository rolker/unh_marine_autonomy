# This represents a position and velocity in free space.
# It is meant to be a lighter weight version of nav_msgs/Odemetry to be
# used in planning.
# The pose in this message should be specified in the coordinate frame given by header.frame_id.
# The twist in this message should be specified in the coordinate frame given by the child_frame_id
std_msgs/Header header
string child_frame_id
geometry_msgs/Pose pose
geometry_msgs/Twist twist
