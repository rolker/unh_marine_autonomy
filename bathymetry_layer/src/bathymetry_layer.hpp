// Copyright (c) 2026 Roland Arsenault
// Licensed under BSD license

#ifndef BATHYMETRY_LAYER_HPP_
#define BATHYMETRY_LAYER_HPP_

#include <memory>
#include <optional>
#include <string>

#include "geographic_msgs/msg/geo_point.hpp"
#include "geometry_msgs/msg/point_stamped.hpp"
#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "nav2_costmap_2d/layer.hpp"
#include "nav2_costmap_2d/layered_costmap.hpp"
#include "rclcpp/rclcpp.hpp"

namespace bathymetry_layer
{

/// @brief Nav2 costmap layer fed by the bathymetric store (marine_bathymetry_store).
///
/// Reads the store's read-only prior layers (`chart/`, and any `processed/`
/// present) from disk and turns *clearance* — the water-surface ellipsoidal
/// height minus the seafloor ellipsoidal height — into occupancy cost so the
/// planner routes around shoals. This is the D1 (prior-only, static) deliverable
/// of issue #164: no live `draft/` reload (deferred to D2, which depends on the
/// atomic-tile-write work in #189).
///
/// **No-data policy (ADR-0002 §D7, two-query safety pattern):** per cell,
/// `bestSource` first answers "is there ANY data here?" (quality-blind). If not,
/// the cell is truly unsurveyed and this layer leaves the master cost untouched
/// (NO_INFORMATION) so another prior — e.g. `s57_layer` — can fill it in. If
/// there *is* data, `shallowestReliable` applies the navigation-safety gate; a
/// cell that has data but fails the uncertainty gate, or whose freshest sample is
/// stale, is written `LETHAL_OBSTACLE` (conservative). Only the clearance ramp
/// produces non-lethal cost.
///
/// **Layer coexistence:** writes use max-cost semantics (a cell is only raised,
/// never lowered, and an unsurveyed cell is skipped), so this layer composes with
/// `s57_layer` in a layered costmap: the higher cost per cell wins, and where this
/// layer has no data `s57_layer` is the sole contributor. See the package README.
class BathymetryLayer : public nav2_costmap_2d::Layer
{
public:
  BathymetryLayer();
  ~BathymetryLayer() override;

  void onInitialize() override;

  void reset() override;

  bool isClearable() override {return false;}

  void updateBounds(
    double robot_x, double robot_y, double robot_yaw,
    double * min_x, double * min_y,
    double * max_x, double * max_y) override;

  void updateCosts(
    nav2_costmap_2d::Costmap2D & master_grid,
    int min_i, int min_j, int max_i, int max_j) override;

  void matchSize() override;

protected:
  // Exposed for unit testing (test/test_bathymetry_layer.cpp): the pure
  // clearance→cost ramp, decoupled from the store/TF pipeline. `clearance` is in
  // metres (water-surface height minus seafloor height; see updateCosts).
  unsigned char computeCost(double clearance) const;

  // Exposed for unit testing: the full two-query per-cell decision (review M1),
  // decoupled from the costmap/TF pipeline. Reads store_ and map_tide_z_.
  // Returns std::nullopt when the cell is truly unsurveyed (the layer leaves the
  // master cost untouched); otherwise the cost to combine into the master grid.
  // @p now_ns is "now" in nanoseconds since the Unix epoch (for the staleness gate).
  std::optional<unsigned char> evaluateCell(
    const gggs::CellIndex & cell, int64_t now_ns) const;

  // Whether @p timestamp_ns (nanoseconds since the Unix epoch) is older than
  // max_age_ relative to @p now_ns. Returns false when the gate is disabled
  // (max_age_ <= 0) or the timestamp is unset (0).
  bool isStale(int64_t timestamp_ns, int64_t now_ns) const;

  // Test seams: the store and the cached water-surface height, so a test fixture
  // can populate a synthetic store and drive cost evaluation without TF.
  std::unique_ptr<marine_bathymetry_store::BathymetryStore> store_;
  double map_tide_z_ = 0.0;

  // Parameters (protected so the test fixture can configure them directly).
  double minimum_depth_ = 1.0;
  double maximum_caution_depth_ = 2.5;
  double max_uncertainty_ = 0.5;
  double max_age_ = 0.0;

private:
  // Convert a costmap world coordinate to WGS84 lat/lon via the `earth` TF frame
  // (mirrors s57_layer::worldToLatLon).
  geographic_msgs::msg::GeoPoint worldToLatLon(double x, double y);

  // Recompute the buffered geographic window from the parent costmap bounds and
  // (re)load / evict store tiles to keep memory bounded. Returns false if the
  // store is not open or a TF lookup fails (logged, throttled).
  void refreshWindow();

  // Open the on-disk store at store_path_ with the parent costmap's resolution.
  void openStore();

  std::string store_path_;
  std::string map_tide_frame_ = "map_tide";
  double buffer_fraction_ = 0.05;

  std::string global_frame_id_;
  double resolution_ = 1.0;

  // Last buffered geographic window passed to loadWindow/evictOutside. Used to
  // skip redundant reloads when the costmap has not scrolled past the buffer.
  geographic_msgs::msg::GeoPoint window_min_;
  geographic_msgs::msg::GeoPoint window_max_;
  bool window_valid_ = false;
};

}  // namespace bathymetry_layer

#endif  // BATHYMETRY_LAYER_HPP_
