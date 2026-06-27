# Addresses a whole GGGS tile (a level/row/col cell block in the Global
# Geographic Grid System). The ROS mirror of gggs::GridIndex {level,row,col};
# named TileIndex (not GridIndex) for consistency with the Tile* message family
# and to avoid the grid_map cell-index reading. See ADR-0008.

uint8  level   # GGGS level (cell-size-derived); e.g. bathy L10, sidescan L13
uint32 row     # GGGS tile row
uint32 col     # GGGS tile column
