// vdatum_query.hpp — VDatum/PROJ grid query for vertical-datum resolution
// (ADR-0010 D6), extracted from mru_transform's chart_datum_node.
//
// Answers "what is the MLLW (and optionally MHHW) height relative to the
// WGS84 ellipsoid at this lat/lon?" from a geoid grid + regional VDatum
// `.gtx` grids on local disk. PROJ networking is disabled — resolution is
// strictly offline (ADR-0010 D6/D7: grids live wherever imports run, never
// in the runtime stack).
//
// Usage: build the query ONCE with make_vdatum_query() and call it per
// point. The returned callable owns the PROJ context and pipelines (shared,
// RAII); constructing it scans the grid directory and compiles the PROJ
// pipelines, so per-point work is a single proj_trans. Do NOT rebuild the
// query per point — a per-cell importer over millions of cells wants one
// factory call total.
//
// This header is PROJ-free: consumers only see std::function and plain
// structs, so linking PROJ stays this library's private concern.

#ifndef MARINE_VERTICAL_DATUM__VDATUM_QUERY_HPP_
#define MARINE_VERTICAL_DATUM__VDATUM_QUERY_HPP_

#include <functional>
#include <optional>
#include <string>

#include "marine_vertical_datum/datum_config.hpp"

namespace marine_vertical_datum
{

// Where the grids live. `geoid_grid` is the ellipsoid→NAVD88 geoid file
// (e.g. us_noaa_g2018u0.tif); `vdatum_grid_dir` is scanned recursively for
// regional NAVD88→datum `.gtx` grids named *_mllw*.gtx / *_mhhw*.gtx.
struct VDatumConfig
{
  std::string geoid_grid;
  std::string vdatum_grid_dir;
};

// The per-point query: (lat, lon) in degrees → VDatumResult, or nullopt
// where the grids have no MLLW coverage. A per-point nullopt is a normal
// gap condition (the precedence chain's polygon fallbacks handle it), so it
// deliberately carries no diagnostic — only setup problems are reported.
using VDatumQueryFn =
  std::function<std::optional<VDatumResult>(double lat, double lon)>;

// Diagnostic sink for setup-time problems (missing grids, PROJ pipeline
// failures). ROS-free replacement for the node's RCLCPP_* logging: callers
// route messages to their own logger; the default discards them.
using DiagFn = std::function<void (const std::string &)>;

// Production factory. Scans the grid directory, creates the PROJ context
// (networking disabled) and MLLW/MHHW pipelines, and returns the query.
// On setup failure (no MLLW grids, unreadable directory, pipeline creation
// error) reports the reason via `diag` and returns an EMPTY std::function —
// test with `if (query)` before use. A missing/failed MHHW pipeline is not
// fatal: the query works and mhhw_z stays nullopt (reported via `diag`).
//
// The returned callable is copyable; copies share one PROJ context and are
// NOT safe for concurrent use from multiple threads.
VDatumQueryFn make_vdatum_query(const VDatumConfig & config, DiagFn diag = {});

}  // namespace marine_vertical_datum

#endif  // MARINE_VERTICAL_DATUM__VDATUM_QUERY_HPP_
