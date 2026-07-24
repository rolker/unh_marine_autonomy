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

#ifndef S102__VDATUM_PROVIDER_HPP_
#define S102__VDATUM_PROVIDER_HPP_

#include <string>
#include <vector>

#include <marine_vertical_datum/datum_config.hpp>
#include <marine_vertical_datum/vdatum_query.hpp>

#include "s102/convert.hpp"

/// @file
/// @brief Adapter binding the converter's `VerticalDatumProvider` seam to the
/// `marine_vertical_datum` library (#274): geoid + VDatum grids and the
/// full precedence chain (override polygons → VDatum → fallback polygons).

namespace marine_bathymetry_store::s102
{

/// @brief Configuration for the marine_vertical_datum-backed provider.
struct VDatumProviderConfig
{
  std::string geoid_grid;          ///< geoid grid path (see #274 README)
  std::string vdatum_grid_dir;     ///< regional VDatum .gtx directory
  std::string datum_config_path;   ///< optional polygon config ("" = none)
};

/// @brief `VerticalDatumProvider` backed by marine_vertical_datum.
class MarineVerticalDatumProvider : public VerticalDatumProvider
{
public:
  /// @throws std::runtime_error if @p config.datum_config_path is non-empty
  /// but unloadable.
  explicit MarineVerticalDatumProvider(const VDatumProviderConfig & config);

  /// @brief MLLW ellipsoidal height via the library's precedence chain.
  ///
  /// The importer intentionally passes no lake-datum override — a lake
  /// import is not an S-102 use case; polygon overrides come from
  /// @p datum_config_path.
  std::optional<double> mllwHeight(double lat, double lon) const override;

private:
  marine_vertical_datum::VDatumQueryFn query_;
  std::vector<marine_vertical_datum::DatumEntry> entries_;
};

}  // namespace marine_bathymetry_store::s102

#endif  // S102__VDATUM_PROVIDER_HPP_
