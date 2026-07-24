// vdatum_query.cpp — PROJ pipeline setup + per-point query, extracted from
// mru_transform's chart_datum_node (ADR-0010 D6). Setup-time problems go to
// the caller's DiagFn; per-point no-coverage is a plain nullopt.

#include "marine_vertical_datum/vdatum_query.hpp"

#include <proj.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace marine_vertical_datum
{

namespace
{

void report(const DiagFn & diag, const std::string & message)
{
  if (diag) {
    diag(message);
  }
}

// Collect .gtx grid files under `dir` whose stem contains `suffix`
// (e.g. "_mllw"), comma-joined for a +proj=vgridshift +grids= step. Sorted
// so the +grids= precedence (first grid with coverage wins on overlap) is
// deterministic across machines — recursive_directory_iterator order is
// filesystem-dependent. Throws fs::filesystem_error on an unreadable
// directory.
std::string collect_grids(const std::string & dir, const std::string & suffix)
{
  std::vector<std::string> paths;
  for (const auto & entry : fs::recursive_directory_iterator(dir)) {
    if (entry.path().extension() == ".gtx" &&
      entry.path().stem().string().find(suffix) != std::string::npos)
    {
      paths.push_back(entry.path().string());
    }
  }
  std::sort(paths.begin(), paths.end());

  std::string grids;
  for (const auto & path : paths) {
    if (!grids.empty()) {
      grids += ",";
    }
    grids += path;
  }
  return grids;
}

// Owns the PROJ context and pipelines; shared by all copies of the returned
// query so cleanup is RAII'd to the last copy.
struct ProjPipelines
{
  PJ_CONTEXT * context = nullptr;
  PJ * mllw = nullptr;
  PJ * mhhw = nullptr;

  ~ProjPipelines()
  {
    if (mllw) {
      proj_destroy(mllw);
    }
    if (mhhw) {
      proj_destroy(mhhw);
    }
    if (context) {
      proj_context_destroy(context);
    }
  }

  ProjPipelines() = default;
  ProjPipelines(const ProjPipelines &) = delete;
  ProjPipelines & operator=(const ProjPipelines &) = delete;
};

// Create a PROJ pipeline: ellipsoid → NAVD88 (geoid) → target datum (grids).
PJ * create_pipeline(
  PJ_CONTEXT * context, const std::string & geoid_grid,
  const std::string & datum_grids, const DiagFn & diag)
{
  const std::string pipeline =
    "+proj=pipeline "
    "+step +proj=vgridshift +grids=" + geoid_grid + " "
    "+step +proj=vgridshift +grids=" + datum_grids;

  PJ * pj = proj_create(context, pipeline.c_str());
  if (!pj) {
    report(
      diag, std::string("Failed to create PROJ pipeline: ") +
      proj_context_errno_string(context, proj_context_errno(context)));
  }
  return pj;
}

// Query one pipeline; returns the negated Z (datum height below ellipsoid,
// matching the datum_config sign convention) or false on no coverage.
bool query_datum(PJ * pipeline, double lon_rad, double lat_rad, double & result_z)
{
  const PJ_COORD input = proj_coord(lon_rad, lat_rad, 0.0, 0.0);
  const PJ_COORD output = proj_trans(pipeline, PJ_FWD, input);

  if (output.xyz.z == HUGE_VAL ||
    std::isinf(output.xyz.z) || std::isnan(output.xyz.z))
  {
    return false;
  }

  result_z = -output.xyz.z;
  return true;
}

}  // namespace

VDatumQueryFn make_vdatum_query(const VDatumConfig & config, DiagFn diag)
{
  if (config.geoid_grid.empty()) {
    report(diag, "VDatum setup: geoid_grid is empty");
    return {};
  }
  if (config.vdatum_grid_dir.empty()) {
    report(diag, "VDatum setup: vdatum_grid_dir is empty");
    return {};
  }

  std::string mllw_grids;
  std::string mhhw_grids;
  try {
    mllw_grids = collect_grids(config.vdatum_grid_dir, "_mllw");
    mhhw_grids = collect_grids(config.vdatum_grid_dir, "_mhhw");
  } catch (const fs::filesystem_error & e) {
    report(
      diag, "VDatum setup: error scanning vdatum_grid_dir '" +
      config.vdatum_grid_dir + "': " + e.what());
    return {};
  }

  if (mllw_grids.empty()) {
    report(
      diag, "VDatum setup: no *_mllw.gtx files found in " +
      config.vdatum_grid_dir);
    return {};
  }

  auto pipelines = std::make_shared<ProjPipelines>();
  pipelines->context = proj_context_create();
  // Strictly offline (ADR-0010 D6/D7): never fetch grids over the network —
  // resolution must behave identically on a disconnected boat.
  proj_context_set_enable_network(pipelines->context, false);

  pipelines->mllw =
    create_pipeline(pipelines->context, config.geoid_grid, mllw_grids, diag);
  if (!pipelines->mllw) {
    return {};   // ProjPipelines dtor releases the context
  }

  if (!mhhw_grids.empty()) {
    pipelines->mhhw =
      create_pipeline(pipelines->context, config.geoid_grid, mhhw_grids, diag);
    if (!pipelines->mhhw) {
      report(diag, "VDatum setup: MHHW pipeline failed — mhhw_z will be absent");
    }
  } else {
    report(
      diag, "VDatum setup: no *_mhhw.gtx files found — mhhw_z will be absent");
  }

  return [pipelines](double lat, double lon) -> std::optional<VDatumResult> {
      const double lon_rad = proj_torad(lon);
      const double lat_rad = proj_torad(lat);

      double mllw_z;
      if (!query_datum(pipelines->mllw, lon_rad, lat_rad, mllw_z)) {
        return std::nullopt;   // normal gap: no MLLW coverage here
      }

      VDatumResult result;
      result.mllw_z = mllw_z;
      if (pipelines->mhhw) {
        double mhhw_z;
        if (query_datum(pipelines->mhhw, lon_rad, lat_rad, mhhw_z)) {
          result.mhhw_z = mhhw_z;
        }
      }
      return result;
    };
}

}  // namespace marine_vertical_datum
