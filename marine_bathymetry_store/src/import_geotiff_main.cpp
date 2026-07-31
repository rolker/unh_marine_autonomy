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
/// @brief CLI: import a depth/uncertainty GeoTIFF into a store layer.
///
/// usage: import_geotiff <store_dir> <layer> <geotiff>
///                       [--cell-size m] [--level N]
///                       [--uncertainty m] [--depth-scale s] [--depth-offset m]
///                       [--platform P] [--sensor S] [--survey K] [--date D]
///
/// Loads any existing tiles under <store_dir>, imports the GeoTIFF into <layer>'s
/// single fused surface (#221 — no per-day epoch), then saves. Per-cell time /
/// source provenance was dropped in #248; coarse store-level provenance
/// (StoreMetadata) may be set with the --platform/--sensor/--survey/--date flags
/// and is written to <store_dir>/registry.json.

#include <cmath>
#include <cstring>
#include <iostream>
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
    "usage: import_geotiff <store_dir> <layer> <geotiff>\n"
    "                      [--cell-size m] [--level N]\n"
    "                      [--uncertainty m] [--depth-scale s] [--depth-offset m]\n"
    "                      [--platform P] [--sensor S] [--survey K] [--date D]\n"
    "  layer:      survey | reference\n"
    "  --cell-size: store default cell size in metres (default 0.5)\n"
    "  --level:     GGGS level to import at (default: derived from --cell-size).\n"
    "               The store is multi-level; pass a level to match the source.\n"
    "  --uncertainty: ignore the file's uncertainty band and assign this\n"
    "               constant 1-sigma value (for sources without one)\n"
    "  --depth-scale / --depth-offset: vertical conversion at import,\n"
    "               height = scale*pixel + offset (defaults 1, 0). A\n"
    "               positive-down depths-below-lake-surface product imports\n"
    "               with scale -1 and offset = lake surface ellipsoidal height\n"
    "  --platform/--sensor/--survey/--date: coarse store-level provenance\n"
    "               (StoreMetadata) written to registry.json (ADR-0005 #248)\n";
  exit(1);
}

marine_bathymetry_store::SourceLayer layerFromName(const std::string & name)
{
  if (name == "survey") {
    return marine_bathymetry_store::SourceLayer::Survey;
  }
  if (name == "reference") {
    return marine_bathymetry_store::SourceLayer::Reference;
  }
  std::cerr << "unknown layer '" << name << "' (expected survey|reference)\n";
  exit(1);
}

}  // namespace

int main(int argc, char * argv[])
{
  std::string positional[3];
  int n_positional = 0;
  double cell_size = 0.5;
  int level = -1;                       // sentinel: derive from --cell-size
  double constant_uncertainty = -1.0;   // sentinel: use the file's band
  double depth_scale = 1.0;
  double depth_offset = 0.0;
  marine_bathymetry_store::StoreMetadata metadata;

  const auto need_arg = [&](int & i) -> const char * {
      if (i + 1 >= argc) {
        usage();
      }
      return argv[++i];
    };
  // std::stod/std::stoi throw on non-numeric input and there is no enclosing
  // try block in main, so an unguarded parse aborts via std::terminate.
  // Parse through these helpers instead: bad input is a clean usage error.
  // Finite-only because a NaN/inf here either hits undefined behavior
  // (--cell-size -> log2/int-cast in gggs::Level::fromCellSize) or silently
  // poisons imported depths (--depth-scale/--depth-offset) or silently
  // disables the flag (--uncertainty: NaN fails the >= 0 sentinel gate).
  const auto parse_finite = [](const char * flag, const char * val) -> double {
      double parsed = 0.0;
      try {
        parsed = std::stod(val);
      } catch (const std::exception &) {
        std::cerr << "bad " << flag << " '" << val << "'\n";
        std::exit(1);
      }
      if (!std::isfinite(parsed)) {
        std::cerr << "bad " << flag << " '" << val << "' (want a finite number)\n";
        std::exit(1);
      }
      return parsed;
    };

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--cell-size") == 0) {
      const char * val = need_arg(i);
      cell_size = parse_finite("--cell-size", val);
      // Non-positive values flow into gggs::Level::fromCellSize() where
      // std::log2(<=0) yields inf/NaN and the following static_cast<int> is
      // undefined behavior — same guard as s102_import.
      if (cell_size <= 0.0) {
        std::cerr << "bad --cell-size '" << val << "' (want a positive finite number)\n";
        return 1;
      }
    } else if (std::strcmp(argv[i], "--level") == 0) {
      const char * val = need_arg(i);
      try {
        level = std::stoi(val);
      } catch (const std::exception &) {
        std::cerr << "bad --level '" << val << "'\n";
        return 1;
      }
    } else if (std::strcmp(argv[i], "--uncertainty") == 0) {
      constant_uncertainty = parse_finite("--uncertainty", need_arg(i));
    } else if (std::strcmp(argv[i], "--depth-scale") == 0) {
      depth_scale = parse_finite("--depth-scale", need_arg(i));
    } else if (std::strcmp(argv[i], "--depth-offset") == 0) {
      depth_offset = parse_finite("--depth-offset", need_arg(i));
    } else if (std::strcmp(argv[i], "--platform") == 0) {
      metadata.platform = need_arg(i);
    } else if (std::strcmp(argv[i], "--sensor") == 0) {
      metadata.sensor = need_arg(i);
    } else if (std::strcmp(argv[i], "--survey") == 0) {
      metadata.survey = need_arg(i);
    } else if (std::strcmp(argv[i], "--date") == 0) {
      metadata.date = need_arg(i);
    } else if (n_positional < 3) {
      positional[n_positional++] = argv[i];
    } else {
      usage();
    }
  }
  if (n_positional != 3) {
    usage();
  }
  const std::string & store_dir = positional[0];
  const auto layer = layerFromName(positional[1]);
  const std::string & geotiff = positional[2];

  // The importer is the one sanctioned writer of the read-only Reference prior,
  // so it opts into reference_writable only when the target layer is
  // Reference (ADR-0002 §D3). For Survey this stays false, so a typo'd layer
  // can't touch the prior.
  const bool reference_writable =
    (layer == marine_bathymetry_store::SourceLayer::Reference);
  auto store = marine_bathymetry_store::BathymetryStore::fromCellSize(
    static_cast<float>(cell_size), reference_writable);
  std::cout << "store default level: " << static_cast<int>(store.level().level()) << "\n";

  marine_bathymetry_store::StoreMetadata existing_metadata;
  const std::size_t loaded =
    marine_bathymetry_store::load(store, store_dir, &existing_metadata);
  std::cout << "loaded " << loaded << " existing tile(s) from " << store_dir << "\n";

  marine_bathymetry_store::GeoTiffImportOptions options;
  options.depth_scale = depth_scale;
  options.depth_offset = depth_offset;
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
  const std::size_t imported = marine_bathymetry_store::importGeoTiff(
    store, layer, geotiff, options);
  std::cout << "imported " << imported << " cell(s) into " << positional[1] << "\n";

  // Write the coarse StoreMetadata only if any field was supplied; otherwise
  // preserve whatever registry.json already held (loaded above).
  const marine_bathymetry_store::StoreMetadata * to_write =
    metadata.empty() ? (existing_metadata.empty() ? nullptr : &existing_metadata) :
    &metadata;
  const std::size_t saved = marine_bathymetry_store::save(store, store_dir, to_write);
  std::cout << "saved " << saved << " tile(s) under " << store_dir << "\n";
  return 0;
}
