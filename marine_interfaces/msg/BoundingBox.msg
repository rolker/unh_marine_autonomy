# Axis aligned Cartesian map bounding box. 
#
# Cartesian version of a geographic_msgs/BoundingBox
#
# The two Points denote diagonally opposite corners of the box.
#
# If min_pt.x is NaN, the bounding box is "global", matching
# any valid x, y, and z.
#
# If min_pt.z is NaN, the bounding box is two-dimensional and
# matches any z value within the specified x and y 
# range.

geometry_msgs/Point min_pt # lowest and most Southwestern corner
geometry_msgs/Point max_pt # highest and most Northeastern corner

