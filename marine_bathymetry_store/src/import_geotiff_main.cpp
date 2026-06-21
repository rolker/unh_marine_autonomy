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
///                       [--cell-size m] [--level N] [--timestamp unix_seconds]
///                       [--uncertainty m] [--depth-scale s] [--depth-offset m]
///                       [--source-id ID --platform P --sensor S
///                        --sensor-class C --campaign K --datum D]
///
/// Loads any existing tiles under <store_dir>, imports the GeoTIFF as the
/// whole content of <layer>/<epoch> (wholesale, ADR-0002 §A1.2), registers the
/// provenance source (if --source-id given) and stamps every cell with its
/// registry index (ADR-0005 D2/D8), then saves. If --timestamp is omitted and
/// <epoch> is an ISO date (YYYY-MM-DD), cells are stamped midnight UTC.

#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/geotiff_import.hpp"
#include "marine_bathymetry_store/registry.hpp"
#include "marine_bathymetry_store/tile_io.hpp"

namespace
{

void usage()
{
  std::cout <<
    "usage: import_geotiff <store_dir> <layer> <epoch> <provenance> <geotiff>\n"
    "                      [--cell-size m] [--level N] [--timestamp unix_seconds]\n"
    "                      [--uncertainty m] [--depth-scale s] [--depth-offset m]\n"
    "                      [--source-id ID --platform P --sensor S\n"
    "                       --sensor-class C --campaign K --datum D]\n"
    "  layer:      processed | draft | chart\n"
    "  epoch:      label, conventionally the acquisition date (YYYY-MM-DD)\n"
    "  provenance: live-fused | replayed\n"
    "  --cell-size: store default cell size in metres (default 0.5)\n"
    "  --level:     GGGS level to import at (default: derived from --cell-size).\n"
    "               The store is multi-level; pass a level to match the source.\n"
    "  --timestamp: per-cell acquisition time; defaults to midnight UTC of an\n"
    "               ISO-date epoch label, else 0\n"
    "  --uncertainty: ignore the file's uncertainty band and assign this\n"
    "               constant 1-sigma value (for sources without one)\n"
    "  --depth-scale / --depth-offset: vertical conversion at import,\n"
    "               height = scale*pixel + offset (defaults 1, 0). A\n"
    "               positive-down depths-below-lake-surface product imports\n"
    "               with scale -1 and offset = lake surface ellipsoidal height\n"
    "  --source-id ... --datum: register a provenance SourceRecord (ADR-0005);\n"
    "               every imported cell is stamped with its registry index\n";
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
  int level = -1;                       // sentinel: derive from --cell-size
  double timestamp = -1.0;              // sentinel: derive from the epoch label
  double constant_uncertainty = -1.0;   // sentinel: use the file's band
  double depth_scale = 1.0;
  double depth_offset = 0.0;
  marine_bathymetry_store::SourceRecord source;

  const auto need_arg = [&](int & i) -> const char * {
      if (i + 1 >= argc) {
        usage();
      }
      return argv[++i];
    };

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--cell-size") == 0) {
      cell_size = std::stod(need_arg(i));
    } else if (std::strcmp(argv[i], "--level") == 0) {
      level = std::stoi(need_arg(i));
    } else if (std::strcmp(argv[i], "--timestamp") == 0) {
      timestamp = std::stod(need_arg(i));
    } else if (std::strcmp(argv[i], "--uncertainty") == 0) {
      constant_uncertainty = std::stod(need_arg(i));
    } else if (std::strcmp(argv[i], "--depth-scale") == 0) {
      depth_scale = std::stod(need_arg(i));
    } else if (std::strcmp(argv[i], "--depth-offset") == 0) {
      depth_offset = std::stod(need_arg(i));
    } else if (std::strcmp(argv[i], "--source-id") == 0) {
      source.source_id = need_arg(i);
    } else if (std::strcmp(argv[i], "--platform") == 0) {
      source.platform = need_arg(i);
    } else if (std::strcmp(argv[i], "--sensor") == 0) {
      source.sensor = need_arg(i);
    } else if (std::strcmp(argv[i], "--sensor-class") == 0) {
      source.sensor_class = need_arg(i);
    } else if (std::strcmp(argv[i], "--campaign") == 0) {
      source.campaign = need_arg(i);
    } else if (std::strcmp(argv[i], "--datum") == 0) {
      source.datum = need_arg(i);
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
  marine_bathymetry_store::Provenance provenance;
  try {
    provenance = marine_bathymetry_store::provenanceFromToken(positional[3]);
  } catch (const std::invalid_argument &) {
    std::cerr << "unknown provenance '" << positional[3]
              << "' (expected live-fused|replayed)\n";
    return 1;
  }
  const std::string & geotiff = positional[4];
  if (timestamp < 0.0) {
    timestamp = timestampFromEpochLabel(epoch);
  }

  // The importer is the one sanctioned writer of the read-only Chart prior, so
  // it opts into chart_writable only when the target layer is Chart (ADR-0002
  // §D3). For Processed/Draft this stays false, so a typo'd layer can't touch
  // Chart.
  const bool chart_writable = (layer == marine_bathymetry_store::SourceLayer::Chart);
  auto store = marine_bathymetry_store::BathymetryStore::fromCellSize(
    static_cast<float>(cell_size), chart_writable);
  std::cout << "store default level: " << static_cast<int>(store.level().level()) << "\n";

  marine_bathymetry_store::SourceRegistry registry;
  const std::size_t loaded = marine_bathymetry_store::load(store, store_dir, &registry);
  std::cout << "loaded " << loaded << " existing tile(s) from " << store_dir << "\n";

  marine_bathymetry_store::GeoTiffImportOptions options;
  options.timestamp = timestamp;
  options.depth_scale = depth_scale;
  options.depth_offset = depth_offset;
  options.source = source;
  if (level >= 0) {
    if (level > 20) {
      std::cerr << "invalid --level " << level << " (GGGS levels are 0..20)\n";
      return 1;
    }
    options.level = static_cast<uint8_t>(level);
  }
  if (constant_uncertainty >= 0.0) {
    options.uncertainty_band = 0;
    options.default_uncertainty = constant_uncertainty;
  }
  const auto imported = marine_bathymetry_store::importGeoTiff(
    store, registry, layer, epoch, geotiff, provenance, options);
  if (!imported) {
    std::cerr << "import refused: epoch '" << epoch << "' is already replayed and the "
      "requested provenance is live-fused (ADR-0002 A1.2 ordering)\n";
    return 2;
  }
  std::cout << "imported " << *imported << " cell(s) into " << positional[1] << "/" <<
    epoch << " (" << positional[3] << ")\n";

  const std::size_t saved = marine_bathymetry_store::save(store, store_dir, &registry);
  std::cout << "saved " << saved << " tile(s) under " << store_dir << "\n";
  return 0;
}
