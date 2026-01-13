# Occupancy probability in the range [0,100].  Unknown is -1.
int8 occupancy_probability

# Polygon assigned the above occupancy value.
# Special case if polygon only has two points: first point is the center of a circle
# and distance to the second point is the radius of the circle.
GeoPolygon polygon

