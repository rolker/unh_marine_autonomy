// Copyright 2026 Roland Arsenault
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "marine_contacts/contact_store.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "marine_interfaces/msg/contact_array.hpp"
#include "rclcpp/serialization.hpp"
#include "rclcpp/serialized_message.hpp"

namespace marine_contacts
{

marine_interfaces::msg::Contact make_box_contact(
  const std::vector<MapPoint> & points, const std::string & id,
  const std::string & source, const std::string & frame, double stamp_s)
{
  marine_interfaces::msg::Contact c;
  c.header.frame_id = frame;
  const double t = std::max(0.0, stamp_s);   // guard negative -> nanosec wraparound
  c.header.stamp.sec = static_cast<int32_t>(t);
  c.header.stamp.nanosec =
    static_cast<uint32_t>((t - static_cast<double>(c.header.stamp.sec)) * 1e9);
  c.id = id;
  c.source = source;
  c.existence_probability = 1.0f;            // human-drawn
  c.origin_kind = marine_interfaces::msg::Contact::ORIGIN_HUMAN;
  c.status = marine_interfaces::msg::Contact::STATUS_PROPOSED;

  // BOX shape + centroid pose from the drawn footprint.
  c.shape.type = marine_interfaces::msg::Shape::BOX;
  c.kinematics.orientation_availability =
    marine_interfaces::msg::Kinematics::ORIENTATION_UNAVAILABLE;
  c.kinematics.pose.pose.orientation.w = 1.0;

  if (!points.empty()) {
    double min_x = points.front().x;
    double max_x = min_x;
    double min_y = points.front().y;
    double max_y = min_y;
    for (const auto & p : points) {
      min_x = std::min(min_x, p.x);
      max_x = std::max(max_x, p.x);
      min_y = std::min(min_y, p.y);
      max_y = std::max(max_y, p.y);
    }
    c.kinematics.pose.pose.position.x = 0.5 * (min_x + max_x);
    c.kinematics.pose.pose.position.y = 0.5 * (min_y + max_y);
    c.shape.dimensions.x = max_x - min_x;
    c.shape.dimensions.y = max_y - min_y;
    c.shape.dimensions.z = 0.0;
  }

  // geo_pose unresolved (lat/lon resolution is a follow-up): NaN latitude marks it.
  c.geo_pose.position.latitude = std::numeric_limits<double>::quiet_NaN();
  return c;
}

std::vector<const marine_interfaces::msg::Contact *> ContactStore::inBox(
  double min_x, double min_y, double max_x, double max_y) const
{
  std::vector<const marine_interfaces::msg::Contact *> out;
  for (const auto & c : contacts_) {
    const double x = c.kinematics.pose.pose.position.x;
    const double y = c.kinematics.pose.pose.position.y;
    if (x >= min_x && x <= max_x && y >= min_y && y <= max_y) {
      out.push_back(&c);
    }
  }
  return out;
}

bool ContactStore::save(const std::string & path) const
{
  marine_interfaces::msg::ContactArray arr;
  arr.contacts = contacts_;
  rclcpp::SerializedMessage serialized;
  try {
    rclcpp::Serialization<marine_interfaces::msg::ContactArray>().serialize_message(
      &arr, &serialized);
  } catch (const std::exception &) {
    return false;
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {return false;}
  const auto & rcl = serialized.get_rcl_serialized_message();
  out.write(reinterpret_cast<const char *>(rcl.buffer),
    static_cast<std::streamsize>(rcl.buffer_length));
  return out.good();
}

bool ContactStore::load(const std::string & path)
{
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {return false;}
  const std::streamsize n = in.tellg();
  if (n < 0) {return false;}
  in.seekg(0);
  rclcpp::SerializedMessage serialized(static_cast<size_t>(n));
  auto & rcl = serialized.get_rcl_serialized_message();
  if (n > 0 && !in.read(reinterpret_cast<char *>(rcl.buffer), n)) {return false;}
  rcl.buffer_length = static_cast<size_t>(n);

  marine_interfaces::msg::ContactArray arr;
  try {
    rclcpp::Serialization<marine_interfaces::msg::ContactArray>().deserialize_message(
      &serialized, &arr);
  } catch (const std::exception &) {
    return false;
  }
  contacts_ = arr.contacts;
  return true;
}

namespace
{
// Escape a string for a JSON string literal (RFC 8259): quotes, backslash, controls.
std::string json_escape(const std::string & s)
{
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char u[8];
          std::snprintf(u, sizeof(u), "\\u%04x", static_cast<unsigned char>(c));
          out += u;
        } else {
          out += c;
        }
    }
  }
  return out;
}
}  // namespace

GeoJsonExportResult export_contacts_geojson(
  const std::vector<marine_interfaces::msg::Contact> & contacts, const std::string & path)
{
  GeoJsonExportResult result;
  std::ofstream f(path);
  if (!f) {return result;}   // ok stays false

  f << "{\n  \"type\": \"FeatureCollection\",\n  \"features\": [";
  bool first = true;
  for (const auto & c : contacts) {
    const double lat = c.geo_pose.position.latitude;
    const double lon = c.geo_pose.position.longitude;
    if (!std::isfinite(lat) || !std::isfinite(lon)) {
      ++result.skipped;   // no resolved geo reference -> not exportable as a point
      continue;
    }
    // Sanitize non-finite numeric properties to 0 — `export` takes arbitrary
    // Contacts (e.g. loaded from a .cdr), and a bare nan/inf token is invalid JSON.
    // Buffers are sized for the worst-case finite double (%f ~ 320 chars) so a
    // mis-resolved huge value can't truncate mid-number into malformed JSON.
    const auto fin = [](double v) {return std::isfinite(v) ? v : 0.0;};
    const double stamp = fin(
      static_cast<double>(c.header.stamp.sec) +
      static_cast<double>(c.header.stamp.nanosec) * 1e-9);
    char coords[768];
    std::snprintf(coords, sizeof(coords), "[%.8f, %.8f]", lon, lat);
    char props[1024];
    std::snprintf(
      props, sizeof(props), "\"width_m\": %.3f, \"height_m\": %.3f, \"stamp\": %.3f",
      fin(c.shape.dimensions.x), fin(c.shape.dimensions.y), stamp);
    f << (first ? "" : ",")
      << "\n    {\"type\": \"Feature\", \"geometry\": {\"type\": \"Point\", "
      << "\"coordinates\": " << coords << "}, \"properties\": {\"id\": \""
      << json_escape(c.id) << "\", " << props << "}}";
    first = false;
    ++result.written;
  }
  f << "\n  ]\n}\n";
  result.ok = f.good();
  return result;
}

}  // namespace marine_contacts
