# Prototype under #403 (coverage planner design); expect revision.
#
# What "surveyed to standard" means for one area: the allowed vertical
# uncertainty as a function of depth, in the form every hydrographic
# standard uses (IHO S-44, NOAA HSSD, and others):
#
#     allowed(depth) = sqrt(a^2 + (b * depth)^2)
#
# a is in metres at 95 % confidence; b is dimensionless. depth is depth
# below the instantaneous water surface, in metres, as S-44 defines it. A
# cell meets the standard when its uncertainty (also 95 %, which is what the
# CUBE grid's uncertainty layer carries at its default 1.96 sigma scale) is
# at most allowed(depth) for its own depth.
#
# Shared by the survey request, the planner, the assessment, and any
# display, so there is exactly one definition of "good enough" in flight.
#
# Defaults fail closed: a is checked first, before any cell is compared. A
# default-constructed standard (a = b = 0), or one with a or b NaN, negative
# or infinite, means "not set", and nothing meets it, whatever the cell's
# uncertainty (a cell with uncertainty exactly 0 would otherwise pass
# 0 <= 0). a and b are finite and >= 0 in a standard that is set.
string  name    # e.g. "S-44 Order 1a", "demo"; free text, for people
float64 a       # metres; the depth-independent part
float64 b       # dimensionless; the depth-proportional part
