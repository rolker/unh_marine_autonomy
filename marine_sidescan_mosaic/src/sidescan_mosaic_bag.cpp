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
/// @brief Offline sidescan bag importer → Tier-1 (ADR-0006 D2/D3, issue #184).
///
/// Reads a bag **directly** (rosbag2, not replay): pass 1 fills a `tf2::BufferCore`
/// from `/tf` + `/tf_static`; pass 2 walks the port/starboard `RawSonarImage`
/// channels (+ the nadir `Range`), resolves the **baked `earth`→transducer pose**
/// at each ping time, decodes the backscatter, and writes one Tier-1 record per
/// ping. No bottom model is applied — that is Tier-2's job.
///
/// NOTE (issue #184): the decode/stamp helpers are replicated from `mosaic_node`
/// for the prototype; the shared per-ping engine extraction (ADR-0006 D12) is a
/// follow-up after #177 (PR #181) merges, to avoid touching the node in parallel.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "builtin_interfaces/msg/time.hpp"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "marine_acoustic_msgs/msg/raw_sonar_image.hpp"
#include "rclcpp/serialization.hpp"
#include "rclcpp/serialized_message.hpp"
#include "rosbag2_cpp/reader.hpp"
#include "rosbag2_storage/storage_filter.hpp"
#include "rosbag2_storage/storage_options.hpp"
#include "sensor_msgs/msg/range.hpp"
#include "tf2/buffer_core.h"
#include "tf2/time.h"
#include "tf2_msgs/msg/tf_message.hpp"

#include "marine_sidescan_mosaic/decode.hpp"
#include "marine_sidescan_mosaic/tier1.hpp"

namespace
{
constexpr std::int64_t kNsPerS = 1000000000LL;

std::int64_t stampNs(const builtin_interfaces::msg::Time & t)
{
  return static_cast<std::int64_t>(t.sec) * kNsPerS + t.nanosec;
}

template<typename MsgT>
MsgT deserialize(const rosbag2_storage::SerializedBagMessageSharedPtr & bag_msg)
{
  rclcpp::SerializedMessage serialized(*bag_msg->serialized_data);
  MsgT out;
  rclcpp::Serialization<MsgT>().deserialize_message(&serialized, &out);
  return out;
}

std::string argValue(int argc, char ** argv, const std::string & flag, const std::string & dflt)
{
  for (int i = 1; i + 1 < argc; ++i) {
    if (flag == argv[i]) {
      return argv[i + 1];
    }
  }
  return dflt;
}
}  // namespace

