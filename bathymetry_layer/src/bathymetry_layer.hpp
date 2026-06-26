// Copyright (c) 2026 Roland Arsenault
// Licensed under BSD license

#ifndef BATHYMETRY_LAYER_HPP_
#define BATHYMETRY_LAYER_HPP_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "geographic_msgs/msg/geo_point.hpp"
#include "geometry_msgs/msg/point_stamped.hpp"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "nav2_costmap_2d/costmap_2d.hpp"
#include "nav2_costmap_2d/layer.hpp"
#include "nav2_costmap_2d/layered_costmap.hpp"
#include "rclcpp/rclcpp.hpp"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"

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
/// (NO_INFORMATION) so another prior — e.g. `s57_layer` — can fill it in. The
/// opt-in `unsurveyed_is_lethal` parameter (default false) overrides this: when
/// set, a truly-unsurveyed cell is written `LETHAL_OBSTACLE` instead. This suits a
/// closed-basin water body whose prior covers the whole navigable interior (so the
/// only no-data cells are land), letting the layer mark the shoreline lethal
/// without a separate land-mask. It is a blanket rule (every no-data cell, not just
/// "land") and, via max-cost combine, overrides other priors on those cells — so
/// choose it per deployment. It is still held behind the tide gate: no cost,
/// lethal-land included, is emitted before a valid `map_tide` arrives.
/// If there *is* data, `shallowestReliable` applies the navigation-safety gate; a
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

  // Expand a leading "~"/"~/" in @p path to $HOME (so one portable store_path
  // resolves on both the boat and dev/sim). Absolute, empty, and "~user" paths
  // are returned unchanged. Exposed for unit testing.
  static std::string expandUserPath(const std::string & path);

  // Test seams: the store and the cached water-surface height, so a test fixture
  // can populate a synthetic store and drive cost evaluation without TF.
  std::unique_ptr<marine_bathymetry_store::BathymetryStore> store_;
  double map_tide_z_ = 0.0;
  // True only after at least one successful map_tide TF lookup. When false,
  // evaluateCell() refuses to compute clearance — tide height 0.0 (the default)
  // is not a valid water-surface datum and would produce arbitrary clearance
  // values. updateCosts() also gates current_=true on this flag (MF1/MF2).
  bool map_tide_valid_ = false;

  // Parameters (protected so the test fixture can configure them directly).
  double minimum_depth_ = 1.0;
  double maximum_caution_depth_ = 2.5;
  double max_uncertainty_ = 0.5;
  double max_age_ = 0.0;
  // When true, a truly-unsurveyed (no-data) cell is written LETHAL_OBSTACLE
  // instead of being left untouched (NO_INFORMATION). Opt-in (default false):
  // a blanket rule suited to a closed basin whose prior covers the whole
  // interior, so the only no-data cells are land. Still subject to the tide gate.
  bool unsurveyed_is_lethal_ = false;

