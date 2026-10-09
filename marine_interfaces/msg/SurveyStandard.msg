# Prototype under #403 (coverage planner design); expect revision.
#
# What "surveyed to standard" means for one area: the allowed vertical
# uncertainty as a function of depth, in the form every hydrographic
# standard uses (IHO S-44, NOAA HSSD, and others):
#
#     allowed(depth) = sqrt(a^2 + (b * depth)^2)
#
# All values in metres at 95 % confidence. depth is depth below the
# instantaneous water surface, as S-44 defines it. A cell meets the
# standard when its uncertainty (also 95 %, which is what the CUBE grid's
# uncertainty layer carries at its default 1.96 sigma scale) is at most
# allowed(depth) for its own depth.
#
# Shared by the survey request, the planner, the assessment, and any
# display, so there is exactly one definition of "good enough" in flight.
#
# Defaults fail closed: a default-constructed standard (a = b = 0) gives
# allowed(depth) = 0, so nothing meets it. Consumers treat a default or NaN
# standard as "not set / nothing meets it".
string  name    # e.g. "S-44 Order 1a", "demo"; free text, for people
float64 a       # metres; the depth-independent part
float64 b       # dimensionless; the depth-proportional part
