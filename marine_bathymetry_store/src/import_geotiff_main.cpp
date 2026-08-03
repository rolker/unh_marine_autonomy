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
/// usage: import_geotiff <store_dir> <layer> <geotiff>          (survey/reference)
///        import_geotiff --stage <staged_dir> chart <geotiff>   (build chart layer)
///        import_geotiff --commit <staged_dir> <store_dir>      (atomic chart swap)
///                       [--cell-size m] [--level N]
///                       [--uncertainty m] [--depth-scale s] [--depth-offset m]
///                       [--platform P] [--sensor S] [--survey K] [--date D]
///
/// For `survey`/`reference` this loads any existing tiles under <store_dir>,
/// imports the GeoTIFF into <layer>'s single fused surface (#221 — no per-day
/// epoch), then saves.
///
/// The `chart` layer follows ADR-0010 D7's wholesale-regeneration semantics
/// (#275): it is never written into the live store incrementally. Instead
/// `--stage` builds the chart layer in a staging directory
/// (`chart_staging_writable=true`), and one `--commit` performs the atomic
/// `replaceChartLayer` swap into the live store. `--commit` is an
/// offline/maintenance operation — see its notes in usage().
///
/// Per-cell time / source provenance was dropped in #248; coarse store-level
/// provenance (StoreMetadata) may be set with the --platform/--sensor/--survey/
/// --date flags and is written to <store_dir>/registry.json.

#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/geotiff_import.hpp"
#include "marine_bathymetry_store/registry.hpp"
#include "marine_bathymetry_store/tile_io.hpp"

namespace
{

namespace fs = std::filesystem;

void usage()
{
  std::cout <<
    "usage: import_geotiff <store_dir> <layer> <geotiff>          (survey/reference)\n"
    "       import_geotiff --stage <staged_dir> chart <geotiff>   (build chart layer)\n"
    "       import_geotiff --commit <staged_dir> <store_dir>      (atomic chart swap)\n"
    "                      [--cell-size m] [--level N]\n"
    "                      [--uncertainty m] [--depth-scale s] [--depth-offset m]\n"
    "                      [--platform P] [--sensor S] [--survey K] [--date D]\n"
    "  layer:      survey | reference | chart\n"
    "  --stage <staged_dir>: build the write-gated chart layer into <staged_dir>\n"
    "               (ADR-0010 D7). Layer must be 'chart'. The live store is not\n"
    "               touched; tiles land in <staged_dir>/chart/. Point repeated\n"
    "               --stage calls at the SAME <staged_dir> to accumulate cells,\n"
    "               but START EACH regeneration cycle from a FRESH/EMPTY dir:\n"
    "               --stage appends (it never deletes), so a reused non-empty dir\n"
    "               carries stale tiles from the prior cycle into the wholesale\n"
    "               swap (a warning is printed when the dir is non-empty).\n"
    "  --commit <staged_dir> <store_dir>: atomically swap <staged_dir>/chart into\n"
    "               <store_dir> via replaceChartLayer (wholesale, never a merge).\n"
    "               OFFLINE/MAINTENANCE ONLY: run only while navigation is not\n"
    "               consuming the store (ADR-0010 D7 nav-down precondition). This\n"
    "               CLI does NOT enforce a nav-liveness check — the enforced check\n"
    "               lives in the cron updater (rolker/s57_tools#28). <staged_dir>\n"
    "               and <store_dir> must be on the SAME filesystem (the atomic\n"
    "               rename rejects a cross-device staged dir).\n"
    "  note: importing chart data does NOT activate it in the costmap; that is\n"
    "               gated on the cost-model rework (uma#276).\n"
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
  if (name == "chart") {
    return marine_bathymetry_store::SourceLayer::Chart;
  }
  std::cerr << "unknown layer '" << name << "' (expected survey|reference|chart)\n";
  exit(1);
}

/// Count `.tif` tiles already staged under <staged_dir>/chart. Used to warn that
/// `--stage` appends: a reused non-empty staged dir would carry stale tiles from
/// a prior regeneration cycle into the wholesale D7 swap (each cycle must start
/// from a fresh dir). A dir that does not exist yet counts as zero.
std::size_t countStagedChartTiles(const std::string & staged_dir)
{
  const fs::path chart_dir = fs::path(staged_dir) / "chart";
  std::error_code ec;
  if (!fs::is_directory(chart_dir, ec)) {
    return 0;
  }
  std::size_t tiles = 0;
  for (const auto & entry : fs::directory_iterator(chart_dir, ec)) {
    if (entry.path().extension() == ".tif") {
      ++tiles;
    }
  }
  return tiles;
}

}  // namespace

