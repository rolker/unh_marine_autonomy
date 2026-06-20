// Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
// Hydrographic Center, University of New Hampshire
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

/// @file
/// @brief Live sidescan mosaicker node (#173 P1+P2).
///
/// Subscribes the Garmin GCV port/stbd RawSonarImage channels + the nadir Range,
/// georeferences each sample (origin from the earth→sensor TF; slant→ground using
/// the held nadir altitude; across-track placement via geodesy), normalizes the
/// ping, splats into GGGS-tiled uint16 tiles, and flushes dirty tiles to GeoTIFF
/// on a timer.
///
/// Frame convention (validate against real data, #173 slice 5): the across-track
/// azimuth is `heading ± across_track_offset_deg`, where heading is the sensor
/// frame +x azimuth — i.e. this assumes the per-channel `frame_id` +x is the
/// vessel-forward / along-track direction, with the look direction (port/stbd)
/// supplied by the sign. If a platform's URDF orients the sensor frame
/// differently, adjust `across_track_offset_deg`.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "geographic_msgs/msg/geo_point.hpp"
#include "marine_acoustic_msgs/msg/raw_sonar_image.hpp"
#include "marine_autonomy/gggs.h"
#include "marine_tiled_raster_store/tile_io.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/range.hpp"
#include "tf2_ros/buffer.h"
#include "tf2_ros/transform_listener.h"

#include "marine_sidescan_mosaic/accumulator.hpp"
#include "marine_sidescan_mosaic/normalizer.hpp"
#include "marine_sidescan_mosaic/projection.hpp"

namespace marine_sidescan_mosaic
{

namespace
{
constexpr int64_t kNsPerS = 1000000000LL;

// Decode a RawSonarImage payload (single beam) to a 1-D double array, honouring
// the declared dtype + endianness. Handles the types the GCV emits (uint8,
// uint16) plus a few common others; unknown dtypes fall back to uint8.
template<typename T>
std::vector<double> decodeAs(const std::vector<uint8_t> & data, bool big_endian)
{
  const std::size_t n = data.size() / sizeof(T);
  std::vector<double> out(n);
  for (std::size_t i = 0; i < n; ++i) {
    T value;
    std::memcpy(&value, data.data() + i * sizeof(T), sizeof(T));
    if (big_endian && sizeof(T) > 1) {
      auto * bytes = reinterpret_cast<uint8_t *>(&value);
      std::reverse(bytes, bytes + sizeof(T));
    }
    out[i] = static_cast<double>(value);
  }
  return out;
}

std::vector<double> decodeSamples(const marine_acoustic_msgs::msg::RawSonarImage & msg)
{
  using SonarImageData = marine_acoustic_msgs::msg::SonarImageData;
  const auto & data = msg.image.data;
  const bool be = msg.image.is_bigendian;
  switch (msg.image.dtype) {
    case SonarImageData::DTYPE_UINT8: return decodeAs<uint8_t>(data, be);
    case SonarImageData::DTYPE_INT8: return decodeAs<int8_t>(data, be);
    case SonarImageData::DTYPE_UINT16: return decodeAs<uint16_t>(data, be);
    case SonarImageData::DTYPE_INT16: return decodeAs<int16_t>(data, be);
    case SonarImageData::DTYPE_UINT32: return decodeAs<uint32_t>(data, be);
    case SonarImageData::DTYPE_FLOAT32: return decodeAs<float>(data, be);
    default: return decodeAs<uint8_t>(data, be);
  }
}

int64_t stampNs(const builtin_interfaces::msg::Time & t)
{
  return static_cast<int64_t>(t.sec) * kNsPerS + t.nanosec;
}
}  // namespace

class SidescanMosaicNode : public rclcpp::Node
{
public:
  SidescanMosaicNode()
  : rclcpp::Node("sidescan_mosaic"),
    level_(declare_parameter<int>("gggs_level", 13)),
    accumulator_(level_, SplatPolicy::Mean)
  {
    earth_frame_ = declare_parameter<std::string>("earth_frame", "earth");
    output_dir_ = declare_parameter<std::string>("output_dir", "sidescan_mosaic");
    sound_speed_ = declare_parameter<double>("sound_speed", 1500.0);
    across_track_offset_deg_ = declare_parameter<double>("across_track_offset_deg", 90.0);
    nadir_staleness_s_ = declare_parameter<double>("nadir_staleness_s", 5.0);
    no_nadir_policy_ = declare_parameter<std::string>("no_nadir_policy", "drop");
    max_tf_age_s_ = declare_parameter<double>("max_tf_age_s", 1.0);
    grid_warn_count_ = declare_parameter<int>("grid_warn_count", 256);
    const double flush_period_s = declare_parameter<double>("flush_period_s", 2.0);
    const std::string splat = declare_parameter<std::string>("splat", "newest");
    SplatPolicy sp = SplatPolicy::Newest;  // operator `draft`-layer default
    if (splat == "mean") {
      sp = SplatPolicy::Mean;
    } else if (splat == "max_hold") {
      sp = SplatPolicy::MaxHold;
    } else if (splat != "newest") {
      RCLCPP_WARN(
        get_logger(), "Unknown splat value '%s'; defaulting to 'newest'", splat.c_str());
    }
    accumulator_ = MosaicAccumulator(level_, sp);
    // One config (declares the norm_* params once) feeds both per-side
    // normalizers — declaring them twice would throw ParameterAlreadyDeclared.
    const NormalizerConfig ncfg = makeNormalizerConfig();
    port_norm_ = RollingNormalizer(ncfg);
    stbd_norm_ = RollingNormalizer(ncfg);

    const std::string port_topic = declare_parameter<std::string>(
      "port_topic", "sonar_image_port");
    const std::string stbd_topic = declare_parameter<std::string>(
      "starboard_topic", "sonar_image_starboard");
    const std::string nadir_topic = declare_parameter<std::string>(
      "nadir_topic", "nadir_depth");

    tf_buffer_ = std::make_unique<tf2_ros::Buffer>(get_clock());
    tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_);

