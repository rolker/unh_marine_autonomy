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

#ifndef S102__CATALOG_HPP_
#define S102__CATALOG_HPP_

#include <optional>
#include <string>
#include <vector>

/// @file
/// @brief S-102 discover stage (#278): locate the newest NOAA tile-scheme
/// catalog and query it for tiles intersecting an area.
///
/// The NOAA `noaa-s102-pds` bucket publishes a machine-readable GeoPackage
/// catalog under `ed3.0.0/_CATALOG/` (regenerated frequently, timestamped
/// name). Each feature is one tile polygon with columns
/// `TILE_ID/REGION/SUBREGION/ISSUANCE/S102V30/S102V30_SHA256/Resolution/UTM`.
/// Downloads must key off the catalog's `S102V30` URL, never off path
/// conventions (one upstream tile's SUBREGION is misspelled "Deleware_Bay").

namespace marine_bathymetry_store::s102
{

/// @brief One catalog row: everything needed to fetch + place a tile.
struct TileRecord
{
  std::string tile_id;       ///< e.g. "US5NH1AG"
  std::string url;           ///< download URL (catalog `S102V30`)
  std::string sha256;        ///< lowercase hex digest (catalog `S102V30_SHA256`)
  std::string issuance;      ///< catalog `ISSUANCE` timestamp string
  double resolution_m = 0.;  ///< parsed from catalog `Resolution` ("4m" → 4)
};

/// @brief Geographic query area, WGS84 degrees.
struct BBox
{
  double min_lon = 0., min_lat = 0., max_lon = 0., max_lat = 0.;
};

/// @brief Parse a catalog `Resolution` string ("4m", "16m") to metres.
/// @return nullopt on anything that doesn't parse as `<positive number>m`.
std::optional<double> parseResolutionMeters(const std::string & text);

/// @brief Find the newest tile-scheme catalog in the bucket.
///
/// Lists `<bucket_url>?list-type=2&prefix=<prefix>` (S3 ListObjectsV2, no
/// auth) via CPLHTTPFetch and returns the full URL of the lexicographically
/// greatest `Navigation_Tile_Scheme_*.gpkg` key — the scheme's names embed a
/// `YYYYMMDD_HHMMSS` timestamp, so lexical order is temporal order.
/// @throws std::runtime_error on HTTP failure, XML parse failure, or an
/// empty listing.
std::string findNewestCatalog(
  const std::string & bucket_url,
  const std::string & prefix = "ed3.0.0/_CATALOG/");

/// @brief Query @p catalog_path for tiles intersecting @p area.
///
/// @p catalog_path is any GDAL-openable vector path: a local `.gpkg` (the
/// test seam and the offline cache) or `/vsicurl/https://…` for remote.
/// Rows whose `Resolution` doesn't parse or whose URL/SHA256 is empty are
/// skipped with a warning on stderr (never imported on faith).
/// @throws std::runtime_error if the catalog can't be opened or has no layer.
std::vector<TileRecord> queryCatalog(
  const std::string & catalog_path, const BBox & area);

}  // namespace marine_bathymetry_store::s102

#endif  // S102__CATALOG_HPP_