int main(int argc, char * argv[])
{
  std::string positional[3];
  int n_positional = 0;
  const char * stage_dir = nullptr;    // set by --stage: build chart into here
  const char * commit_dir = nullptr;   // set by --commit: staged dir to swap in
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
    if (std::strcmp(argv[i], "--stage") == 0) {
      stage_dir = need_arg(i);
    } else if (std::strcmp(argv[i], "--commit") == 0) {
      commit_dir = need_arg(i);
    } else if (std::strcmp(argv[i], "--cell-size") == 0) {
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

  // --stage and --commit are mutually exclusive modes, and neither combines with
  // a normal survey/reference import.
  if (stage_dir != nullptr && commit_dir != nullptr) {
    std::cerr << "--stage and --commit are mutually exclusive\n";
    usage();
  }

  // --commit mode: atomic wholesale swap of the staged chart layer into a store.
  // No store loading or import; the staged dir is <staged>/chart and the sole
  // positional is <store_dir>.
  if (commit_dir != nullptr) {
    if (n_positional != 1) {
      usage();
    }
    const std::string & store_dir = positional[0];
    const std::string staged_chart = (fs::path(commit_dir) / "chart").string();
    try {
      marine_bathymetry_store::replaceChartLayer(staged_chart, store_dir);
    } catch (const std::exception & e) {
      std::cerr << "commit failed: " << e.what() << "\n";
      return 1;
    }
    std::cout << "committed chart layer " << staged_chart << " -> " << store_dir << "\n";
    return 0;
  }

  // --stage mode: build the write-gated chart layer into <staged_dir>. The live
  // store is untouched; positionals are <layer> <geotiff> and layer must be
  // chart. Nothing is loaded — each cycle accumulates via the OS directory.
  if (stage_dir != nullptr) {
    if (n_positional != 2) {
      usage();
    }
    const auto layer = layerFromName(positional[0]);
    if (layer != marine_bathymetry_store::SourceLayer::Chart) {
      std::cerr << "--stage requires layer 'chart' (got '" << positional[0]
                << "'); survey/reference import directly into <store_dir>\n";
      usage();
    }
    const std::string & geotiff = positional[1];

    const std::size_t already = countStagedChartTiles(stage_dir);
    if (already > 0) {
      std::cerr << "warning: " << (fs::path(stage_dir) / "chart").string() << " already holds "
                << already << " tile(s); --stage appends. Start each wholesale "
                << "regeneration cycle from a fresh/empty staged dir (ADR-0010 D7) "
                << "or stale tiles from a prior cycle will be committed.\n";
    }

    auto store = marine_bathymetry_store::BathymetryStore::fromCellSize(
      static_cast<float>(cell_size), /*reference_writable=*/false,
      /*chart_staging_writable=*/true);
    std::cout << "store default level: " << static_cast<int>(store.level().level()) << "\n";

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
    std::cout << "imported " << imported << " cell(s) into chart\n";

    const marine_bathymetry_store::StoreMetadata * to_write =
      metadata.empty() ? nullptr : &metadata;
    const std::size_t saved = marine_bathymetry_store::save(store, stage_dir, to_write);
    std::cout << "saved " << saved << " tile(s) under " << stage_dir << "\n";
    return 0;
  }

  // Normal mode: survey/reference import into the live store.
  if (n_positional != 3) {
    usage();
  }
  const std::string & store_dir = positional[0];
  const auto layer = layerFromName(positional[1]);
  const std::string & geotiff = positional[2];
  if (layer == marine_bathymetry_store::SourceLayer::Chart) {
    std::cerr << "chart imports must use --stage/--commit (ADR-0010 D7 wholesale "
              << "regeneration); a chart layer is never written into a live store "
              << "incrementally\n";
    usage();
  }

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
