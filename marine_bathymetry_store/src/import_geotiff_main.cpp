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
/// @brief CLI: import a depth/uncertainty GeoTIFF as one epoch of a store.
///
/// usage: import_geotiff <store_dir> <layer> <epoch> <provenance> <geotiff>
///                       [--cell-size m] [--timestamp unix_seconds]
///
/// Loads any existing tiles under <store_dir>, imports the GeoTIFF as the
/// whole content of <layer>/<epoch> (wholesale, ADR-0002 §A1.2), and saves.
/// If --timestamp is omitted and <epoch> is an ISO date (YYYY-MM-DD), cells
/// are stamped midnight UTC of that date.

#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/geotiff_import.hpp"
#include "marine_bathymetry_store/tile_io.hpp"

namespace
{

void usage()
{
  std::cout <<
    "usage: import_geotiff <store_dir> <layer> <epoch> <provenance> <geotiff>\n"
    "                      [--cell-size m] [--timestamp unix_seconds]\n"
    "                      [--uncertainty m] [--depth-scale s] [--depth-offset m]\n"
    "  layer:      processed | draft | chart\n"
    "  epoch:      label, conventionally the acquisition date (YYYY-MM-DD)\n"
    "  provenance: live-fused | replayed\n"
    "  --cell-size: store cell size in metres (default 0.5; must match any\n"
    "               existing store under <store_dir>)\n"
    "  --timestamp: per-cell acquisition time; defaults to midnight UTC of an\n"
    "               ISO-date epoch label, else 0\n"
    "  --uncertainty: ignore the file's uncertainty band and assign this\n"
    "               constant 1-sigma value (for sources without one)\n"
    "  --depth-scale / --depth-offset: vertical conversion at import,\n"
    "               height = scale*pixel + offset (defaults 1, 0). A\n"
    "               positive-down depths-below-lake-surface product imports\n"
    "               with scale -1 and offset = lake surface ellipsoidal height\n";
  exit(1);
}

marine_bathymetry_store::SourceLayer layerFromName(const std::string & name)
{
  if (name == "processed") {
    return marine_bathymetry_store::SourceLayer::Processed;
  }
  if (name == "draft") {
    return marine_bathymetry_store::SourceLayer::Draft;
  }
  if (name == "chart") {
    return marine_bathymetry_store::SourceLayer::Chart;
  }
  std::cerr << "unknown layer '" << name << "' (expected processed|draft|chart)\n";
  exit(1);
}

/// Midnight UTC of an ISO date label, or 0 if the label isn't one.
double timestampFromEpochLabel(const std::string & label)
{
  std::tm tm = {};
  std::istringstream in(label);
  in >> std::get_time(&tm, "%Y-%m-%d");
  if (in.fail() || !in.eof()) {
    return 0.0;
  }
  return static_cast<double>(timegm(&tm));
}

}  // namespace

int main(int argc, char * argv[])
{
  std::string positional[5];
  int n_positional = 0;
  double cell_size = 0.5;
  double timestamp = -1.0;   // sentinel: derive from the epoch label
  double constant_uncertainty = -1.0;   // sentinel: use the file's band
  double depth_scale = 1.0;
  double depth_offset = 0.0;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--cell-size") == 0 && i + 1 < argc) {
      cell_size = std::stod(argv[++i]);
    } else if (std::strcmp(argv[i], "--timestamp") == 0 && i + 1 < argc) {
      timestamp = std::stod(argv[++i]);
    } else if (std::strcmp(argv[i], "--uncertainty") == 0 && i + 1 < argc) {
      constant_uncertainty = std::stod(argv[++i]);
    } else if (std::strcmp(argv[i], "--depth-scale") == 0 && i + 1 < argc) {
      depth_scale = std::stod(argv[++i]);
    } else if (std::strcmp(argv[i], "--depth-offset") == 0 && i + 1 < argc) {
      depth_offset = std::stod(argv[++i]);
    } else if (n_positional < 5) {
      positional[n_positional++] = argv[i];
    } else {
      usage();
    }
  }
  if (n_positional != 5) {
    usage();
  }
  const std::string & store_dir = positional[0];
  const auto layer = layerFromName(positional[1]);
  const std::string & epoch = positional[2];
  const auto provenance = marine_bathymetry_store::provenanceFromToken(positional[3]);
  const std::string & geotiff = positional[4];
  if (timestamp < 0.0) {
    timestamp = timestampFromEpochLabel(epoch);
  }

  auto store = marine_bathymetry_store::BathymetryStore::fromCellSize(
    static_cast<float>(cell_size));
  std::cout << "store level: " << static_cast<int>(store.level().level()) << "\n";

  const std::size_t loaded = marine_bathymetry_store::load(store, store_dir);
  std::cout << "loaded " << loaded << " existing tile(s) from " << store_dir << "\n";

  marine_bathymetry_store::GeoTiffImportOptions options;
  options.timestamp = timestamp;
  options.depth_scale = depth_scale;
  options.depth_offset = depth_offset;
  if (constant_uncertainty >= 0.0) {
    options.uncertainty_band = 0;
    options.default_uncertainty = constant_uncertainty;
  }
  const auto imported = marine_bathymetry_store::importGeoTiff(
    store, layer, epoch, geotiff, provenance, options);
  if (!imported) {
    std::cerr << "import refused: epoch '" << epoch << "' is already replayed and the "
      "requested provenance is live-fused (ADR-0002 A1.2 ordering)\n";
    return 2;
  }
  std::cout << "imported " << *imported << " cell(s) into " << positional[1] << "/" <<
    epoch << " (" << positional[3] << ")\n";

  const std::size_t saved = marine_bathymetry_store::save(store, store_dir);
  std::cout << "saved " << saved << " tile(s) under " << store_dir << "\n";
  return 0;
}
