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

// [#370 Slice A] `export_geotiff` CLI: one mosaicked GeoTIFF from a box of a
// tiled raster layer. A thin shell over
// marine_tiled_raster_store::exportGeoTiff (export_geotiff.cpp), where the
// selection, refusals and writing live and are tested.

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

#include "marine_tiled_raster_store/export_geotiff.hpp"

namespace
{

namespace mtrs = marine_tiled_raster_store;

/// Default `--max-pixels`: pixels PER BAND (width x height). 250 M Float32
/// pixels is 1 GB per band uncompressed; multiply by the band count for the
/// total. The only place this number lives — the library has no default.
constexpr std::uint64_t kDefaultMaxPixelsPerBand = 250000000ULL;

void usage(std::ostream & os)
{
  os <<
    "usage: export_geotiff <layer_dir> --box LATMIN LONMIN LATMAX LONMAX -o <out.tif>\n"
    "                      [--level N] [--max-pixels N]\n"
    "  Mosaics one level of a GGGS-tiled raster layer (depth or MBES backscatter)\n"
    "  inside the box into ONE north-up EPSG:4326 Float32 GeoTIFF (tiled, DEFLATE,\n"
    "  NaN no-data, band descriptions + provenance metadata). Reads only.\n"
    "  --box LATMIN LONMIN LATMAX LONMAX   required; degrees, snapped outward to the\n"
    "                  level's cell grid; boxes reaching |lat| >= 72 are refused\n"
    "  -o, --output PATH   written as a unique PATH.tmp.XXXXXX, then renamed over PATH\n"
    "  --level N       level to export (0-20). Default: the finest level holding a\n"
    "                  native tile inside the box. Per tile: native tile, else the\n"
    "                  overviews/ sidecar tile, else a NaN hole; never upsampled\n"
    "                  or folded on the fly\n"
    "  --max-pixels N  refuse when width x height (pixels per band) exceeds N\n"
    "                  (default " << kDefaultMaxPixelsPerBand <<
    " = 1 GB of Float32 per band uncompressed;\n"
    "                  multiply by the band count for the total). Peak memory\n"
    "                  is at most about twice the uncompressed output, usually\n"
    "                  far less (see the package README)\n"
    "  Exit status: 0 written, 1 refused or failed (nothing written), 2 usage.\n";
}

bool parseDouble(const std::string & text, double & out)
{
  if (text.empty()) {
    return false;
  }
  char * end = nullptr;
  errno = 0;
  const double v = std::strtod(text.c_str(), &end);
  if (errno != 0 || end == nullptr || *end != '\0' || !std::isfinite(v)) {
    return false;
  }
  out = v;
  return true;
}

bool parseUnsigned(const std::string & text, std::uint64_t & out)
{
  if (text.empty() || text[0] == '-' || text[0] == '+') {
    return false;
  }
  char * end = nullptr;
  errno = 0;
  const unsigned long long v = std::strtoull(text.c_str(), &end, 10);  // NOLINT(runtime/int)
  if (errno != 0 || end == nullptr || *end != '\0') {
    return false;
  }
  out = static_cast<std::uint64_t>(v);
  return true;
}

int usageError(const std::string & message)
{
  std::cerr << "export_geotiff: " << message << "\n";
  usage(std::cerr);
  return 2;
}

}  // namespace

int main(int argc, char ** argv)
{
  std::optional<std::string> layer_dir;
  std::optional<std::string> output;
  std::optional<mtrs::ExportBox> box;
  mtrs::ExportOptions options;
  options.max_pixels = kDefaultMaxPixelsPerBand;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      usage(std::cout);
      return 0;
    } else if (arg == "--box") {
      if (i + 4 >= argc) {
        return usageError("--box needs four numbers: LATMIN LONMIN LATMAX LONMAX");
      }
      double v[4];
      for (int k = 0; k < 4; ++k) {
        if (!parseDouble(argv[i + 1 + k], v[k])) {
          return usageError(std::string("--box: not a number: ") + argv[i + 1 + k]);
        }
      }
      box = mtrs::ExportBox{v[0], v[1], v[2], v[3]};
      i += 4;
    } else if (arg == "-o" || arg == "--output") {
      if (i + 1 >= argc) {
        return usageError(arg + " needs a path");
      }
      output = argv[++i];
    } else if (arg == "--level") {
      std::uint64_t level = 0;
      if (i + 1 >= argc || !parseUnsigned(argv[i + 1], level) || level > 20) {
        return usageError("--level needs an integer 0-20");
      }
      options.level = static_cast<std::uint8_t>(level);
      ++i;
    } else if (arg == "--max-pixels") {
      std::uint64_t cap = 0;
      if (i + 1 >= argc || !parseUnsigned(argv[i + 1], cap) || cap == 0) {
        return usageError("--max-pixels needs a positive integer");
      }
      options.max_pixels = cap;
      ++i;
    } else if (!arg.empty() && arg[0] == '-') {
      return usageError("unknown option " + arg);
    } else if (layer_dir.has_value()) {
      return usageError("more than one <layer_dir> given");
    } else {
      layer_dir = arg;
    }
  }

  if (!layer_dir.has_value()) {
    return usageError("<layer_dir> is required");
  }
  if (!box.has_value()) {
    return usageError("--box is required");
  }
  if (!output.has_value() || output->empty()) {
    return usageError("-o <out.tif> is required");
  }
  const mtrs::ExportBox & b = *box;
  if (b.lat_min < -90.0 || b.lat_max > 90.0 || b.lon_min < -180.0 || b.lon_max > 180.0) {
    return usageError("--box: latitudes must be in [-90, 90] and longitudes in [-180, 180]");
  }
  if (!(b.lat_min < b.lat_max) || !(b.lon_min < b.lon_max)) {
    return usageError("--box: needs LATMIN < LATMAX and LONMIN < LONMAX");
  }
  options.box = b;

  try {
    const mtrs::ExportResult r = mtrs::exportGeoTiff(*layer_dir, *output, options);
    for (const std::string & w : r.warnings) {
      std::cerr << "WARNING: " << w << "\n";
    }
    const std::uint64_t total = r.width * r.height;
    std::ostringstream pct;
    pct << std::fixed << std::setprecision(1) <<
      (total == 0 ? 0.0 : 100.0 * static_cast<double>(r.nodata_count[0]) /
    static_cast<double>(total));
    std::cout <<
      "export_geotiff: wrote " << *output << "\n"
      "  level:            " << static_cast<unsigned>(r.source_level) <<
      " (" << r.source_kind << ")\n"
      "  tiles used:       " << r.tiles_native << " native, " << r.tiles_overview <<
      " overview, of " << r.tile_positions << " tile position(s) in the box\n"
      "  size:             " << r.width << " x " << r.height << " px, " << r.band_count <<
      " band(s) Float32 (downcast: " << r.downcast << ")\n"
      "  geometric error:  " << r.geometric_error_m << " m\n"
      "  NoData (band 1):  " << r.nodata_count[0] << " of " << total << " cells (" <<
      pct.str() << "%)\n"
      "  vertical datum:   " << r.vertical_datum;
    if (r.source_vertical_datum.has_value()) {
      std::cout << " (source: " << *r.source_vertical_datum << ")";
    }
    std::cout << "\n  warnings:         " << r.warnings.size() << "\n";
    return 0;
  } catch (const std::invalid_argument & e) {
    return usageError(e.what());
  } catch (const std::exception & e) {
    std::cerr << "export_geotiff: error: " << e.what() << "\n"
      "export_geotiff: nothing written\n";
    return 1;
  }
}
