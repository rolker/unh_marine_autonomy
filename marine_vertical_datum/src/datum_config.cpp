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

// datum_config.cpp — Implementation of the pure datum resolution core.

#include "marine_vertical_datum/datum_config.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

#include "yaml-cpp/yaml.h"

namespace marine_vertical_datum
{

namespace
{

// Is the point collinear with and within the segment a–b? Treats an exact
// (or near-exact) on-edge point as on the segment.
bool point_on_segment(double lat, double lon, const LatLon & a, const LatLon & b)
{
  constexpr double kEps = 1e-9;
  // Cross product: zero ⇒ collinear.
  const double cross =
    (lon - a.lon) * (b.lat - a.lat) - (lat - a.lat) * (b.lon - a.lon);
  if (std::abs(cross) > kEps) {
    return false;
  }
  // Within the segment's bounding box (with tolerance).
  return lon >= std::min(a.lon, b.lon) - kEps &&
         lon <= std::max(a.lon, b.lon) + kEps &&
         lat >= std::min(a.lat, b.lat) - kEps &&
         lat <= std::max(a.lat, b.lat) + kEps;
}

}  // namespace

bool point_in_ring(double lat, double lon, const std::vector<LatLon> & ring)
{
  const size_t n = ring.size();
  if (n < 3) {
    return false;
  }

  // Boundary counts as inside.
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    if (point_on_segment(lat, lon, ring[i], ring[j])) {
      return true;
    }
  }

  // Standard ray-casting (crossing number) test.
  bool inside = false;
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const double yi = ring[i].lat, xi = ring[i].lon;
    const double yj = ring[j].lat, xj = ring[j].lon;
    const bool crosses = ((yi > lat) != (yj > lat)) &&
      (lon < (xj - xi) * (lat - yi) / (yj - yi) + xi);
    if (crosses) {
      inside = !inside;
    }
  }
  return inside;
}

std::vector<DatumEntry> load_datum_config(const std::string & path)
{
  YAML::Node root;
  try {
    root = YAML::LoadFile(path);
  } catch (const YAML::Exception & e) {
    throw std::runtime_error(
      "Failed to load datum config '" + path + "': " + e.what());
  }

  const YAML::Node polygons = root["datum_polygons"];
  if (!polygons || !polygons.IsSequence()) {
    throw std::runtime_error(
      "Datum config '" + path + "' missing a 'datum_polygons' sequence");
  }

  std::vector<DatumEntry> entries;
  // Wrap the per-entry parse so yaml-cpp type-conversion failures (e.g. a
  // non-numeric chart_datum_z or ring vertex) surface as std::runtime_error,
  // matching this function's documented contract. Our own validation throws
  // std::runtime_error directly and passes through untouched.
  try {
    for (const auto & node : polygons) {
      DatumEntry entry;
      entry.name = node["name"] ? node["name"].as<std::string>() :
        std::string("unnamed");

      if (!node["chart_datum_z"]) {
        throw std::runtime_error(
          "Datum entry '" + entry.name + "' is missing 'chart_datum_z'");
      }
      entry.chart_datum_z = node["chart_datum_z"].as<double>();

      if (node["mhhw_z"]) {
        entry.mhhw_z = node["mhhw_z"].as<double>();
      }

      entry.override_vdatum = node["override"] && node["override"].as<bool>();

      const YAML::Node ring = node["ring"];
      if (!ring || !ring.IsSequence() || ring.size() < 3) {
        throw std::runtime_error(
          "Datum entry '" + entry.name +
          "' needs a 'ring' of at least 3 [lat, lon] points");
      }
      for (const auto & pt : ring) {
        if (!pt.IsSequence() || pt.size() != 2) {
          throw std::runtime_error(
            "Datum entry '" + entry.name +
            "' has a malformed ring vertex (expected [lat, lon])");
        }
        entry.ring.push_back(LatLon{pt[0].as<double>(), pt[1].as<double>()});
      }

      entries.push_back(std::move(entry));
    }
  } catch (const YAML::Exception & e) {
    throw std::runtime_error(
      "Malformed datum config '" + path + "': " + e.what());
  }
  return entries;
}

std::optional<DatumResult> resolve_datum(
  double lat, double lon,
  std::optional<double> lake_datum,
  std::optional<double> lake_datum_mhhw,
  const std::optional<VDatumResult> & vdatum,
  const std::vector<DatumEntry> & entries)
{
  // 1. lake_datum param wins outright.
  if (lake_datum.has_value()) {
    return DatumResult{DatumSource::PARAM, "param", *lake_datum, lake_datum_mhhw};
  }

  // 2. config override entries (consulted before VDatum).
  for (const auto & e : entries) {
    if (e.override_vdatum && point_in_ring(lat, lon, e.ring)) {
      return DatumResult{
        DatumSource::POLYGON_CONFIG, e.name, e.chart_datum_z, e.mhhw_z};
    }
  }

  // 3. VDatum.
  if (vdatum.has_value()) {
    return DatumResult{
      DatumSource::VDATUM, "vdatum", vdatum->mllw_z, vdatum->mhhw_z};
  }

  // 4. config fallback entries (fill VDatum gaps).
  for (const auto & e : entries) {
    if (!e.override_vdatum && point_in_ring(lat, lon, e.ring)) {
      return DatumResult{
        DatumSource::POLYGON_CONFIG, e.name, e.chart_datum_z, e.mhhw_z};
    }
  }

  // 5. Nothing matched.
  return std::nullopt;
}

}  // namespace marine_vertical_datum