private:
  // Convert a costmap world coordinate to WGS84 lat/lon via the `earth` TF frame
  // (mirrors s57_layer::worldToLatLon). Per-cell tf buffer lookup — used only for
  // the buffered-window AABB; tile rendering uses the hoisted transform below.
  geographic_msgs::msg::GeoPoint worldToLatLon(double x, double y);

  // Apply a cached global_frame->earth transform to a world point and convert to
  // lat/lon — the hoisted-transform inner loop of generateTile(). No per-cell tf
  // buffer lookup (the costly part of worldToLatLon); the transform is looked up
  // once per generation pass and reused for every cell.
  geographic_msgs::msg::GeoPoint worldToLatLon(
    double x, double y, const geometry_msgs::msg::TransformStamped & to_earth) const;

  // Recompute the buffered geographic window from the parent costmap bounds and
  // (re)load / evict store tiles to keep memory bounded. Returns false if the
  // store is not open or a TF lookup fails (logged, throttled).
  void refreshWindow();

  // Open the on-disk store at store_path_ with the parent costmap's resolution.
  void openStore();

  // World-anchored cost-tile cache (mirrors s57_layer). The prior is static in
  // world space, so a tile is rendered once and reused as the rolling window
  // scrolls — turning per-cycle O(cells x (tf + store query)) into an O(cells)
  // blit in updateCosts. TileID is the integer tile grid coordinate; a tile
  // covers tile_size_ x tile_size_ cells of resolution_ metres, anchored at
  // (x_origin_, y_origin_) in the costmap global frame.
  typedef std::pair<int, int> TileID;
  struct TileInfo
  {
    // Pre-rendered costs for this tile (nullptr until generated). NO_INFORMATION
    // where the cell is unsurveyed/untouched; otherwise the bathymetry cost.
    std::shared_ptr<nav2_costmap_2d::Costmap2D> costmap;
    // True once the tile has been rendered against the current tide.
    bool generated = false;
    // Marked when the tide moved past the threshold; the tile keeps serving its
    // cached costs until it is regenerated, so the costmap never flickers.
    bool needs_update = false;
  };

  TileID worldToTile(double x, double y) const;
  // Render (or re-render) a tile's costs from the store using @p to_earth (the
  // hoisted global_frame->earth transform). Returns false if rendering failed
  // (e.g. a per-cell projection threw); the tile is left ungenerated.
  bool generateTile(const TileID & id, const geometry_msgs::msg::TransformStamped & to_earth);

  std::string store_path_;
  std::string map_tide_frame_ = "map_tide";
  // Ellipsoid-referenced world frame (REP-105 'map'): z=0 is the WGS84 ellipsoid.
  // The water-surface height is read as map_tide_frame's z in this frame. MUST be
  // distinct from both map_tide_frame_ and the costmap global frame (#220).
  std::string map_frame_ = "map";
  double buffer_fraction_ = 0.05;

  std::string global_frame_id_;
  double resolution_ = 1.0;

  // Cost-tile cache + its fixed world anchor (cells, metres). x_origin_/y_origin_
  // stay 0 so tile boundaries are stable in the global frame across cycles (a
  // rolling window then reuses the same tiles). tile_size_ is the tile edge in
  // cells. max_tiles_per_cycle_ bounds how many tiles are rendered per
  // updateBounds call so the first full pass over a large (e.g. 4000x4000) global
  // costmap is spread across cycles instead of blocking activation for minutes.
  std::map<TileID, TileInfo> tiles_;
  double x_origin_ = 0.0;
  double y_origin_ = 0.0;
  int tile_size_ = 100;
  int max_tiles_per_cycle_ = 8;
  // True when every tile overlapping the current window is generated and
  // up to date (no pending render work). Gates current_ (MF2) alongside the
  // tide/window flags so Nav2 sees a "not yet current" costmap while the first
  // pass is still filling tiles in, then "current" once complete.
  bool all_tiles_generated_ = false;

  // Tide-change invalidation: re-render tiles only when the water surface moves
  // more than this (metres) from the value the cache was rendered against.
  double last_tide_z_ = 0.0;
  bool tide_rendered_ = false;
  double tide_invalidate_threshold_ = 0.02;

  // Last buffered geographic window passed to loadWindow/evictOutside. Used to
  // skip redundant reloads when the costmap has not scrolled past the buffer.
  geographic_msgs::msg::GeoPoint window_min_;
  geographic_msgs::msg::GeoPoint window_max_;
  bool window_valid_ = false;

  // One-shot flag: the first loadWindow/evictOutside failure is logged at ERROR
  // level; subsequent failures from the same root cause (e.g. bad store_path_)
  // are suppressed to avoid log spam. Reset in openStore() so a path reconfigure
  // gets a fresh error if it also fails.
  bool store_path_error_logged_ = false;
};

}  // namespace bathymetry_layer

#endif  // BATHYMETRY_LAYER_HPP_