    // All callbacks mutate the shared accumulator + nadir/normalizer state, so
    // pin them to one mutually-exclusive group: serialized under the default
    // single-threaded executor AND if the node is ever composed into a
    // multi-threaded one (no torn std::map iteration during a flush).
    cb_group_ = create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    rclcpp::SubscriptionOptions sub_opts;
    sub_opts.callback_group = cb_group_;

    // Imagery is best-effort on the driver; match it or no samples arrive.
    auto qos = rclcpp::SensorDataQoS();
    port_sub_ = create_subscription<marine_acoustic_msgs::msg::RawSonarImage>(
      port_topic, qos,
      [this](const marine_acoustic_msgs::msg::RawSonarImage & m) {onPing(m, Side::Port);},
      sub_opts);
    stbd_sub_ = create_subscription<marine_acoustic_msgs::msg::RawSonarImage>(
      stbd_topic, qos,
      [this](const marine_acoustic_msgs::msg::RawSonarImage & m) {onPing(m, Side::Starboard);},
      sub_opts);
    nadir_sub_ = create_subscription<sensor_msgs::msg::Range>(
      nadir_topic, qos,
      [this](const sensor_msgs::msg::Range & m) {onNadir(m);}, sub_opts);

    flush_timer_ = create_wall_timer(
      std::chrono::duration<double>(flush_period_s), [this]() {flush();}, cb_group_);

    // Fail loudly at startup if the output directory can't be created — better
    // than silently dropping the whole survey product on every flush.
    if (!ensureOutputDir()) {
      RCLCPP_ERROR(
        get_logger(), "output_dir '%s' is not writable; tile flush will fail",
        output_dir_.c_str());
    }

    RCLCPP_INFO(
      get_logger(),
      "sidescan_mosaic: level %u (~%.3f m cells), splat=%s, out=%s",
      static_cast<unsigned>(level_.level()), level_.cellSize(), splat.c_str(),
      output_dir_.c_str());
  }

private:
  NormalizerConfig makeNormalizerConfig()
  {
    NormalizerConfig c;
    c.target_level = declare_parameter<double>("norm_target_level", c.target_level);
    c.percentile = declare_parameter<double>("norm_percentile", c.percentile);
    c.ema_alpha = declare_parameter<double>("norm_ema_alpha", c.ema_alpha);
    c.min_reference = declare_parameter<double>("norm_min_reference", c.min_reference);
    return c;
  }

