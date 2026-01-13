# This represents 2-D map area where polygons define the probability of
# occupancy.

std_msgs/Header header

# Value to use when not covered by a polygon
int8 default_occupancy_probability

# Area represented by this map
BoundingBox bounds

OccupancyPolygon[] polygons