int main(int argc, char ** argv)
{
  if (argc < 3) {
    std::cerr <<
      "usage: sidescan_mosaic_bag <bag_uri> <out.sst1>\n"
      "       [--port-topic T] [--stbd-topic T] [--nadir-topic T]\n"
      "       [--sound-speed S] [--nadir-staleness S] [--earth-frame F]\n"
      "       [--bins N]   # require sample0+samples_per_beam==N (decode-bug gate; 0=off)\n";
    return 2;
  }
  const std::string bag_uri = argv[1];
  const std::string out_path = argv[2];
  const std::string base = "/bizzy/sensors/sidescan/garmin_sidescan/";
  const std::string port_topic = argValue(argc, argv, "--port-topic", base + "sonar_image_port");
  const std::string stbd_topic =
    argValue(argc, argv, "--stbd-topic", base + "sonar_image_starboard");
  const std::string nadir_topic = argValue(argc, argv, "--nadir-topic", base + "nadir_depth");
  const std::string earth_frame = argValue(argc, argv, "--earth-frame", "earth");
  const double sound_speed_fallback = std::stod(argValue(argc, argv, "--sound-speed", "1500.0"));
  const double nadir_staleness_s = std::stod(argValue(argc, argv, "--nadir-staleness", "5.0"));
  const int expected_bins = std::stoi(argValue(argc, argv, "--bins", "2048"));

  // Pass 1 — fill the TF buffer from /tf + /tf_static.
  tf2::BufferCore tf_buffer(tf2::durationFromSec(36000.0));
  {
    rosbag2_cpp::Reader reader;
    rosbag2_storage::StorageOptions so;
    so.uri = bag_uri;
    reader.open(so);
    rosbag2_storage::StorageFilter filter;
    filter.topics = {"/tf", "/tf_static"};
    reader.set_filter(filter);
    std::size_t n_tf = 0;
    while (reader.has_next()) {
      auto bag_msg = reader.read_next();
      const bool is_static = bag_msg->topic_name == "/tf_static";
      auto m = deserialize<tf2_msgs::msg::TFMessage>(bag_msg);
      for (const auto & tr : m.transforms) {
        tf_buffer.setTransform(tr, "bag", is_static);
        ++n_tf;
      }
    }
    std::cerr << "pass 1: buffered " << n_tf << " transforms\n";
  }

  // Pass 2 — pings + nadir, resolve pose, decode, write Tier-1.
  std::ofstream out(out_path, std::ios::binary);
  if (!out) {
    std::cerr << "error: cannot open output " << out_path << "\n";
    return 1;
  }
  marine_sidescan_mosaic::writeTier1Header(out);

  rosbag2_cpp::Reader reader;
  rosbag2_storage::StorageOptions so;
  so.uri = bag_uri;
  reader.open(so);
  rosbag2_storage::StorageFilter filter;
  filter.topics = {port_topic, stbd_topic, nadir_topic};
  reader.set_filter(filter);

  float held_nadir = -1.0F;
  std::int64_t held_nadir_ns = 0;
  std::size_t n_written = 0, n_no_tf = 0, n_no_rate = 0, n_bad_bins = 0;

  while (reader.has_next()) {
    auto bag_msg = reader.read_next();
    const std::string & topic = bag_msg->topic_name;

    if (topic == nadir_topic) {
      auto r = deserialize<sensor_msgs::msg::Range>(bag_msg);
      if (std::isfinite(r.range) && r.range > 0.0F) {
        held_nadir = r.range;
        held_nadir_ns = stampNs(r.header.stamp);
      }
      continue;
    }

    const bool is_port = topic == port_topic;
    auto msg = deserialize<marine_acoustic_msgs::msg::RawSonarImage>(bag_msg);
    if (msg.sample_rate <= 0.0) {
      ++n_no_rate;
      continue;
    }
    // Decode-bug gate (#184): pre-fix bags mis-located sample values and show an
    // inconsistent bin count; well-formed pings have sample0+samples_per_beam == N.
    if (expected_bins > 0 &&
      static_cast<int>(msg.sample0) + static_cast<int>(msg.samples_per_beam) != expected_bins)
    {
      ++n_bad_bins;
      continue;
    }
    const std::int64_t ping_ns = stampNs(msg.header.stamp);

    geometry_msgs::msg::TransformStamped pose;
    try {
      pose = tf_buffer.lookupTransform(
        earth_frame, msg.header.frame_id,
        tf2::TimePoint(std::chrono::nanoseconds(ping_ns)));
    } catch (const tf2::TransformException &) {
      ++n_no_tf;
      continue;
    }

    marine_sidescan_mosaic::Tier1Ping p;
    p.stamp_ns = ping_ns;
    p.channel = is_port ? marine_sidescan_mosaic::Tier1Channel::Port
      : marine_sidescan_mosaic::Tier1Channel::Starboard;
    p.tx = pose.transform.translation.x;
    p.ty = pose.transform.translation.y;
    p.tz = pose.transform.translation.z;
    p.qx = pose.transform.rotation.x;
    p.qy = pose.transform.rotation.y;
    p.qz = pose.transform.rotation.z;
    p.qw = pose.transform.rotation.w;
    p.sound_speed = msg.ping_info.sound_speed > 0.0 ? msg.ping_info.sound_speed
      : sound_speed_fallback;
    p.sample_rate = msg.sample_rate;
    p.sample0 = static_cast<std::int32_t>(msg.sample0);
    const double age_s = std::abs(ping_ns - held_nadir_ns) / 1e9;
    p.nadir_altitude_m = (held_nadir > 0.0F && age_s <= nadir_staleness_s) ? held_nadir : -1.0F;
    const auto raw = marine_sidescan_mosaic::decodeSamples(msg);
    p.samples.assign(raw.begin(), raw.end());   // double -> float (lossless for GCV range).

    marine_sidescan_mosaic::writeTier1Ping(out, p);
    ++n_written;
  }

  std::cerr << "pass 2: wrote " << n_written << " Tier-1 pings to " << out_path
            << " (dropped: no-tf=" << n_no_tf << " no-rate=" << n_no_rate
            << " bad-bins=" << n_bad_bins << ")\n";
  return 0;
}
