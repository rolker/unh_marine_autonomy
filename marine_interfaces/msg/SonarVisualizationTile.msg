# A GGGS-tiled sonar DISPLAY product: one tile's worth of quantized, named
# bands for live operator visualization (boat -> CAMP / web viewer). This is a
# lossy display projection of the durable stores (bathy ADR-0002, backscatter
# ADR-0006/0007), never a competing storage schema -- it carries no provenance
# and is always rebuildable from the stores. See ADR-0008.
#
# Incremental update: bands carry only the DIRTY SUB-WINDOW of the tile (the
# region that changed), which the consumer patches into its tile at the window
# offset. A full-tile update sets the window to the whole tile. Live push is
# best-effort: a lost or reordered sub-window patch is healed by the
# catalog/TileRequest path, which re-sends the affected tile in full. The
# consumer orders patches by header.stamp (ADR-0008 D3); detailed
# patch-application semantics live in the consumer (camp #121).

# stamp = this tile's version time (boat clock; drives newest-wins ordering,
#         ADR-0008 D3). frame_id = display CRS tag (e.g. "gggs").
std_msgs/Header header

TileIndex index          # which GGGS tile

# Full tile size in cells. For GGGS this is the grid edge for index.level
# (e.g. 960x960), derivable from the GGGS spec; carried here as a convenience
# and a consistency check.
uint16 width
uint16 height

# Dirty sub-window in cell coordinates, bounded by the tile:
#   window_col + window_width  <= width
#   window_row + window_height <= height
# The band data arrays cover exactly this window, row-major. Cell (r,c) of the
# window is GGGS cell (window_row + r, window_col + c) -- rows/cols follow GGGS
# cell index order (the renderer georeferences via GGGS; no display orientation
# is implied).
uint16 window_col
uint16 window_row
uint16 window_width
uint16 window_height

VisualizationBand[] bands   # one entry per band present (depth/uncertainty/...)
