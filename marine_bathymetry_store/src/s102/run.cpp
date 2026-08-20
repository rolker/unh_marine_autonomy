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

#include "s102/run.hpp"

#include <cpl_conv.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/geotiff_import.hpp"
#include "marine_bathymetry_store/registry.hpp"
#include "marine_bathymetry_store/tile_io.hpp"
#include "s102/fetch.hpp"

namespace fs = std::filesystem;

namespace marine_bathymetry_store::s102
{

namespace
{

constexpr const char * kImportedSidecar = "s102_imported.json";

nlohmann::json loadImportedSidecar(const fs::path & store_dir)
{
  const fs::path path = store_dir / kImportedSidecar;
  if (!fs::exists(path)) {
    return nlohmann::json::object();
  }
  std::ifstream in(path);
  nlohmann::json sidecar;
  try {
    in >> sidecar;
  } catch (const nlohmann::json::exception & e) {
    throw std::runtime_error(
            "corrupt " + path.string() + ": " + e.what() +
            " (delete it to force a full re-import)");
  }
  return sidecar.is_object() ? sidecar : nlohmann::json::object();
}

void saveImportedSidecarAtomic(
  const fs::path & store_dir, const nlohmann::json & sidecar)
{
  const fs::path path = store_dir / kImportedSidecar;
  const fs::path tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp);
    out << sidecar.dump(2) << "\n";
    // Close explicitly and re-check: the stream defers its final flush to
    // close, and a disk-full there sets failbit only then. Catch it before
    // rename so we never promote a truncated sidecar over the good one.
    out.close();
    if (!out) {
      throw std::runtime_error("cannot write " + tmp.string());
    }
  }
  fs::rename(tmp, path);
}

}  // namespace

S102ImportSummary runS102Import(const S102ImportOptions & options)
{
  if (options.datum == nullptr) {
    throw std::runtime_error("no vertical-datum provider configured");
  }
  if (options.store_dir.empty() || options.cache_dir.empty()) {
    throw std::runtime_error("store_dir and cache_dir are required");
  }

  TileCache cache(options.cache_dir);
  const fs::path cached_catalog = fs::path(cache.dir()) / "catalog.gpkg";

  // Resolve the catalog: explicit path/URL beats discovery; offline uses the
  // copy cached by the last online run.
  std::string catalog = options.catalog;
  // Provenance name for StoreMetadata.survey: the real catalog filename, not
  // the "catalog.gpkg" alias we cache discovered catalogs under (below). On a
  // discovered run the alias would otherwise erase which catalog was used.
  std::string catalog_name;
  if (catalog.empty()) {
    if (options.offline) {
      if (!fs::exists(cached_catalog)) {
        throw std::runtime_error(
                "offline with no cached catalog at " + cached_catalog.string() +
                " — run online once (or pass --catalog <path>)");
      }
      catalog = cached_catalog.string();
    } else {
      const std::string url = findNewestCatalog(kDefaultBucket);
      std::cout << "s102: catalog " << url << "\n";
      catalog_name = fs::path(url).filename().string();
      // Cache the catalog itself so later --offline runs can discover. The
      // S3 listing carries no digest for the catalog (tile payloads stay
      // SHA-verified); a plain streamed copy is enough here.
      const std::string vsi = "/vsicurl/" + url;
      if (CPLCopyFile(cached_catalog.string().c_str(), vsi.c_str()) == 0) {
        catalog = cached_catalog.string();
      } else {
        // Copy failed (transient network?) — query the remote directly.
        catalog = vsi;
      }
    }
  }
  // Explicit --catalog and offline (cached alias) both fall back to the
  // resolved path's filename; only discovery has a better name (set above).
  if (catalog_name.empty()) {
    catalog_name = fs::path(catalog).filename().string();
  }

  S102ImportSummary summary;
  const auto records = queryCatalog(catalog, options.area);
  summary.discovered = records.size();
  if (records.empty()) {
    std::cout << "s102: no tiles intersect the area\n";
    return summary;
  }

  nlohmann::json imported = loadImportedSidecar(options.store_dir);

  // Reference is the read-only prior; the importer is its one sanctioned
  // writer (same gate as import_geotiff_main.cpp).
  const bool reference_writable = (options.layer == SourceLayer::Reference);
  auto store = BathymetryStore::fromCellSize(
    static_cast<float>(options.cell_size), reference_writable);
  StoreMetadata existing_metadata;
  fs::create_directories(options.store_dir);
  const std::size_t loaded = load(store, options.store_dir, &existing_metadata);
  std::cout << "s102: loaded " << loaded << " existing tile(s) from " <<
    options.store_dir << "\n";

  std::string newest_issuance;
  bool any_imported = false;
  for (const auto & record : records) {
    if (!options.force && imported.contains(record.tile_id) &&
      imported[record.tile_id] == record.issuance)
    {
      ++summary.skipped;
      continue;
    }

    const auto fetched = cache.fetch(record, options.offline);
    if (fetched.downloaded) {
      ++summary.fetched;
    }

    fs::create_directories(fs::path(cache.dir()) / "converted");
    const fs::path converted =
      fs::path(cache.dir()) / "converted" / (record.tile_id + ".tif");
    std::cout << "s102: converting " << record.tile_id << " (" <<
      record.resolution_m << "m, issuance " << record.issuance << ")\n";
    convertTile(fetched.local_path, converted.string(), *options.datum);
    ++summary.converted;

    GeoTiffImportOptions import_options;
    import_options.level =
      gggs::Level::fromCellSize(static_cast<float>(record.resolution_m)).level();
    const std::size_t cells =
      importGeoTiff(store, options.layer, converted.string(), import_options)
      .cells_imported;
    std::cout << "s102: imported " << cells << " cell(s) from " <<
      record.tile_id << " at level " <<
      static_cast<int>(*import_options.level) << "\n";
    summary.imported_cells += cells;
    imported[record.tile_id] = record.issuance;
    any_imported = true;
    newest_issuance = std::max(newest_issuance, record.issuance);
  }

  if (any_imported) {
    // Coarse ADR-0005 provenance, written only when the store has none —
    // an S-102 import must not clobber an existing survey's metadata.
    const StoreMetadata * to_write = nullptr;
    StoreMetadata metadata;
    if (existing_metadata.empty()) {
      metadata.sensor = "NOAA S-102 ed3.0.0";
      metadata.survey = catalog_name;
      metadata.date = newest_issuance;
      to_write = &metadata;
    } else {
      to_write = &existing_metadata;
    }
    const std::size_t saved = save(store, options.store_dir, to_write);
    std::cout << "s102: saved " << saved << " tile(s) under " <<
      options.store_dir << "\n";
    saveImportedSidecarAtomic(options.store_dir, imported);
  } else {
    std::cout << "s102: nothing to do (all " << summary.skipped <<
      " tile(s) already imported at current issuance)\n";
  }
  return summary;
}

}  // namespace marine_bathymetry_store::s102
