# Prototype under #403 (coverage planner design); expect revision.
#
# How much of one SurveyArea is covered, and how much of that meets its
# standard, judged over the union of one or more bathymetry grids.
# Areas are in square metres of the assessment raster: the area was
# rasterised at cell_size (the CUBE cell size), so area_total is the
# rasterised polygon, not its geodetic area, and ratios between the three
# area fields are exact. Fractions are left to the consumer to compute.
# The per-source areas in sources are measured on this same raster, after
# resampling each source grid to it and clipping to the polygon.
# A cell is covered when any source has a depth for it. It meets the
# standard when the smallest uncertainty across sources is at most
# standard.allowed(depth), with depth taken from the source that has that
# smallest uncertainty. A covered cell with no uncertainty value does not
# meet the standard.
# header.frame_id is the frame of the assessment raster, a projected metric
# frame (metres). The polygon is projected into it and cell_size is metres
# in it. Source grids may be published in other frames (each boat has its
# own map frame, with its own origin); the assessor transforms them into
# this frame before resampling.
#
# Invariant: area_meeting_standard <= area_covered <= area_total. Cells
# outside the polygon are not counted in any of the three.
# The target polygon is area.polygon.points.
std_msgs/Header        header      # stamp: assessment time. frame_id: the assessment raster's frame
SurveyArea             area        # the area and standard this was judged against
float64                cell_size   # metres
float64                area_total
float64                area_covered
float64                area_meeting_standard
CoverageContribution[] sources     # one per grid, same order every message