  // Create the output directory if needed; true if it exists and is a directory.
  bool ensureOutputDir() const
  {
    std::error_code ec;
    std::filesystem::create_directories(output_dir_, ec);
    return std::filesystem::is_directory(output_dir_, ec);
  }

  // Held nadir altitude valid for a ping at @p ping_ns, per the staleness bound;
  // std::nullopt when too stale / never received (caller applies no_nadir_policy).
  std::optional<double> altitudeFor(int64_t ping_ns) const
  {
    if (!nadir_valid_) {
      return std::nullopt;
    }
    const double age_s = std::abs(ping_ns - nadir_stamp_ns_) / 1e9;
    if (age_s > nadir_staleness_s_) {
      return std::nullopt;
    }
    return nadir_altitude_m_;
  }

  bool lookupPose(
    const std::string & frame, const builtin_interfaces::msg::Time & stamp,
    geometry_msgs::msg::Transform & out) const
  {
    try {
      out = tf_buffer_->lookupTransform(earth_frame_, frame, tf2::TimePoint(
          std::chrono::nanoseconds(stampNs(stamp)))).transform;
      return true;
    } catch (const tf2::TransformException &) {
      // Fall back to the latest available transform, bounded by max_tf_age.
    }
    try {
      const auto tf = tf_buffer_->lookupTransform(earth_frame_, frame, tf2::TimePointZero);
      const double age_s = std::abs(stampNs(stamp) - stampNs(tf.header.stamp)) / 1e9;
      if (age_s > max_tf_age_s_) {
        return false;
      }
      out = tf.transform;
      return true;
    } catch (const tf2::TransformException &) {
      return false;
    }
  }

  void onNadir(const sensor_msgs::msg::Range & msg)
  {
    if (std::isfinite(msg.range) && msg.range > 0.0) {
      nadir_altitude_m_ = msg.range;
      nadir_stamp_ns_ = stampNs(msg.header.stamp);
      nadir_valid_ = true;
    }
  }

  void onPing(const marine_acoustic_msgs::msg::RawSonarImage & msg, Side side)
  {
    if (msg.sample_rate <= 0.0) {
      return;   // can't reconstruct range without a scale
    }
    if (msg.image.beam_count > 1) {
      // The per-sample pipeline treats the payload as one across-track scan
      // line; a multi-beam image would be mis-projected. The GCV is single-beam.
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 5000,
        "beam_count %u > 1 unsupported (single-beam sidescan only); dropping ping",
        static_cast<unsigned>(msg.image.beam_count));
      return;
    }
    const int64_t ping_ns = stampNs(msg.header.stamp);

