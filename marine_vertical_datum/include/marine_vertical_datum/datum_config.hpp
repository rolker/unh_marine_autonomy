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

// datum_config.hpp — Pure, ROS-free vertical-datum resolution (ADR-0010 D6).
//
// Consumers (chart importers, CAMP display, chart_datum_node) resolve the
// vertical datum at a point from several sources. This header holds the
// source-agnostic core so the full precedence chain is unit-testable without
// rclcpp or PROJ:
//
//   1. lake_datum param (if set)            — wins outright everywhere
//   2. config entries with override == true — beat VDatum
//   3. VDatum (PROJ) result                 — where grids cover the point
//   4. config entries with override == false (default) — fill VDatum gaps
//   5. nothing matches                      — caller leaves chart_datum absent
//
// Within a pass, the first matching entry (file order) wins.

#ifndef MARINE_VERTICAL_DATUM__DATUM_CONFIG_HPP_
#define MARINE_VERTICAL_DATUM__DATUM_CONFIG_HPP_

#include <optional>
#include <string>
#include <vector>

namespace marine_vertical_datum
{

enum class DatumSource
{
  VDATUM,
  POLYGON_CONFIG,
  PARAM,
};

// A geographic point in degrees.
struct LatLon
{
  double lat;
  double lon;
};

// One polygon→datum entry from the config file. Heights are signed metres in
// the same convention as the published map→chart_datum transform: the datum's
// height relative to the WGS84 ellipsoid (negative when below the ellipsoid).
struct DatumEntry
{
  std::string name;
  std::vector<LatLon> ring;          // polygon vertices, degrees (>= 3)
  double chart_datum_z = 0.0;        // chart datum height rel. ellipsoid (m)
  std::optional<double> mhhw_z;      // optional MHHW height rel. ellipsoid (m)
  bool override_vdatum = false;      // true → consulted before VDatum
};

// Result of a VDatum/PROJ query at a point (caller passes nullopt for no
// coverage / VDatum disabled).
struct VDatumResult
{
  double mllw_z;                     // MLLW height rel. ellipsoid (m)
  std::optional<double> mhhw_z;      // MHHW height rel. ellipsoid (m), if any
};

// The resolved datum and where it came from.
struct DatumResult
{
  DatumSource source;
  std::string name;                  // "vdatum", the polygon name, or "param"
  double chart_datum_z;
  std::optional<double> mhhw_z;
};

// Ray-casting point-in-polygon test. `ring` is an open polygon (the closing
// edge from last→first vertex is implied). Points on an edge count as inside.
// Operates in raw lat/lon degrees: correct for local polygons; rings crossing
// the ±180° antimeridian are NOT supported.
bool point_in_ring(double lat, double lon, const std::vector<LatLon> & ring);

// Parse a polygon→datum config file (YAML). Throws std::runtime_error on a
// missing file or malformed content.
std::vector<DatumEntry> load_datum_config(const std::string & path);

// Resolve the datum at a point following the precedence chain documented above.
// Returns nullopt when nothing matches (caller leaves chart_datum absent).
std::optional<DatumResult> resolve_datum(
  double lat, double lon,
  std::optional<double> lake_datum,
  std::optional<double> lake_datum_mhhw,
  const std::optional<VDatumResult> & vdatum,
  const std::vector<DatumEntry> & entries);

}  // namespace marine_vertical_datum

#endif  // MARINE_VERTICAL_DATUM__DATUM_CONFIG_HPP_
