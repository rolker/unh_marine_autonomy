# Prototype under #403 (coverage planner design); expect revision.
#
# One target polygon and the standard it is to be surveyed to. A survey
# with several polygons sends several of these; id tells them apart and a
# repeat with the same id replaces the earlier definition.
# No frame: the polygon is geographic. stamp says when the definition was
# made, so a consumer that receives the same id twice out of order keeps
# the newer one. Ordering beyond stamp is the transport's job; no QoS
# contract is claimed here.
string                  id
builtin_interfaces/Time stamp
GeoPolygon              polygon    # ring closed implicitly; a repeated first point at the end is accepted too
SurveyStandard          standard
int16                   priority   # lower is more urgent; 0 if unused
