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

#include "s102/catalog.hpp"

#include <cpl_http.h>
#include <cpl_minixml.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <cctype>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace marine_bathymetry_store::s102
{

std::optional<double> parseResolutionMeters(const std::string & text)
{
  if (text.empty()) {
    return std::nullopt;
  }
  std::size_t pos = 0;
  double value = 0.;
  try {
    value = std::stod(text, &pos);
  } catch (const std::exception &) {
    return std::nullopt;
  }
  // Exactly `<number>m`, nothing trailing; resolutions are strictly positive.
  if (pos + 1 != text.size() || text[pos] != 'm' || !(value > 0.)) {
    return std::nullopt;
  }
  return value;
}

std::string findNewestCatalog(
  const std::string & bucket_url, const std::string & prefix)
{
  const std::string list_url =
    bucket_url + "?list-type=2&prefix=" + prefix;
  CPLHTTPResult * result = CPLHTTPFetch(list_url.c_str(), nullptr);
  if (result == nullptr || result->nStatus != 0 || result->pabyData == nullptr) {
    const std::string err =
      (result != nullptr && result->pszErrBuf != nullptr) ? result->pszErrBuf : "no data";
    CPLHTTPDestroyResult(result);
    throw std::runtime_error("S3 listing failed for " + list_url + ": " + err);
  }
  const std::string xml(reinterpret_cast<const char *>(result->pabyData),
    result->nDataLen);
  CPLHTTPDestroyResult(result);

  CPLXMLNode * root = CPLParseXMLString(xml.c_str());
  if (root == nullptr) {
    throw std::runtime_error("unparseable S3 listing from " + list_url);
  }
  const std::unique_ptr<CPLXMLNode, decltype(& CPLDestroyXMLNode)> root_guard(
    root, &CPLDestroyXMLNode);

  std::string newest;
  const CPLXMLNode * list = CPLGetXMLNode(root, "=ListBucketResult");
  for (const CPLXMLNode * node = (list != nullptr) ? list->psChild : nullptr;
    node != nullptr; node = node->psNext)
  {
    if (node->eType != CXT_Element || std::string(node->pszValue) != "Contents") {
      continue;
    }
    const std::string key = CPLGetXMLValue(node, "Key", "");
    // The tile-scheme names embed YYYYMMDD_HHMMSS, so the lexicographically
    // greatest matching key is the newest catalog.
    if (key.find("Navigation_Tile_Scheme_") != std::string::npos &&
      key.size() >= 5 && key.substr(key.size() - 5) == ".gpkg" && key > newest)
    {
      newest = key;
    }
  }
  if (newest.empty()) {
    throw std::runtime_error(
      "no Navigation_Tile_Scheme_*.gpkg under " + bucket_url + "/" + prefix);
  }
  return bucket_url + "/" + newest;
}

std::vector<TileRecord> queryCatalog(
  const std::string & catalog_path, const BBox & area)
{
  GDALAllRegister();
  const auto dataset = std::unique_ptr<GDALDataset>(GDALDataset::Open(
      catalog_path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY));
  if (dataset == nullptr) {
    throw std::runtime_error("cannot open catalog " + catalog_path);
  }
  if (dataset->GetLayerCount() < 1) {
    throw std::runtime_error("catalog has no layers: " + catalog_path);
  }
  OGRLayer * layer = dataset->GetLayer(0);
  layer->SetSpatialFilterRect(
    area.min_lon, area.min_lat, area.max_lon, area.max_lat);

  std::vector<TileRecord> records;
  layer->ResetReading();
  for (auto & feature : *layer) {
    TileRecord rec;
    rec.tile_id = feature->GetFieldAsString("TILE_ID");
    rec.url = feature->GetFieldAsString("S102V30");
    rec.sha256 = feature->GetFieldAsString("S102V30_SHA256");
    rec.issuance = feature->GetFieldAsString("ISSUANCE");
    const std::string resolution = feature->GetFieldAsString("Resolution");
    const auto parsed = parseResolutionMeters(resolution);
    if (rec.url.empty() || rec.sha256.empty() || !parsed.has_value()) {
      // Never fetch or import a row we can't verify or place.
      std::cerr << "s102: skipping catalog row '" << rec.tile_id <<
        "' (url/sha256 missing or unparseable Resolution '" << resolution <<
        "')\n";
      continue;
    }
    // Digests are compared case-insensitively downstream; normalize here.
    for (auto & c : rec.sha256) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    rec.resolution_m = *parsed;
    records.push_back(std::move(rec));
  }
  return records;
}

}  // namespace marine_bathymetry_store::s102
