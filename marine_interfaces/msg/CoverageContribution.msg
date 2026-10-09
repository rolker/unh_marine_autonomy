# Prototype under #403 (coverage planner design); expect revision.
#
# One source's share of a CoverageAssessment. Every area here is measured on
# the assessment raster, the same one as the totals: the source's grid is
# resampled to it and clipped to the polygon, so cells outside the polygon
# are counted in none of these areas. Coverage and meeting the standard are
# judged from this source alone, independent of the others. As in the union
# test, a cell meets the standard for a source only if that source has a
# finite depth and a finite uncertainty >= 0 there; anything else does not
# meet it. Sums across
# sources can exceed the totals, because sources overlap; area_unique is the
# part that cannot.
#
# Invariants: area_meeting_standard <= area_covered and
# area_unique <= area_covered. area_unique concerns covered cells only and
# says nothing about whether they meet the standard.
# Against the assessment: every source's area_covered is at most the
# assessment's area_covered, and the sum of area_unique over all sources is
# at most it too. A single source's area_meeting_standard can exceed the
# assessment's, because the union test takes depth from the source with the
# smallest uncertainty, which need not be this one.
#
# Consumers key on source exactly as given. It is stable for the life of a
# run, unique within one message (a platform that supplies two grids gets
# two distinct strings, e.g. its grid topics), and the sources list in
# CoverageAssessment has the same order in every message.
string  source                  # platform name if known, else the grid topic
float64 area_covered            # m^2: this source has a depth here
float64 area_meeting_standard   # m^2: of those, its own uncertainty meets the standard
float64 area_unique             # m^2: no other source covers it; credit without double counting
