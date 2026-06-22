// Copyright (c) 2026 Roland Arsenault
// Licensed under BSD license

#include "bathymetry_layer.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>

#include "geodesy/ecef.h"
#include "geodesy/wgs84.h"
#include "geometry_msgs/msg/point_stamped.hpp"
#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/query.hpp"
#include "marine_bathymetry_store/tile_io.hpp"
#include "nav2_costmap_2d/cost_values.hpp"

namespace bathymetry_layer
{

using marine_bathymetry_store::bestSource;
using marine_bathymetry_store::BathymetryStore;
using marine_bathymetry_store::DepthSample;
using marine_bathymetry_store::shallowestReliable;

BathymetryLayer::BathymetryLayer() = default;

BathymetryLayer::~BathymetryLayer() = default;

void BathymetryLayer::onInitialize()
{
  auto node = node_.lock();
  current_ = false;

  declareParameter("enabled", rclcpp::ParameterValue(true));
  node->get_parameter(name_ + ".enabled", enabled_);

  declareParameter("store_path", rclcpp::ParameterValue(store_path_));
  node->get_parameter(name_ + ".store_path", store_path_);

  declareParameter("minimum_depth", rclcpp::ParameterValue(minimum_depth_));
  node->get_parameter(name_ + ".minimum_depth", minimum_depth_);

  declareParameter("maximum_caution_depth", rclcpp::ParameterValue(maximum_caution_depth_));
  node->get_parameter(name_ + ".maximum_caution_depth", maximum_caution_depth_);

  // A degenerate or inverted ramp would make computeCost divide by zero or
  // produce nonsense. Parameters are external input — validate.
  if (!(std::isfinite(minimum_depth_) && std::isfinite(maximum_caution_depth_) &&
    maximum_caution_depth_ > minimum_depth_))
  {
    RCLCPP_WARN_STREAM(
      logger_,
      "Invalid depth ramp (minimum_depth="  << minimum_depth_ << ", maximum_caution_depth="
                                            << maximum_caution_depth_
                                            << "); using defaults 1.0 / 2.5 m instead.");
    minimum_depth_ = 1.0;
    maximum_caution_depth_ = 2.5;
  }

  declareParameter("max_uncertainty", rclcpp::ParameterValue(max_uncertainty_));
  node->get_parameter(name_ + ".max_uncertainty", max_uncertainty_);

  declareParameter("max_age", rclcpp::ParameterValue(max_age_));
  node->get_parameter(name_ + ".max_age", max_age_);

  declareParameter("map_tide_frame", rclcpp::ParameterValue(map_tide_frame_));
  node->get_parameter(name_ + ".map_tide_frame", map_tide_frame_);

  const double default_buffer_fraction = buffer_fraction_;
  declareParameter("buffer_fraction", rclcpp::ParameterValue(buffer_fraction_));
  node->get_parameter(name_ + ".buffer_fraction", buffer_fraction_);
  if (!std::isfinite(buffer_fraction_) || buffer_fraction_ < 0.0) {
    RCLCPP_WARN_STREAM(
      logger_,
      "Invalid buffer_fraction value " << buffer_fraction_ << "; using "
                                       << default_buffer_fraction << " instead.");
    buffer_fraction_ = default_buffer_fraction;
  }

  global_frame_id_ = layered_costmap_->getGlobalFrameID();

  if (store_path_.empty()) {
    RCLCPP_WARN_STREAM(
      logger_,
      "bathymetry_layer '" << name_ << "' has no store_path; the layer will contribute "
        "no costs until one is configured.");
  }

  RCLCPP_INFO_STREAM(
    logger_,
    "bathymetry_layer '" << name_ << "': store_path='" << store_path_ << "', minimum_depth="
                         << minimum_depth_ << " m, maximum_caution_depth=" << maximum_caution_depth_
                         << " m, max_uncertainty=" << max_uncertainty_ << " m, max_age=" << max_age_
                         << " s, map_tide_frame='" << map_tide_frame_ << "'");

  matchSize();
}

void BathymetryLayer::reset()
{
  current_ = false;
  window_valid_ = false;
}

void BathymetryLayer::openStore()
{
  if (store_path_.empty()) {
    store_.reset();
    return;
  }
  try {
    // The default level only governs cellIndex(lat,lon); the store is otherwise
    // multi-level. chart_writable=false: a navigation consumer never mutates the
    // read-only prior.
    store_ = std::make_unique<BathymetryStore>(
      BathymetryStore::fromCellSize(static_cast<float>(resolution_), false));
  } catch (const std::exception & e) {
    RCLCPP_ERROR_STREAM(
      logger_, "bathymetry_layer '" << name_ << "': failed to construct store: " << e.what());
    store_.reset();
  }
}

void BathymetryLayer::matchSize()
{
  auto parent = layered_costmap_->getCostmap();
  resolution_ = parent->getResolution();

  // Resolution may have changed; rebuild the store at the new default level and
  // force a fresh window load on the next updateBounds.
  openStore();
  window_valid_ = false;
  current_ = false;
}

geographic_msgs::msg::GeoPoint BathymetryLayer::worldToLatLon(double x, double y)
{
  geometry_msgs::msg::PointStamped world;
  world.point.x = x;
  world.point.y = y;
  world.header.frame_id = global_frame_id_;
  geometry_msgs::msg::PointStamped ecef;
  tf_->transform(world, ecef, "earth");
  geodesy::ECEFPoint ecef_point(ecef.point);
  return geodesy::toMsg(ecef_point);
}

void BathymetryLayer::refreshWindow()
{
  if (!store_) {
    return;
  }

  auto parent = layered_costmap_->getCostmap();
  const double world_min_x = parent->getOriginX();
  const double world_min_y = parent->getOriginY();
  const double world_max_x = world_min_x + parent->getSizeInMetersX();
  const double world_max_y = world_min_y + parent->getSizeInMetersY();

  // Buffer the costmap window so tiles are resident slightly ahead of the
  // rolling window edge (same convention as s57_layer's buffer_fraction_).
  const double buffer =
    std::max(world_max_x - world_min_x, world_max_y - world_min_y) * buffer_fraction_;

  geographic_msgs::msg::GeoPoint min_geo;
  geographic_msgs::msg::GeoPoint max_geo;
  try {
    // The four buffered corners; project each to lat/lon and take the AABB. The
    // global frame need not be axis-aligned with north, so corner-projection is
    // required (a two-corner box would clip the window under rotation).
    const double xs[4] = {
      world_min_x - buffer, world_max_x + buffer, world_min_x - buffer, world_max_x + buffer};
    const double ys[4] = {
      world_min_y - buffer, world_min_y - buffer, world_max_y + buffer, world_max_y + buffer};
    double lat_min = 90.0;
    double lat_max = -90.0;
    double lon_min = 180.0;
    double lon_max = -180.0;
    for (int k = 0; k < 4; ++k) {
      const auto geo = worldToLatLon(xs[k], ys[k]);
      lat_min = std::min(lat_min, geo.latitude);
      lat_max = std::max(lat_max, geo.latitude);
      lon_min = std::min(lon_min, geo.longitude);
      lon_max = std::max(lon_max, geo.longitude);
    }
    min_geo = gggs::geoPoint(lat_min, lon_min);
    max_geo = gggs::geoPoint(lat_max, lon_max);
  } catch (const tf2::TransformException & e) {
    RCLCPP_WARN_THROTTLE(
      logger_, *clock_, 10000,
      "bathymetry_layer '%s': cannot project costmap bounds to lat/lon: %s",
      name_.c_str(), e.what());
    return;
  }

  // Skip redundant reloads if the window has not changed materially.
  if (window_valid_ &&
    std::abs(min_geo.latitude - window_min_.latitude) < 1e-9 &&
    std::abs(min_geo.longitude - window_min_.longitude) < 1e-9 &&
    std::abs(max_geo.latitude - window_max_.latitude) < 1e-9 &&
    std::abs(max_geo.longitude - window_max_.longitude) < 1e-9)
  {
    return;
  }

  try {
    // Evict tiles outside the new buffered window first, then load the window.
    // Both use the SAME buffered box (review S2) so margin tiles are not
    // immediately evicted after loading.
    marine_bathymetry_store::evictOutside(*store_, min_geo, max_geo);
    marine_bathymetry_store::loadWindow(*store_, store_path_, min_geo, max_geo);
    window_min_ = min_geo;
    window_max_ = max_geo;
    window_valid_ = true;
  } catch (const std::exception & e) {
    RCLCPP_WARN_THROTTLE(
      logger_, *clock_, 10000,
      "bathymetry_layer '%s': failed to (re)load store window from '%s': %s",
      name_.c_str(), store_path_.c_str(), e.what());
  }
}

void BathymetryLayer::updateBounds(
  double, double, double, double * min_x, double * min_y, double * max_x, double * max_y)
{
  if (!enabled_) {
    return;
  }

  // Cache the water-surface ellipsoidal height from the map_tide frame's
  // z-origin (mirror s57_layer's tide lookup at TimePointZero).
  if (!map_tide_frame_.empty()) {
    try {
      auto transform =
        tf_->lookupTransform(global_frame_id_, map_tide_frame_, tf2::TimePointZero);
      map_tide_z_ = transform.transform.translation.z;
    } catch (const tf2::TransformException & e) {
      RCLCPP_WARN_THROTTLE(
        logger_, *clock_, 10000,
        "bathymetry_layer '%s': cannot look up %s in %s: %s",
        name_.c_str(), map_tide_frame_.c_str(), global_frame_id_.c_str(), e.what());
    }
  }

  refreshWindow();

  // This layer contributes data over the whole parent window; expand the update
  // bounds to the parent costmap extent so updateCosts is invoked across it.
  auto parent = layered_costmap_->getCostmap();
  const double world_min_x = parent->getOriginX();
  const double world_min_y = parent->getOriginY();
  const double world_max_x = world_min_x + parent->getSizeInMetersX();
  const double world_max_y = world_min_y + parent->getSizeInMetersY();
  *min_x = std::min(*min_x, world_min_x);
  *min_y = std::min(*min_y, world_min_y);
  *max_x = std::max(*max_x, world_max_x);
  *max_y = std::max(*max_y, world_max_y);
}

unsigned char BathymetryLayer::computeCost(double clearance) const
{
  if (!std::isfinite(clearance) || clearance < minimum_depth_) {
    return nav2_costmap_2d::LETHAL_OBSTACLE;
  }
  if (clearance >= maximum_caution_depth_) {
    return nav2_costmap_2d::FREE_SPACE;
  }
  // Linear ramp between minimum_depth_ (LETHAL boundary) and maximum_caution_depth_
  // (FREE boundary), matching s57_layer::get_cost_from_grid.
  const double scaled = nav2_costmap_2d::MAX_NON_OBSTACLE *
    (1.0 - (clearance - minimum_depth_) / (maximum_caution_depth_ - minimum_depth_));
  return static_cast<unsigned char>(scaled);
}

bool BathymetryLayer::isStale(int64_t timestamp_ns, int64_t now_ns) const
{
  if (max_age_ <= 0.0 || timestamp_ns == 0) {
    return false;
  }
  const int64_t max_age_ns = static_cast<int64_t>(max_age_ * 1e9);
  return (now_ns - timestamp_ns) > max_age_ns;
}

std::optional<unsigned char> BathymetryLayer::evaluateCell(
  const gggs::CellIndex & cell, int64_t now_ns) const
{
  if (!store_) {
    return std::nullopt;
  }

  // Two-query no-data policy (review M1, ADR-0002 §D7):
  //   1. bestSource — quality-blind "is there ANY data here?"
  const std::optional<DepthSample> any = bestSource(*store_, cell);
  if (!any) {
    // Truly unsurveyed: leave the master cost untouched (NO_INFORMATION) so
    // another prior (e.g. s57_layer) can contribute.
    return std::nullopt;
  }

  //   2. shallowestReliable — the navigation-safety gate (uncertainty).
  const std::optional<DepthSample> sample = shallowestReliable(*store_, cell, max_uncertainty_);

  if (!sample || isStale(any->timestamp, now_ns)) {
    // Data exists but every sample is over-uncertain, or the freshest data is
    // stale → conservative LETHAL (a surveyed-but-unusable cell is NOT treated
    // as unsurveyed; that mis-classification was review finding M1).
    return nav2_costmap_2d::LETHAL_OBSTACLE;
  }

  // sample->depth is ellipsoidal HEIGHT (WGS84, up-positive): a seafloor 5 m
  // below the ellipsoid has depth=-5.0, so a SHALLOWER (more hazardous) seafloor
  // has a LARGER (less-negative) depth, hence a SMALLER clearance and a HIGHER
  // cost. clearance = water-surface height − seafloor height.
  const double clearance = map_tide_z_ - sample->depth;
  return computeCost(clearance);
}

void BathymetryLayer::updateCosts(
  nav2_costmap_2d::Costmap2D & master_grid, int min_i, int min_j, int max_i, int max_j)
{
  if (!enabled_ || !store_) {
    return;
  }

  const int64_t now_ns = clock_->now().nanoseconds();

  for (int j = min_j; j < max_j; ++j) {
    for (int i = min_i; i < max_i; ++i) {
      double wx;
      double wy;
      master_grid.mapToWorld(static_cast<unsigned int>(i), static_cast<unsigned int>(j), wx, wy);

      gggs::CellIndex cell;
      try {
        const auto geo = worldToLatLon(wx, wy);
        cell = store_->cellIndex(geo.latitude, geo.longitude);
      } catch (const tf2::TransformException & e) {
        RCLCPP_WARN_THROTTLE(
          logger_, *clock_, 10000,
          "bathymetry_layer '%s': cannot project cell to lat/lon: %s", name_.c_str(), e.what());
        continue;
      }

      const std::optional<unsigned char> evaluated = evaluateCell(cell, now_ns);
      if (!evaluated) {
        // Truly unsurveyed: leave the master cost untouched (NO_INFORMATION) so
        // another prior (e.g. s57_layer) can contribute. Never WRITE NO_INFORMATION.
        continue;
      }
      const unsigned char cost = *evaluated;

      // Max-cost combine: only raise the master cost (or fill NO_INFORMATION).
      const unsigned char existing =
        master_grid.getCost(static_cast<unsigned int>(i), static_cast<unsigned int>(j));
      if (existing == nav2_costmap_2d::NO_INFORMATION || cost > existing) {
        master_grid.setCost(
          static_cast<unsigned int>(i), static_cast<unsigned int>(j), cost);
      }
    }
  }

  current_ = true;
}

}  // namespace bathymetry_layer

#include "pluginlib/class_list_macros.hpp"
PLUGINLIB_EXPORT_CLASS(bathymetry_layer::BathymetryLayer, nav2_costmap_2d::Layer)
