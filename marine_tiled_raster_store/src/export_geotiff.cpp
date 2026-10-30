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

// [#370 Slice A / ADR-0011 / ADR-0002 D4] Whole-area GeoTIFF export from a
// tiled raster layer. See export_geotiff.hpp for the contract.

#include "marine_tiled_raster_store/export_geotiff.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <gdal_priv.h>
#include <cpl_error.h>
#include <cpl_string.h>
#include <ogr_spatialref.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "marine_autonomy/gggs.h"
#include "marine_tiled_raster_store/coverage_manifest.hpp"
#include "marine_tiled_raster_store/tile_io.hpp"
#include "marine_tiled_raster_store/tiled_raster_tile.hpp"

namespace marine_tiled_raster_store
{

namespace
{

namespace fs = std::filesystem;

constexpr std::uint64_t kEdge = gggs::cell_rows_per_grid;   // 960 cells per tile edge
/// Output block edge (TILED GeoTIFF). The writer emits exactly one block-row
/// per strip, so no output block is ever read back and rewritten.
constexpr int kBlock = 256;
/// A box edge this close (in cells) to a lattice line snaps onto it.
constexpr double kSnapToleranceCells = 1e-4;
/// GGGS lattice origin: rows count north from -96 deg, columns east from -180.
constexpr double kLatOrigin = -96.0;
constexpr double kLonOrigin = -180.0;
/// tile_io's validated envelope, and the first longitude-scale boundary.
constexpr double kPolarLimit = 72.0;

enum class Source {kNative, kOverview};

struct SelectedTile
{
  gggs::GridIndex grid;
  Source source;
  std::string path;
};

using TileKey = std::pair<std::uint32_t, std::uint32_t>;   // (tile row, tile column)

/// The part of one loaded tile that lies inside the export window, already cast
/// to Float32: global cell rows [row_begin, row_end) x columns
/// [col_begin, col_end), band-major, rows SOUTH-first like the tile itself. The
/// double tile it came from is dropped as soon as this is filled, so the cache
/// holds only window cells at 4 bytes each.
struct ClippedTile
{
  std::uint64_t row_begin = 0, row_end = 0, col_begin = 0, col_end = 0;
  std::vector<float> data;
  std::size_t rows() const {return static_cast<std::size_t>(row_end - row_begin);}
  std::size_t cols() const {return static_cast<std::size_t>(col_end - col_begin);}
  std::size_t offset(std::size_t band, std::uint64_t global_row) const
  {
    return (band * rows() + static_cast<std::size_t>(global_row - row_begin)) * cols();
  }
  const float * row(std::size_t band, std::uint64_t global_row) const
  {
    return data.data() + offset(band, global_row);
  }
  float * row(std::size_t band, std::uint64_t global_row)
  {
    return data.data() + offset(band, global_row);
  }
};

/// GDAL's own reason for the most recent failure, as ` (GDAL: <message>)`, or
/// empty when it recorded none. Callers CPLErrorReset() before the call they
/// check, so a stale message from an earlier, harmless error is never blamed.
std::string gdalReason()
{
  const char * msg = CPLGetLastErrorMsg();
  if (msg == nullptr || *msg == '\0') {
    return {};
  }
  return std::string(" (GDAL: ") + msg + ")";
}

/// RAII: closes a GDAL dataset on every exit path.
struct DatasetCloser
{
  GDALDataset * ds = nullptr;
  ~DatasetCloser() {if (ds) {GDALClose(ds);}}
};

/// Create `<output>.tmp.XXXXXX` beside the output, EXCLUSIVELY (O_CREAT |
/// O_EXCL), and return its path. The random suffix means two exports aimed at
/// one output never share a temporary file, and O_EXCL means this run never
/// adopts (or later removes) a file it did not create. Created 0666 so the
/// umask decides the final output's mode, as for any new file (mkstemp would
/// force 0600 onto the renamed output).
fs::path createUniqueTmp(const std::string & output_path)
{
  static constexpr char kAlphabet[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::random_device seed;
  std::mt19937_64 gen((static_cast<std::uint64_t>(seed()) << 32) ^ seed());
  std::uniform_int_distribution<std::size_t> pick(0, sizeof(kAlphabet) - 2);
  constexpr int kAttempts = 64;
  for (int attempt = 0; attempt < kAttempts; ++attempt) {
    std::string suffix(6, 'x');
    for (char & c : suffix) {
      c = kAlphabet[pick(gen)];
    }
    const fs::path p(output_path + ".tmp." + suffix);
    const int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (fd >= 0) {
      ::close(fd);
      return p;
    }
    if (errno != EEXIST) {
      throw std::runtime_error(
              "could not create the temporary file " + p.string() + ": " +
              std::strerror(errno));
    }
  }
  throw std::runtime_error(
          "could not create a unique temporary file " + output_path + ".tmp.XXXXXX after " +
          std::to_string(kAttempts) + " attempts");
}

/// RAII: removes THIS run's temporary file on every exit path that does not
/// disarm it. Declared BEFORE the dataset closer so the dataset is closed first.
struct TmpRemover
{
  fs::path path;
  bool armed = true;
  ~TmpRemover()
  {
    if (armed) {
      std::error_code ec;
      fs::remove(path, ec);
    }
  }
};

std::string levelList(const std::set<std::uint8_t> & levels)
{
  if (levels.empty()) {
    return "none";
  }
  std::string out;
  for (const std::uint8_t l : levels) {
    if (!out.empty()) {
      out += ", ";
    }
    out += std::to_string(static_cast<unsigned>(l));
  }
  return out;
}

std::string formatDouble(double v)
{
  std::ostringstream s;
  s.precision(std::numeric_limits<double>::max_digits10);   // round-trips
  s << v;
  return s.str();
}

fs::path withoutTrailingSeparator(fs::path p)
{
  if (p.has_parent_path() && p.filename().empty()) {
    p = p.parent_path();   // "a/b/" -> "a/b"
  }
  return p;
}

/// @p p made absolute with every symlink resolved (`weakly_canonical`: the
/// existing prefix is resolved, a not-yet-existing tail is normalized), and no
/// trailing separator. Two spellings of one place compare equal, which is what
/// both the store guard and the registry lookup need.
fs::path resolvedPath(const fs::path & p, std::error_code & ec)
{
  const fs::path abs = fs::absolute(p, ec);
  if (ec) {
    return {};
  }
  const fs::path r = fs::weakly_canonical(abs, ec);
  if (ec) {
    return {};
  }
  return withoutTrailingSeparator(r);
}

fs::path resolvedPath(const fs::path & p)
{
  std::error_code ec;
  fs::path r = resolvedPath(p, ec);
  if (ec) {
    throw std::runtime_error("cannot resolve path " + p.string() + ": " + ec.message());
  }
  return r;
}

/// Whether @p p is @p dir or lies anywhere beneath it (component-wise, so
/// `/a/bc` is not under `/a/b`). Both must come from resolvedPath().
bool isWithin(const fs::path & p, const fs::path & dir)
{
  auto pi = p.begin();
  for (auto di = dir.begin(); di != dir.end(); ++di, ++pi) {
    if (pi == p.end() || *pi != *di) {
      return false;
    }
  }
  return true;
}

void validateOptions(const ExportOptions & options)
{
  if (!options.max_pixels.has_value()) {
    throw std::invalid_argument(
            "exportGeoTiff: max_pixels must be set (there is no library default)");
  }
  if (*options.max_pixels == 0) {
    throw std::invalid_argument("exportGeoTiff: max_pixels must be > 0");
  }
  const ExportBox & b = options.box;
  for (const double v : {b.lat_min, b.lon_min, b.lat_max, b.lon_max}) {
    if (!std::isfinite(v)) {
      throw std::invalid_argument("exportGeoTiff: box values must be finite");
    }
  }
  if (b.lat_min < -90.0 || b.lat_max > 90.0) {
    throw std::invalid_argument("exportGeoTiff: box latitude outside [-90, 90]");
  }
  if (b.lon_min < -180.0 || b.lon_max > 180.0) {
    throw std::invalid_argument("exportGeoTiff: box longitude outside [-180, 180]");
  }
  if (!(b.lat_min < b.lat_max) || !(b.lon_min < b.lon_max)) {
    throw std::invalid_argument(
            "exportGeoTiff: box needs LATMIN < LATMAX and LONMIN < LONMAX");
  }
  if (options.level.has_value() &&
    static_cast<std::size_t>(*options.level) >= gggs::levels.size())
  {
    throw std::invalid_argument(
            "exportGeoTiff: level " + std::to_string(static_cast<unsigned>(*options.level)) +
            " is not a GGGS level (0-20)");
  }
}

/// Tile-row / tile-column range a window spans (inclusive).
struct TileRange
{
  std::uint64_t row_first = 0, row_last = 0, col_first = 0, col_last = 0;
  bool contains(const gggs::GridIndex & g) const
  {
    return g.row() >= row_first && g.row() <= row_last &&
           g.column() >= col_first && g.column() <= col_last;
  }
  std::uint64_t count() const
  {
    return (row_last - row_first + 1) * (col_last - col_first + 1);
  }
};

TileRange tileRange(const CellWindow & w)
{
  return TileRange{w.row_begin / kEdge, (w.row_end - 1) / kEdge,
    w.col_begin / kEdge, (w.col_end - 1) / kEdge};
}

/// The grids among @p grids that lie inside @p box at their own level.
std::map<std::uint8_t, std::vector<gggs::GridIndex>> gridsInBoxByLevel(
  const std::vector<gggs::GridIndex> & grids, const ExportBox & box)
{
  std::map<std::uint8_t, TileRange> ranges;
  std::map<std::uint8_t, std::vector<gggs::GridIndex>> out;
  for (const gggs::GridIndex & g : grids) {
    auto it = ranges.find(g.level());
    if (it == ranges.end()) {
      it = ranges.emplace(g.level(), tileRange(snapBoxToLevel(box, g.level()))).first;
    }
    if (it->second.contains(g)) {
      out[g.level()].push_back(g);
    }
  }
  return out;
}

std::set<std::uint8_t> levelsOf(
  const std::map<std::uint8_t, std::vector<gggs::GridIndex>> & by_level)
{
  std::set<std::uint8_t> levels;
  for (const auto & item : by_level) {
    levels.insert(item.first);
  }
  return levels;
}

/// Header-only GDAL probe of one tile: band count + whether any band is
/// Float64. Refuses a non-float band; an unopenable file is a runtime failure.
struct Probe
{
  int bands = 0;
  bool float64 = false;
};

Probe probeTile(const std::string & path)
{
  DatasetCloser in;
  CPLErrorReset();
  in.ds = GDALDataset::FromHandle(GDALOpen(path.c_str(), GA_ReadOnly));
  if (in.ds == nullptr) {
    throw std::runtime_error(
            "cannot open tile " + path + " (unreadable or not a raster)" + gdalReason());
  }
  Probe p;
  p.bands = in.ds->GetRasterCount();
  if (p.bands < 1) {
    throw ExportRefused("tile " + path + " has no raster bands");
  }
  for (int b = 1; b <= p.bands; ++b) {
    const GDALDataType t = in.ds->GetRasterBand(b)->GetRasterDataType();
    if (t == GDT_Float64) {
      p.float64 = true;
    } else if (t != GDT_Float32) {
      throw ExportRefused(
              "tile " + path + " band " + std::to_string(b) + " is " +
              GDALGetDataTypeName(t) +
              "; only Float32/Float64 layers can be exported (a non-float layer, "
              "e.g. uint16 sidescan, has no NaN no-data and no band-name table here)");
    }
  }
  return p;
}

/// Source vertical datum from the layer's parent registry. Never throws: a
/// missing or unparsable registry is reported through @p problem.
std::optional<std::string> readRegistryDatum(const std::string & path, std::string & problem)
{
  std::error_code ec;
  if (!fs::is_regular_file(path, ec)) {
    problem = "registry not found";
    return std::nullopt;
  }
  std::ifstream in(path);
  if (!in) {
    problem = "registry could not be opened";
    return std::nullopt;
  }
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
  if (doc.is_discarded() || !doc.is_object()) {
    problem = "registry is not a parsable JSON object";
    return std::nullopt;
  }
  const auto it = doc.find("datum");
  if (it == doc.end()) {
    problem = "registry has no \"datum\" field";
    return std::nullopt;
  }
  if (!it->is_string()) {
    problem = "registry \"datum\" is not a string";
    return std::nullopt;
  }
  const std::string datum = it->get<std::string>();
  if (datum.empty()) {
    problem = "registry \"datum\" is empty";
    return std::nullopt;
  }
  return datum;
}

/// Native tiles at levels other than the export level whose data really is
/// missing from the export. Two cases, judged per tile position (no pixels are
/// read):
///  - HOLE: a native tile at another level covers a position the export level
///    leaves empty (no native and no sidecar tile there). For a coarser native
///    tile that is every uncovered export-level position beneath it (a coarser
///    level is never upsampled); for a finer one, its own export-level ancestor
///    position (stale or missing sidecar).
///  - SHADOWED: a finer native tile has a NATIVE ancestor at a level from the
///    export level up to its own (exclusive). The overview fold never carries
///    data past a native tile (ADR-0011 fold scope), so whatever fills that
///    export-level position holds none of the finer tile's data.
/// A native tile at another level that is fully covered by an export-level
/// tile carrying its data (a finer native tile whose sidecar ancestor is
/// present with no native tile in between, or a coarser one under export-level
/// tiles) is not reported: nothing of it is lost.
/// Valid where the longitude scale factor is 1 (every tile in a non-polar
/// box), where the tile lattice nests exactly: level L tile (r, c) covers level
/// L+k tile rows [r*2^k, (r+1)*2^k) and columns [c*2^k, (c+1)*2^k).
std::vector<std::string> excludedLevelWarnings(
  std::uint8_t level, const std::map<std::uint8_t, std::vector<gggs::GridIndex>> & native_in_box,
  const std::vector<gggs::GridIndex> & native_all,
  const std::map<TileKey, SelectedTile> & selection, const TileRange & tiles)
{
  using LevelKey = std::tuple<std::uint8_t, std::uint64_t, std::uint64_t>;
  std::set<LevelKey> native_keys;
  for (const gggs::GridIndex & g : native_all) {
    native_keys.emplace(g.level(), g.row(), g.column());
  }
  const auto selected = [&selection](std::uint64_t row, std::uint64_t col) {
      return selection.count(
        TileKey{static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(col)}) != 0;
    };
  std::set<std::uint8_t> hole_levels, shadowed_levels, shadowing_levels;
  std::set<std::pair<std::uint64_t, std::uint64_t>> holes;
  for (const auto & item : native_in_box) {
    const std::uint8_t other = item.first;
    if (other == level) {
      continue;
    }
    for (const gggs::GridIndex & g : item.second) {
      if (other < level) {
        const unsigned k = static_cast<unsigned>(level - other);
        const std::uint64_t r_first =
          std::max<std::uint64_t>(static_cast<std::uint64_t>(g.row()) << k, tiles.row_first);
        const std::uint64_t r_last = std::min<std::uint64_t>(
          ((static_cast<std::uint64_t>(g.row()) + 1) << k) - 1, tiles.row_last);
        const std::uint64_t c_first =
          std::max<std::uint64_t>(static_cast<std::uint64_t>(g.column()) << k, tiles.col_first);
        const std::uint64_t c_last = std::min<std::uint64_t>(
          ((static_cast<std::uint64_t>(g.column()) + 1) << k) - 1, tiles.col_last);
        for (std::uint64_t r = r_first; r <= r_last; ++r) {
          for (std::uint64_t c = c_first; c <= c_last; ++c) {
            if (!selected(r, c)) {
              holes.emplace(r, c);
              hole_levels.insert(other);
            }
          }
        }
        continue;
      }
      // Finer: the nearest native ancestor from its own level - 1 down to the
      // export level, if any.
      bool shadowed = false;
      for (int a = static_cast<int>(other) - 1; a >= static_cast<int>(level); --a) {
        const unsigned k = static_cast<unsigned>(static_cast<int>(other) - a);
        if (native_keys.count(
            LevelKey{static_cast<std::uint8_t>(a), g.row() >> k, g.column() >> k}) != 0)
        {
          shadowed = true;
          shadowing_levels.insert(static_cast<std::uint8_t>(a));
          break;
        }
      }
      if (shadowed) {
        shadowed_levels.insert(other);
        continue;
      }
      const unsigned k = static_cast<unsigned>(other - level);
      const std::uint64_t r = static_cast<std::uint64_t>(g.row()) >> k;
      const std::uint64_t c = static_cast<std::uint64_t>(g.column()) >> k;
      if (!selected(r, c)) {
        holes.emplace(r, c);
        hole_levels.insert(other);
      }
    }
  }
  std::vector<std::string> warnings;
  const std::string level_str = std::to_string(static_cast<unsigned>(level));
  if (!holes.empty()) {
    warnings.push_back(
      "native tiles at level(s) " + levelList(hole_levels) + " inside the box are NOT "
      "exported: only level " + level_str + " is read (native, else sidecar), and " +
      std::to_string(holes.size()) + " level-" + level_str + " tile position(s) they "
      "cover have no tile at that level, so their data appears there as NaN holes");
  }
  if (!shadowed_levels.empty()) {
    warnings.push_back(
      "native tiles at level(s) " + levelList(shadowed_levels) + " inside the box are NOT "
      "exported: they lie beneath native tile(s) at level(s) " + levelList(shadowing_levels) +
      ", and the overview fold never carries data past a native tile (ADR-0011 fold "
      "scope), so their data never reached level " + level_str + "; export at their "
      "own level to include it");
  }
  return warnings;
}

void checkCE(CPLErr err, const std::string & what)
{
  if (err != CE_None) {
    throw std::runtime_error("GDAL error: " + what + gdalReason());
  }
}

}  // namespace

CellWindow snapBoxToLevel(const ExportBox & box, std::uint8_t level)
{
  if (static_cast<std::size_t>(level) >= gggs::levels.size()) {
    throw std::invalid_argument(
            "snapBoxToLevel: level " + std::to_string(static_cast<unsigned>(level)) +
            " is not a GGGS level (0-20)");
  }
  const gggs::LevelSpecs & spec = gggs::levels[level];
  const double cell = spec.cell_angular_span;
  const auto lower = [cell](double degrees_from_origin) {
      const double v = std::floor(degrees_from_origin / cell + kSnapToleranceCells);
      return static_cast<std::uint64_t>(std::max(0.0, v));
    };
  const auto upper = [cell](double degrees_from_origin) {
      const double v = std::ceil(degrees_from_origin / cell - kSnapToleranceCells);
      return static_cast<std::uint64_t>(std::max(0.0, v));
    };
  const std::uint64_t max_rows = static_cast<std::uint64_t>(spec.row_count) * kEdge;
  const std::uint64_t max_cols = static_cast<std::uint64_t>(spec.column_count) * kEdge;
  CellWindow w;
  w.row_begin = std::min(lower(box.lat_min - kLatOrigin), max_rows - 1);
  w.row_end = std::min(std::max(upper(box.lat_max - kLatOrigin), w.row_begin + 1), max_rows);
  w.col_begin = std::min(lower(box.lon_min - kLonOrigin), max_cols - 1);
  w.col_end = std::min(std::max(upper(box.lon_max - kLonOrigin), w.col_begin + 1), max_cols);
  return w;
}

std::vector<std::string> bandNamesForCount(std::size_t band_count)
{
  if (band_count == 2) {
    return {"depth", "uncertainty"};
  }
  if (band_count == 3) {
    return {"mean", "scaled_standard_error", "sample_sd"};
  }
  std::vector<std::string> names;
  for (std::size_t b = 0; b < band_count; ++b) {
    names.push_back("band_" + std::to_string(b + 1));
  }
  return names;
}

std::string registryPathForLayer(const std::string & layer_dir)
{
  // Resolved like the store guard: through a symlinked layer path the lexical
  // parent would be the LINK's directory, missing the store's registry.json or
  // reading another store's. If the path cannot be resolved (a permission
  // error on a component), fall back to the lexical parent; the read below
  // then reports what it found there.
  std::error_code ec;
  fs::path layer = resolvedPath(fs::path(layer_dir), ec);
  if (ec) {
    layer = withoutTrailingSeparator(fs::absolute(fs::path(layer_dir)).lexically_normal());
  }
  return (layer.parent_path() / "registry.json").string();
}

ExportResult exportGeoTiff(
  const std::string & layer_dir, const std::string & output_path,
  const ExportOptions & options)
{
  validateOptions(options);
  const ExportBox & box = options.box;
  const std::uint64_t max_pixels = *options.max_pixels;

  if (output_path.empty()) {
    throw std::invalid_argument("exportGeoTiff: output path is empty");
  }
  const fs::path out_path(output_path);
  const fs::path layer = resolvedPath(fs::path(layer_dir));
  const fs::path sidecar = layer / "overviews";
  // The store is data of record and the tool is read-only on it. Refuse an
  // output anywhere under the directory holding the layer and its registry.json:
  // that covers the layer, its overviews/ sidecar, any subdirectory, sibling
  // layers and the registry itself. Paths are compared after resolving
  // symlinks, so a link to the layer is no way around it. Checked twice: the
  // output as given (following it if it is itself a symlink), and its resolved
  // directory, where the temporary file is created. Runs before any file exists.
  {
    const fs::path store_root = layer.parent_path();
    const fs::path out_abs = fs::absolute(out_path);
    for (const fs::path & candidate :
      {resolvedPath(out_abs), resolvedPath(out_abs.parent_path()) / out_abs.filename()})
    {
      if (isWithin(candidate, store_root)) {
        throw ExportRefused(
                "refusing to write the export inside the store: " + candidate.string() +
                " is under " + store_root.string() + ", the directory holding layer " +
                layer.filename().string() + " and its registry.json; choose an output "
                "outside the store");
      }
    }
  }
  std::error_code ec;
  if (!fs::is_directory(layer, ec)) {
    throw ExportRefused("layer directory does not exist: " + layer.string());
  }

  // Polar refusal. 72 deg is a tile boundary at every level, so a box below it
  // snaps to tile rows that are all scale factor 1.
  if (box.lat_max >= kPolarLimit || box.lat_min <= -kPolarLimit) {
    const auto straddles = [&box](double boundary) {
        return box.lat_min < boundary && boundary < box.lat_max;
      };
    const bool crosses = straddles(kPolarLimit) || straddles(-kPolarLimit) ||
      straddles(80.0) || straddles(-80.0);
    throw ExportRefused(
            std::string("box reaches |lat| >= 72 degrees") +
            (crosses ? " and crosses a GGGS longitude-scale boundary" : "") +
            ": tile I/O is validated only below 72 degrees and one north-up pixel "
            "width cannot span scale bands (polar export is not supported)");
  }

  ExportResult result;

  // ---- Discover native and sidecar tiles (names only). ----
  std::size_t skipped_native = 0;
  const std::vector<gggs::GridIndex> native_all =
    gridsInDir(layer.string(), std::nullopt, skipped_native);
  if (skipped_native > 0) {
    result.warnings.push_back(
      std::to_string(skipped_native) + " tile-shaped name(s) in " + layer.string() +
      " could not be parsed and are ignored; any of them inside the box is "
      "exported as a NaN hole");
  }
  const bool has_sidecar = fs::is_directory(sidecar, ec);
  std::size_t skipped_sidecar = 0;
  const std::vector<gggs::GridIndex> sidecar_all = has_sidecar ?
    gridsInDir(sidecar.string(), std::nullopt, skipped_sidecar) :
    std::vector<gggs::GridIndex>{};
  if (skipped_sidecar > 0) {
    result.warnings.push_back(
      std::to_string(skipped_sidecar) + " tile-shaped name(s) in " + sidecar.string() +
      " could not be parsed and are ignored");
  }
  const auto native_in_box = gridsInBoxByLevel(native_all, box);
  const auto sidecar_in_box = gridsInBoxByLevel(sidecar_all, box);
  const std::set<std::uint8_t> native_levels = levelsOf(native_in_box);
  const std::set<std::uint8_t> sidecar_levels = levelsOf(sidecar_in_box);
  const std::string available =
    "native levels in the box: " + levelList(native_levels) +
    "; sidecar levels in the box: " +
    (has_sidecar ? levelList(sidecar_levels) : std::string("no overviews/ sidecar"));

  // ---- Choose the level. ----
  std::uint8_t level = 0;
  if (options.level.has_value()) {
    level = *options.level;
  } else if (!native_levels.empty()) {
    level = *native_levels.rbegin();
  } else {
    throw ExportRefused(
            "the box intersects no native tile in " + layer.string() + " (" + available +
            "); pass --level to export a sidecar level, or check the box "
            "(LATMIN LONMIN LATMAX LONMAX)");
  }
  result.source_level = level;

  // ---- Window, size and cap: before anything is read or allocated. ----
  const CellWindow window = snapBoxToLevel(box, level);
  const TileRange tiles = tileRange(window);
  const std::uint64_t width = window.col_end - window.col_begin;
  const std::uint64_t height = window.row_end - window.row_begin;
  const std::string level_str = std::to_string(static_cast<unsigned>(level));
  if (height != 0 && width > max_pixels / height) {
    const bool overflows = width > std::numeric_limits<std::uint64_t>::max() / height;
    throw ExportRefused(
            "box at level " + level_str + " is " + std::to_string(width) + " x " +
            std::to_string(height) + " px (" +
            (overflows ? std::string("more than 2^64") : std::to_string(width * height)) +
            " pixels per band), over the cap of " + std::to_string(max_pixels) +
            "; shrink the box, choose a coarser --level, or raise --max-pixels");
  }
  constexpr std::uint64_t kMaxDim = static_cast<std::uint64_t>(std::numeric_limits<int>::max());
  if (width > kMaxDim || height > kMaxDim) {
    throw ExportRefused(
            "box at level " + level_str + " is " + std::to_string(width) + " x " +
            std::to_string(height) + " px; a GeoTIFF dimension is limited to " +
            std::to_string(kMaxDim));
  }
  result.width = width;
  result.height = height;
  result.tile_positions = tiles.count();

  // ---- Per-tile selection at the level: native, else sidecar, else hole. ----
  std::map<TileKey, SelectedTile> selection;
  const auto native_at = native_in_box.find(level);
  if (native_at != native_in_box.end()) {
    for (const gggs::GridIndex & g : native_at->second) {
      selection.emplace(
        TileKey{g.row(), g.column()},
        SelectedTile{g, Source::kNative, (layer / tileFilename(g)).string()});
    }
  }
  const auto sidecar_at = sidecar_in_box.find(level);
  if (sidecar_at != sidecar_in_box.end()) {
    for (const gggs::GridIndex & g : sidecar_at->second) {
      // emplace keeps an existing native entry: native always wins.
      selection.emplace(
        TileKey{g.row(), g.column()},
        SelectedTile{g, Source::kOverview, (sidecar / tileFilename(g)).string()});
    }
  }
  if (selection.empty()) {
    const bool finer_than_native = !native_levels.empty() && level > *native_levels.rbegin();
    if (finer_than_native) {
      throw ExportRefused(
              "level " + level_str + " is finer than the finest native level in the box (" +
              std::to_string(static_cast<unsigned>(*native_levels.rbegin())) +
              "); refusing to upsample (" + available + ")");
    }
    throw ExportRefused(
            "level " + level_str + " has no native tile in the box and " +
            (has_sidecar ? std::string("the overviews/ sidecar has none at that level") :
            std::string("the layer has no overviews/ sidecar (if a rebuild is in progress, "
            "retry)")) +
            " (" + available + "); levels are never folded on the fly — build the "
            "layer's overviews (build_depth_overviews / build_sidescan_overviews) "
            "or choose an available level");
  }
  for (std::string & w : excludedLevelWarnings(level, native_in_box, native_all, selection, tiles)) {
    result.warnings.push_back(std::move(w));
  }

  // ---- Required dtype probe of every selected tile (headers only). ----
  GDALAllRegister();
  std::optional<Probe> first;
  std::string first_path;
  bool any_float64 = false;
  for (const auto & item : selection) {
    const Probe p = probeTile(item.second.path);
    if (!first.has_value()) {
      first = p;
      first_path = item.second.path;
    } else if (p.bands != first->bands) {
      throw ExportRefused(
              "mixed band counts in the selection: " + first_path + " has " +
              std::to_string(first->bands) + " band(s), " + item.second.path + " has " +
              std::to_string(p.bands));
    }
    any_float64 = any_float64 || p.float64;
    if (item.second.source == Source::kNative) {
      ++result.tiles_native;
    } else {
      ++result.tiles_overview;
    }
  }
  const std::size_t bands = static_cast<std::size_t>(first->bands);
  result.band_count = bands;
  result.band_names = bandNamesForCount(bands);
  result.downcast = any_float64 ? "Float64->Float32" : "NONE";
  result.source_kind = result.tiles_overview == 0 ? "native" :
    (result.tiles_native == 0 ? "overview" : "mixed");

  // ---- Geometric error: max over contributing tiles. ----
  const double nominal_cell = gggs::Level(level).cellSize();
  double geometric_error = 0.0;
  if (result.tiles_native > 0) {
    geometric_error = nominal_cell;
  }
  if (result.tiles_overview > 0) {
    const std::optional<CoverageManifest> manifest =
      loadCoverageManifest((sidecar / coverageManifestFilename()).string());
    std::size_t unrecorded = 0;
    for (const auto & item : selection) {
      if (item.second.source != Source::kOverview) {
        continue;
      }
      const std::optional<double> e =
        manifest.has_value() ? manifest->geometricError(item.second.grid) : std::nullopt;
      if (e.has_value() && std::isfinite(*e)) {
        geometric_error = std::max(geometric_error, *e);
      } else {
        ++unrecorded;
        geometric_error = std::max(geometric_error, nominal_cell);
      }
    }
    if (unrecorded > 0) {
      result.warnings.push_back(
        std::to_string(unrecorded) + " sidecar tile(s) have no geometric error in " +
        (sidecar / coverageManifestFilename()).string() +
        "; GEOMETRIC_ERROR_M falls back to the nominal cell size for them, which "
        "UNDERSTATES a derived tile's error");
    }
  }
  result.geometric_error_m = geometric_error;

  // ---- Datum policy by band count. ----
  if (bands == 2) {
    result.vertical_datum = "WGS84_ELLIPSOIDAL";
    const std::string registry = registryPathForLayer(layer.string());
    std::string problem;
    const std::optional<std::string> datum = readRegistryDatum(registry, problem);
    if (datum.has_value()) {
      result.source_vertical_datum = *datum;
    } else {
      result.source_vertical_datum = "UNKNOWN";
      result.warnings.push_back(
        "source datum conversion not recorded for " + layer.string() + "\n"
        "  registry: " + registry + " (" + problem + ")\n"
        "  Stored depths are WGS84 ellipsoidal by the store contract (ADR-0002 D4), so\n"
        "  the output is labelled VERTICAL_DATUM=WGS84_ELLIPSOIDAL, but the datum the\n"
        "  import converted FROM is not on record: SOURCE_VERTICAL_DATUM=UNKNOWN.\n"
        "  The registry is shared by sibling layers and may describe another one.");
    }
  } else if (bands == 3) {
    result.vertical_datum = "NOT_APPLICABLE";
  } else {
    result.vertical_datum = "UNSPECIFIED";
    result.warnings.push_back(
      std::to_string(bands) + " band(s) match no known layer kind (2 = depth, "
      "3 = backscatter): bands are named band_1..band_" + std::to_string(bands) +
      " and no vertical datum is asserted");
  }

  // ---- Geotransform from the integer origin. ----
  const double cell = gggs::levels[level].cell_angular_span;
  const double west = kLonOrigin + static_cast<double>(window.col_begin) * cell;
  const double north = kLatOrigin + static_cast<double>(window.row_end) * cell;
  result.geotransform = {west, cell, 0.0, north, 0.0, -cell};

  // ---- Write: a unique <output>.tmp.XXXXXX, strip by strip, close checked,
  // rename. Only the file this run created is ever removed; nothing else
  // matching the pattern (another run's, or a crashed run's) is touched. ----
  GDALDriver * driver = GetGDALDriverManager()->GetDriverByName("GTiff");
  if (driver == nullptr) {
    throw std::runtime_error("GTiff driver unavailable");
  }
  const fs::path tmp_path = createUniqueTmp(output_path);
  TmpRemover tmp_guard{tmp_path};
  DatasetCloser out;
  {
    char ** create = nullptr;
    create = CSLSetNameValue(create, "TILED", "YES");
    create = CSLSetNameValue(create, "BLOCKXSIZE", std::to_string(kBlock).c_str());
    create = CSLSetNameValue(create, "BLOCKYSIZE", std::to_string(kBlock).c_str());
    create = CSLSetNameValue(create, "COMPRESS", "DEFLATE");
    create = CSLSetNameValue(create, "PREDICTOR", "3");
    create = CSLSetNameValue(create, "BIGTIFF", "IF_SAFER");
    CPLErrorReset();
    out.ds = driver->Create(
      tmp_path.c_str(), static_cast<int>(width), static_cast<int>(height),
      static_cast<int>(bands), GDT_Float32, create);
    CSLDestroy(create);
  }
  if (out.ds == nullptr) {
    throw std::runtime_error("could not create " + tmp_path.string() + gdalReason());
  }
  double gt[6];
  std::copy(result.geotransform.begin(), result.geotransform.end(), gt);
  CPLErrorReset();
  checkCE(out.ds->SetGeoTransform(gt), "set geotransform");
  {
    OGRSpatialReference srs;
    if (srs.importFromEPSG(4326) != OGRERR_NONE) {
      throw std::runtime_error("could not build EPSG:4326");
    }
    // The geotransform is x = longitude, y = latitude (GDAL's traditional GIS
    // order), whatever axis order the EPSG definition declares.
    srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    checkCE(out.ds->SetSpatialRef(&srs), "set spatial reference");
  }
  const double nan = std::numeric_limits<double>::quiet_NaN();
  for (std::size_t b = 0; b < bands; ++b) {
    GDALRasterBand * band = out.ds->GetRasterBand(static_cast<int>(b) + 1);
    band->SetDescription(result.band_names[b].c_str());
    checkCE(band->SetNoDataValue(nan), "set no-data");
  }

  // One block-row strip, never taller than the output: a short, wide box must
  // not allocate 256 rows it will never fill.
  const std::uint64_t strip_rows = std::min<std::uint64_t>(height, kBlock);
  std::vector<float> strip(bands * static_cast<std::size_t>(width * strip_rows));
  std::vector<std::uint64_t> valid(bands, 0);
  std::uint64_t overflowed = 0;
  // Loaded tiles, clipped to the window and cast to Float32, in GGGS cell order
  // (row 0 = SOUTH: loadTile flips the north-up file back once). Strips run
  // north -> south and are at most 256 rows, so the cache spans at most two
  // tile rows, and a tile whose tile row lies wholly north of the current strip
  // is never needed again. Memory is bounded by the window, not by the tiles:
  // at most min(height, 1920) window rows of cached cells plus the strip plus
  // one tile being decoded, i.e. no more than about twice the uncompressed
  // output, which --max-pixels caps.
  std::map<TileKey, ClippedTile> cache;
  for (std::uint64_t y0 = 0; y0 < height; y0 += kBlock) {
    const std::uint64_t rows = std::min<std::uint64_t>(kBlock, height - y0);
    const std::size_t plane = static_cast<std::size_t>(width * rows);
    std::fill(strip.begin(), strip.begin() + bands * plane,
        std::numeric_limits<float>::quiet_NaN());

    // Output row y (0 = north) is global cell row row_end-1-y: the one place the
    // north-up / south-first orientation is reconciled. Pixel rows within a tile
    // are NOT flipped again.
    const std::uint64_t top_row = window.row_end - 1 - y0;
    const std::uint64_t bottom_row = window.row_end - y0 - rows;
    const std::uint64_t tr_top = top_row / kEdge;
    const std::uint64_t tr_bottom = bottom_row / kEdge;
    for (auto it = cache.begin(); it != cache.end(); ) {
      it = it->first.first > tr_top ? cache.erase(it) : std::next(it);
    }
    for (std::uint64_t tr = tr_bottom; tr <= tr_top; ++tr) {
      for (std::uint64_t tc = tiles.col_first; tc <= tiles.col_last; ++tc) {
        const TileKey key{static_cast<std::uint32_t>(tr), static_cast<std::uint32_t>(tc)};
        const auto sel = selection.find(key);
        if (sel == selection.end() || cache.count(key) != 0) {
          continue;
        }
        const TiledRasterTile<double> tile =
          loadTile<double>(sel->second.path, gggs::Level(level), bands);
        if (tile.index() != sel->second.grid) {
          throw std::runtime_error(
                  "tile " + sel->second.path + " is georeferenced as " +
                  tileFilename(tile.index()) + ", not as its name says");
        }
        // Clip to the window and cast once. Each window cell lies in exactly
        // one tile and each tile is loaded once, so the valid and overflow
        // counts are taken here, exactly once per cell.
        ClippedTile clipped;
        clipped.row_begin = std::max(window.row_begin, tr * kEdge);
        clipped.row_end = std::min(window.row_end, tr * kEdge + kEdge);
        clipped.col_begin = std::max(window.col_begin, tc * kEdge);
        clipped.col_end = std::min(window.col_end, tc * kEdge + kEdge);
        clipped.data.resize(bands * clipped.rows() * clipped.cols());
        for (std::size_t b = 0; b < bands; ++b) {
          for (std::uint64_t gr = clipped.row_begin; gr < clipped.row_end; ++gr) {
            const double * src = tile.band(b).data() +
              static_cast<std::size_t>(gr - tr * kEdge) * kEdge +
              static_cast<std::size_t>(clipped.col_begin - tc * kEdge);
            float * dst = clipped.row(b, gr);
            for (std::size_t i = 0; i < clipped.cols(); ++i) {
              const double v = src[i];
              const float f = static_cast<float>(v);
              dst[i] = f;
              if (!std::isnan(f)) {
                ++valid[b];
                if (std::isfinite(v) && !std::isfinite(f)) {
                  ++overflowed;
                }
              }
            }
          }
        }
        cache.emplace(key, std::move(clipped));
      }
    }

    for (std::uint64_t yy = 0; yy < rows; ++yy) {
      const std::uint64_t gr = top_row - yy;
      const std::uint64_t tr = gr / kEdge;
      for (std::uint64_t tc = tiles.col_first; tc <= tiles.col_last; ++tc) {
        const auto hit = cache.find(
          TileKey{static_cast<std::uint32_t>(tr), static_cast<std::uint32_t>(tc)});
        if (hit == cache.end()) {
          continue;
        }
        const ClippedTile & ct = hit->second;
        for (std::size_t b = 0; b < bands; ++b) {
          const float * src = ct.row(b, gr);
          float * dst = strip.data() + b * plane + static_cast<std::size_t>(yy * width) +
            static_cast<std::size_t>(ct.col_begin - window.col_begin);
          std::copy(src, src + ct.cols(), dst);
        }
      }
    }

    CPLErrorReset();
    checkCE(
      out.ds->RasterIO(
        GF_Write, 0, static_cast<int>(y0), static_cast<int>(width), static_cast<int>(rows),
        strip.data(), static_cast<int>(width), static_cast<int>(rows), GDT_Float32,
        static_cast<int>(bands), nullptr, sizeof(float),
        static_cast<GSpacing>(sizeof(float)) * static_cast<GSpacing>(width),
        static_cast<GSpacing>(sizeof(float)) * static_cast<GSpacing>(plane), nullptr),
      "write strip at row " + std::to_string(y0));
  }
  cache.clear();

  const std::uint64_t total = width * height;
  for (std::size_t b = 0; b < bands; ++b) {
    result.nodata_count.push_back(total - valid[b]);
  }
  if (overflowed > 0) {
    result.warnings.push_back(
      std::to_string(overflowed) + " finite value(s) exceed the Float32 range and "
      "were written as +/-inf");
  }

  // ---- Metadata. ----
  out.ds->SetMetadataItem("SOURCE_LEVEL", level_str.c_str());
  out.ds->SetMetadataItem("SOURCE_KIND", result.source_kind.c_str());
  out.ds->SetMetadataItem("GEOMETRIC_ERROR_M", formatDouble(result.geometric_error_m).c_str());
  std::string joined;
  for (const std::string & n : result.band_names) {
    joined += (joined.empty() ? "" : ",") + n;
  }
  out.ds->SetMetadataItem("BAND_NAMES", joined.c_str());
  out.ds->SetMetadataItem("VERTICAL_DATUM", result.vertical_datum.c_str());
  if (result.source_vertical_datum.has_value()) {
    out.ds->SetMetadataItem("SOURCE_VERTICAL_DATUM", result.source_vertical_datum->c_str());
  }
  out.ds->SetMetadataItem("DOWNCAST", result.downcast.c_str());
  out.ds->SetMetadataItem("NODATA_COUNT", std::to_string(result.nodata_count[0]).c_str());
  for (std::size_t b = 0; b < bands; ++b) {
    out.ds->GetRasterBand(static_cast<int>(b) + 1)->SetMetadataItem(
      "NODATA_COUNT", std::to_string(result.nodata_count[b]).c_str());
  }

  // Close and check: the compressed raster is flushed here, so a disk-full or
  // I/O failure surfaces only now (GDALClose returns CPLErr since GDAL 3.7).
  GDALDataset * ds = out.ds;
  out.ds = nullptr;
  CPLErrorReset();
  if (GDALClose(ds) != CE_None) {
    throw std::runtime_error("failed to flush/close " + tmp_path.string() + gdalReason());
  }
  fs::rename(tmp_path, out_path);   // atomic replace of an existing output
  tmp_guard.armed = false;
  return result;
}

}  // namespace marine_tiled_raster_store
