# Prototype under #403 (coverage planner design); expect revision.
#
# One source's share of a CoverageAssessment, judged on that source's own
# grid, independent of the others. Sums across sources can exceed the
# totals, because sources overlap; area_unique is the part that cannot.
#
# Invariant: area_unique <= area_covered. area_unique concerns covered cells
# only and says nothing about whether they meet the standard.
#
# Consumers key on source exactly as given. It is stable for the life of a
# run, and the sources list in CoverageAssessment has the same order in
# every message.
string  source                  # platform name if known, else the grid topic
float64 area_covered            # cells this source has a depth for
float64 area_meeting_standard   # of those, cells whose own uncertainty meets the standard
float64 area_unique             # cells no other source covers: credit without double counting
