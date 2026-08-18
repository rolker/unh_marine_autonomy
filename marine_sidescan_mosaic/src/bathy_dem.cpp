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

#include "marine_sidescan_mosaic/bathy_dem.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <stdexcept>

#include "marine_tiled_raster_store/tile_io.hpp"

namespace marine_sidescan_mosaic
{

namespace fs = std::filesystem;
namespace mtrs = marine_tiled_raster_store;

const char kDefaultBathyLayers[] = "survey,reference";

namespace
{

/// @brief The GGGS level encoded as the leading integer of a tile filename
///   (`<level>_<row>_<col>.tif`), or `nullopt` if the name is not one.
///
/// This is the **only** filename parse the reader performs. The full
/// filename→`GridIndex` inverse is not available here: `gggs::GridIndex`'s
/// `(level,row,column)` constructor is private and `marine_bathymetry_store`
/// keeps its derive helper in an anonymous namespace. The lookup therefore runs
/// the other way — `Level::gridIndex(lat,lon)` → `tileFilename()` → membership
/// test against the scanned name set (see `BathyDem::tileFor`).
std::optional<int> levelFromName(const std::string & name)
{
  if (name.size() < 5 || name.compare(name.size() - 4, 4, ".tif") != 0) {
    return std::nullopt;
  }
  const auto underscore = name.find('_');
  if (underscore == std::string::npos || underscore == 0) {
    return std::nullopt;
  }
  const std::string prefix = name.substr(0, underscore);
  if (prefix.find_first_not_of("0123456789") != std::string::npos) {
    return std::nullopt;
  }
  const int level = std::atoi(prefix.c_str());
  if (level < 0 || level >= static_cast<int>(gggs::levels.size())) {
    return std::nullopt;
  }
  return level;
}

}  // namespace

std::vector<std::string> splitCsv(const std::string & csv)
{
  std::vector<std::string> out;
  std::string item;
  std::istringstream stream(csv);
  while (std::getline(stream, item, ',')) {
    const auto begin = item.find_first_not_of(" \t");
    if (begin == std::string::npos) {
      continue;
    }
    const auto end = item.find_last_not_of(" \t");
    out.push_back(item.substr(begin, end - begin + 1));
  }
  return out;
}

BathyDem::BathyDem(
  const std::string & store_root,
  const std::vector<std::string> & layer_names,
  std::size_t cache_tiles)
: store_root_(store_root), cache_tiles_(std::max<std::size_t>(1, cache_tiles))
{
  if (layer_names.empty()) {
    throw std::runtime_error("BathyDem: no layer names requested (see --bathy-layers)");
  }
  if (!fs::exists(store_root_)) {
    throw std::runtime_error("BathyDem: bathy store root does not exist: " + store_root_);
  }

  std::set<int> levels_seen;
  std::vector<std::string> missing;
  std::vector<std::string> empty;
  for (const auto & name : layer_names) {
    const fs::path dir = fs::path(store_root_) / name;
    if (!fs::is_directory(dir)) {
      missing.push_back(name);
      continue;
    }
    Layer layer;
    layer.name = name;
    layer.dir = dir.string();
    std::set<int> layer_levels;
    for (const auto & entry : fs::directory_iterator(dir)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      const std::string filename = entry.path().filename().string();
      const auto level = levelFromName(filename);
      if (!level) {
        continue;   // not a value tile (registry.json, overviews sidecar, ...).
      }
      layer.files.insert(filename);
      layer_levels.insert(*level);
      levels_seen.insert(*level);
      ++tile_count_;
    }
    // Finest (highest level number) first: a fine patch wins over the coarse
    // regional surface where both cover a position.
    layer.levels.assign(layer_levels.rbegin(), layer_levels.rend());
    if (layer.files.empty()) {
      empty.push_back(name);
      continue;
    }
    layer_names_.push_back(layer.name);
    layers_.push_back(std::move(layer));
  }

  // A layer that is absent or holds no tiles while ANOTHER requested layer does
  // is not an error — reference-only coverage is a legitimate configuration —
  // but it must never be silent: after ADR-0010 D3 re-classifies `survey/` as
  // `processed/`, a run asking for `survey,reference` would otherwise quietly
  // orthorectify against the coarse regional layer alone (#297 review).
  for (const auto & name : missing) {
    warnings_.push_back(
      "requested bathy layer '" + name + "/' does not exist under " + store_root_ +
      "; continuing with the layer(s) that do. If the store re-classified its layers "
      "(ADR-0010 D3 renames survey/ to processed/), pass --bathy-layers instead of "
      "accepting the reduced coverage.");
  }
  for (const auto & name : empty) {
    warnings_.push_back(
      "requested bathy layer '" + name + "/' exists under " + store_root_ +
      " but holds no <level>_<row>_<col>.tif tiles; continuing without it.");
  }

  if (missing.size() == layer_names.size()) {
    std::ostringstream msg;
    msg << "BathyDem: none of the requested layer directories exists under " << store_root_
        << " (looked for:";
    for (const auto & name : layer_names) {
      msg << " " << name << "/";
    }
    msg << "). A bathy store that renamed its layers (ADR-0010 D3 re-classifies";
    msg << " survey/ as processed/) is a --bathy-layers change, not an empty store.";
    throw std::runtime_error(msg.str());
  }
  if (tile_count_ == 0) {
    std::ostringstream msg;
    msg << "BathyDem: no <level>_<row>_<col>.tif tiles found under " << store_root_
        << " in layer(s):";
    for (const auto & name : layer_names) {
      msg << " " << name << "/";
    }
    throw std::runtime_error(msg.str());
  }

  all_levels_.assign(levels_seen.rbegin(), levels_seen.rend());
  for (const auto & layer : layers_) {
    lookups_by_layer_[layer.name] = 0;
  }
}

std::string BathyDem::describe() const
{
  std::ostringstream out;
  out << store_root_ << " [";
  for (std::size_t i = 0; i < layers_.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    out << layers_[i].name << "/: " << layers_[i].files.size() << " tiles, level(s)";
    for (const int level : layers_[i].levels) {
      out << " " << level;
    }
  }
  out << "]";
  return out.str();
}

const BathyDem::Tile * BathyDem::tileFor(
  std::size_t layer, int level_n, const gggs::GridIndex & grid)
{
  const std::string filename = mtrs::tileFilename(grid);
  if (layers_[layer].files.count(filename) == 0) {
    return nullptr;   // no tile on disk for this grid — cheap negative test.
  }
  const CacheKey key{layer, level_n, grid.row(), grid.column()};
  const auto found = cache_index_.find(key);
  if (found != cache_index_.end()) {
    lru_.splice(lru_.begin(), lru_, found->second);
    return &lru_.begin()->second;
  }

  const std::string path = (fs::path(layers_[layer].dir) / filename).string();
  // A tile the scan listed but that cannot be read is an error, not a
  // no-coverage: silently degrading a corrupt store to flat-bottom placement is
  // exactly the stale-data path the quality standard forbids.
  Tile tile = mtrs::loadTile<double>(path, gggs::Level(static_cast<uint8_t>(level_n)), kBands);

  lru_.emplace_front(key, std::move(tile));
  cache_index_[key] = lru_.begin();
  while (lru_.size() > cache_tiles_) {
    cache_index_.erase(lru_.back().first);
    lru_.pop_back();
  }
  return &lru_.begin()->second;
}

std::optional<double> BathyDem::cellValue(
  std::size_t layer, int level_n, double lat_deg, double lon_deg)
{
  const gggs::Level level(static_cast<uint8_t>(level_n));
  gggs::GridIndex grid;
  try {
    grid = level.gridIndex(lat_deg, lon_deg);
  } catch (const std::out_of_range &) {
    return std::nullopt;   // off-ellipsoid query: no coverage, not a crash.
  }
  if (!grid.valid()) {
    return std::nullopt;
  }
  const Tile * tile = tileFor(layer, level_n, grid);
  if (tile == nullptr) {
    return std::nullopt;
  }
  const gggs::CellIndex cell(grid, gggs::geoPoint(lat_deg, lon_deg));
  const double depth = tile->get(cell.row(), cell.column(), kDepthBand);
  if (!std::isfinite(depth)) {
    return std::nullopt;   // NaN = no data (marine_bathymetry_store contract).
  }
  return depth;
}

std::optional<BathyDem::Source> BathyDem::resolveSource(double lat_deg, double lon_deg)
{
  for (std::size_t i = 0; i < layers_.size(); ++i) {
    for (const int level : layers_[i].levels) {
      if (cellValue(i, level, lat_deg, lon_deg)) {
        return Source{i, level};
      }
    }
  }
  return std::nullopt;
}

std::optional<double> BathyDem::depthAt(double lat_deg, double lon_deg)
{
  const auto source = resolveSource(lat_deg, lon_deg);
  if (!source) {
    return std::nullopt;
  }

  const gggs::Level level(static_cast<uint8_t>(source->level));
  const gggs::CellIndex cell = level.cellIndex(gggs::geoPoint(lat_deg, lon_deg));
  // CellIndex::position() is the cell's SOUTH-WEST corner, so the centre is the
  // corner plus half a cell span in each axis.
  const auto corner = cell.position();
  const double lat_span = cell.grid().latitudinalSpan() / gggs::cell_rows_per_grid;
  const double lon_span = cell.grid().longitudinalSpan() / gggs::cell_columns_per_grid;
  const double centre_lat = corner.latitude + 0.5 * lat_span;
  const double centre_lon = corner.longitude + 0.5 * lon_span;

  // The four bracketing cell centres: this cell's, plus its neighbours on the
  // side the query point falls toward. Each neighbour is resolved from its own
  // lat/lon (`cellValue` → `Level::gridIndex`), so one falling into an adjoining
  // GRID resolves there rather than clamping at the tile edge.
  const double lat_dir = (lat_deg >= centre_lat) ? 1.0 : -1.0;
  const double lon_dir = (lon_deg >= centre_lon) ? 1.0 : -1.0;
  const double t = std::min(1.0, std::abs(lat_deg - centre_lat) / lat_span);
  const double u = std::min(1.0, std::abs(lon_deg - centre_lon) / lon_span);
  const double lats[2] = {centre_lat, centre_lat + lat_dir * lat_span};
  const double lons[2] = {centre_lon, centre_lon + lon_dir * lon_span};
  const double lat_weight[2] = {1.0 - t, t};
  const double lon_weight[2] = {1.0 - u, u};

  double blended = 0.0;
  bool all_valid = true;
  double nearest_weight = -1.0;
  double nearest_value = 0.0;
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) {
      const double weight = lat_weight[i] * lon_weight[j];
      const auto value = cellValue(source->layer, source->level, lats[i], lons[j]);
      if (!value) {
        all_valid = false;
        continue;
      }
      blended += weight * *value;
      if (weight > nearest_weight) {
        nearest_weight = weight;
        nearest_value = *value;
      }
    }
  }
  if (nearest_weight < 0.0) {
    return std::nullopt;   // unreachable: resolveSource proved the centre cell valid.
  }
  ++lookups_by_layer_[layers_[source->layer].name];
  // All four present ⇒ the weights sum to 1 and `blended` is the bilinear value.
  // Otherwise fall back to the nearest valid of the four (never a partial blend,
  // whose weights would not sum to 1 and would bias the result toward zero).
  return all_valid ? blended : nearest_value;
}

}  // namespace marine_sidescan_mosaic
