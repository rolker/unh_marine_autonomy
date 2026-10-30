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

#ifndef MARINE_TILED_RASTER_STORE__EXPORT_GEOTIFF_HPP_
#define MARINE_TILED_RASTER_STORE__EXPORT_GEOTIFF_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

/// @file
/// @brief Export a box of a tiled raster layer as ONE mosaicked GeoTIFF
///        (unh_marine_autonomy#370, Slice A).
///
/// The stores hold one GeoTIFF per GGGS tile; sharing a survey needs one file.
/// `exportGeoTiff()` reads the tiles of one level that intersect a lat/lon box
/// and writes a single north-up EPSG:4326 Float32 GeoTIFF, strip by strip, so
/// memory stays bounded by one output block-row plus the tiles it overlaps.
///
/// **Layer-agnostic.** Nothing is keyed on a layer name. The band count picks
/// the band names (2 = depth/uncertainty, 3 = mean/scaled_standard_error/
/// sample_sd, anything else = band_N), and a required GDAL header probe refuses
/// any tile that is not Float32/Float64, so a uint16 sidescan layer can never be
/// mislabelled as backscatter.
///
/// **Tile selection (ADR-0011).** One level is exported: the requested one, or
/// the finest level holding a native tile inside the box. Per tile position at
/// that level: the native `<level>_<row>_<col>.tif` wins, else the derived
/// `overviews/<level>_<row>_<col>.tif` sidecar tile, else a NaN hole. Nothing is
/// folded or upsampled on the fly.
///
/// **Datum (ADR-0002 D4).** Stored depth is always WGS84 ellipsoidal; the
/// registry's `datum` is the SOURCE datum an import converted from. A 2-band
/// export is labelled `VERTICAL_DATUM=WGS84_ELLIPSOIDAL` and records the registry
/// value as `SOURCE_VERTICAL_DATUM` (`UNKNOWN` plus a loud warning when it is
/// not recorded). A 3-band (backscatter) export is `NOT_APPLICABLE`.
///
/// **Never a partial file.** Everything that can be refused is checked before
/// the output is created; the raster is written to `<output>.tmp`, closed (and
/// the close checked), then renamed over the output. Every failure path removes
/// the `.tmp`, and an existing output is replaced only by that final rename.
///
/// @note Boxes reaching |lat| >= 72 are refused: `tile_io` is validated only
///   below 72 degrees, and above it GGGS widens tile longitude spans by 3x / 9x,
///   which one north-up pixel width cannot represent.

namespace marine_tiled_raster_store
{

/// @brief A geographic box in degrees (WGS84), given in the CLI's
///        LATMIN LONMIN LATMAX LONMAX order.
struct ExportBox
{
  double lat_min = 0.0;
  double lon_min = 0.0;
  double lat_max = 0.0;
  double lon_max = 0.0;
};

/// @brief What to export.
struct ExportOptions
{
  /// The area to export. Required; snapped OUTWARD to the level's cell grid.
  ExportBox box;
  /// Level to export. `std::nullopt` = the finest level holding a native tile
  /// inside the box.
  std::optional<std::uint8_t> level;
  /// Refuse when width*height (pixels PER BAND, independent of band count)
  /// exceeds this. Deliberately has **no library default**: an unset value is
  /// rejected. The `export_geotiff` CLI supplies its documented default.
  std::optional<std::uint64_t> max_pixels;
};

/// @brief What was written.
struct ExportResult
{
  std::uint64_t width = 0;    ///< Output columns.
  std::uint64_t height = 0;   ///< Output rows.
  std::size_t band_count = 0;
  /// NaN cells per band: total cells minus the non-NaN cells written.
  std::vector<std::uint64_t> nodata_count;
  std::uint8_t source_level = 0;
  /// `native`, `overview` or `mixed`.
  std::string source_kind;
  /// Max over the contributing tiles: `coverage.json`'s `geometric_error_m` for
  /// a sidecar tile, the NOMINAL equatorial `gggs::Level::cellSize()` for a
  /// native tile (not the local ground size).
  double geometric_error_m = 0.0;
  std::vector<std::string> band_names;
  /// `WGS84_ELLIPSOIDAL` (2 bands), `NOT_APPLICABLE` (3 bands) or `UNSPECIFIED`.
  std::string vertical_datum;
  /// Registry `datum` (or `UNKNOWN`) — set for 2-band (depth) exports only.
  std::optional<std::string> source_vertical_datum;
  /// `Float64->Float32` when any source band is Float64, else `NONE`.
  std::string downcast;
  std::size_t tiles_native = 0;     ///< Native tiles read.
  std::size_t tiles_overview = 0;   ///< Sidecar tiles read.
  /// Tile positions the snapped box spans at the source level (read or hole).
  std::uint64_t tile_positions = 0;
  /// The output's GDAL geotransform (north-up; pixel_y negative).
  std::array<double, 6> geotransform{};
  /// Human-readable warnings, in the order they arose. The CLI prints each to
  /// stderr; none of them stopped the export.
  std::vector<std::string> warnings;
};

/// @brief A refusal: the request cannot be exported as asked (empty selection,
///        over the pixel cap, polar box, non-float tile, ...). Nothing is
///        written when it is thrown.
class ExportRefused : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

/// @brief A box snapped to a level's cell lattice, in GLOBAL GGGS cell indices.
///
/// Rows count north from -96 deg, columns east from -180 deg (lattice origin of
/// `gggs::GridIndex`), so tile `(row, col)` owns cell rows
/// `[row*960, row*960+960)` and columns `[col*960, col*960+960)`. Half-open.
/// Valid only where the longitude scale factor is 1 (|lat| < 72).
struct CellWindow
{
  std::uint64_t row_begin = 0;
  std::uint64_t row_end = 0;
  std::uint64_t col_begin = 0;
  std::uint64_t col_end = 0;
};

/// @brief Snap @p box outward to @p level's cell lattice.
///
/// A box edge within 1e-4 of a cell of a lattice line snaps onto that line, so
/// floating-point noise never adds a sliver row; the window is never empty.
/// @throws std::invalid_argument for a level > 20.
CellWindow snapBoxToLevel(const ExportBox & box, std::uint8_t level);

/// @brief Band names for a band count: 2 = `depth,uncertainty`;
///        3 = `mean,scaled_standard_error,sample_sd`; otherwise `band_1..N`.
std::vector<std::string> bandNamesForCount(std::size_t band_count);

/// @brief The registry a layer's datum is read from:
///        `<layer_dir>/../registry.json`, resolved after making @p layer_dir
///        absolute, normalizing it and dropping a trailing slash (so `a/b/`
///        resolves to `a/registry.json`, never `a/b/registry.json`).
std::string registryPathForLayer(const std::string & layer_dir);

/// @brief Export @p options.box of the layer in @p layer_dir to @p output_path.
///
/// Reads only; writes only `<output_path>.tmp` and then `output_path`. A stale
/// `<output_path>.tmp` from a crashed run is removed first.
///
/// @throws std::invalid_argument for invalid options (unset `max_pixels`, box
///   out of range or with min >= max, level > 20).
/// @throws ExportRefused when the request cannot be exported as asked.
/// @throws std::runtime_error on an unreadable tile or any GDAL / filesystem
///   failure. In every throwing case no output and no `.tmp` is left, and an
///   existing @p output_path is untouched.
ExportResult exportGeoTiff(
  const std::string & layer_dir, const std::string & output_path,
  const ExportOptions & options);

}  // namespace marine_tiled_raster_store

#endif  // MARINE_TILED_RASTER_STORE__EXPORT_GEOTIFF_HPP_