    // Altitude (slant→ground): held nadir, else apply the no-nadir policy.
    double altitude = 0.0;
    if (const auto alt = altitudeFor(ping_ns)) {
      altitude = *alt;
    } else if (no_nadir_policy_ == "assume_zero") {
      altitude = 0.0;
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 5000,
        "no fresh nadir altitude; projecting with uncorrected slant geometry");
    } else {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 5000, "no fresh nadir altitude; dropping ping");
      return;
    }

    geometry_msgs::msg::Transform pose;
    if (!lookupPose(msg.header.frame_id, msg.header.stamp, pose)) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 5000,
        "no usable %s→%s transform; dropping ping", earth_frame_.c_str(),
        msg.header.frame_id.c_str());
      return;
    }

    const auto gh = ecefPoseToGeoHeading(
      pose.translation.x, pose.translation.y, pose.translation.z,
      pose.rotation.x, pose.rotation.y, pose.rotation.z, pose.rotation.w);

    // wgs84::direct requires the origin at altitude 0 (it places on the surface).
    geographic_msgs::msg::GeoPoint origin;
    origin.latitude = gh.latitude_deg;
    origin.longitude = gh.longitude_deg;
    origin.altitude = 0.0;

    const double sound_speed = msg.ping_info.sound_speed > 0.0 ?
      msg.ping_info.sound_speed : sound_speed_;
    const double offset_rad = across_track_offset_deg_ * M_PI / 180.0;
    const double azimuth = gh.heading_rad +
      (side == Side::Starboard ? offset_rad : -offset_rad);

    const std::vector<double> raw = decodeSamples(msg);
    if (msg.samples_per_beam > 0 && raw.size() != msg.samples_per_beam) {
      // A length mismatch points at a dtype/endianness/payload error; the scan
      // line is still placed per-sample, but flag it so it doesn't smear silently.
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 5000,
        "decoded %zu samples != samples_per_beam %u; check dtype/endianness",
        raw.size(), static_cast<unsigned>(msg.samples_per_beam));
    }
    std::vector<std::uint16_t> norm;
    (side == Side::Port ? port_norm_ : stbd_norm_).normalize(raw, norm);

    const int sample0 = static_cast<int>(msg.sample0);
    for (std::size_t j = 0; j < norm.size(); ++j) {
      const double slant = slantRange(
        static_cast<int>(j), sample0, sound_speed, msg.sample_rate);
      const double ground = groundRange(slant, altitude);
      if (ground <= 0.0) {
        continue;   // inside the nadir cone; nothing to place on the ground
      }
      accumulator_.add(projectSample(origin, azimuth, ground, level_), norm[j]);
    }
  }

  void flush()
  {
    std::size_t written = 0;
    try {
      // Band no-data 0: untouched cells are transparent in GIS / CAMP.
      written = marine_tiled_raster_store::saveTiles<std::uint16_t>(
        accumulator_.tiles(), output_dir_, {std::optional<std::uint16_t>(0)});
    } catch (const std::exception & e) {
      RCLCPP_ERROR_THROTTLE(
        get_logger(), *get_clock(), 5000, "tile flush failed: %s", e.what());
      return;
    }
    if (written > 0) {
      RCLCPP_INFO(get_logger(), "flushed %zu tile(s) to %s", written, output_dir_.c_str());
    }
    // The accumulator never evicts: memory grows with covered extent. Make the
    // ceiling visible (P1 has no eviction; offload-after-flush is a P3/P4 concern).
    const std::size_t grids = accumulator_.tiles().size();
    if (grid_warn_count_ > 0 && grids > static_cast<std::size_t>(grid_warn_count_)) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 30000,
        "%zu GGGS grids held in memory (~%zu MB); grows with survey extent",
        grids, grids * 13);
    }
  }

  gggs::Level level_;
  MosaicAccumulator accumulator_;
  RollingNormalizer port_norm_;
  RollingNormalizer stbd_norm_;

  std::string earth_frame_;
  std::string output_dir_;
  std::string no_nadir_policy_;
  double sound_speed_ = 1500.0;
  double across_track_offset_deg_ = 90.0;
  double nadir_staleness_s_ = 5.0;
  double max_tf_age_s_ = 1.0;
  int grid_warn_count_ = 256;

  double nadir_altitude_m_ = 0.0;
  int64_t nadir_stamp_ns_ = 0;
  bool nadir_valid_ = false;

  std::unique_ptr<tf2_ros::Buffer> tf_buffer_;
  std::shared_ptr<tf2_ros::TransformListener> tf_listener_;
  rclcpp::CallbackGroup::SharedPtr cb_group_;
  rclcpp::Subscription<marine_acoustic_msgs::msg::RawSonarImage>::SharedPtr port_sub_;
  rclcpp::Subscription<marine_acoustic_msgs::msg::RawSonarImage>::SharedPtr stbd_sub_;
  rclcpp::Subscription<sensor_msgs::msg::Range>::SharedPtr nadir_sub_;
  rclcpp::TimerBase::SharedPtr flush_timer_;
};

}  // namespace marine_sidescan_mosaic

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<marine_sidescan_mosaic::SidescanMosaicNode>());
  rclcpp::shutdown();
  return 0;
}
