# Shape of a perception Contact, from least to most shape information.
# SPHERE and POINT cover isotropic / extentless cases without faking a box
# orientation.

uint8 POINT    = 0
uint8 SPHERE   = 1
uint8 CYLINDER = 2
uint8 BOX      = 3
uint8 POLYGON  = 4
uint8 type

geometry_msgs/Polygon footprint   # used when type == POLYGON
geometry_msgs/Vector3 dimensions  # extents for SPHERE/CYLINDER/BOX (m)
