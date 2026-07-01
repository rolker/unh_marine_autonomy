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

#ifndef MARINE_MBES_BACKSCATTER_STORE__QUERY_HPP_
#define MARINE_MBES_BACKSCATTER_STORE__QUERY_HPP_

#include <cstdint>
#include <functional>
#include <optional>

#include "geographic_msgs/msg/geo_point.hpp"
#include "marine_autonomy/gggs.h"
#include "marine_mbes_backscatter_store/mbes_cell.hpp"
#include "marine_mbes_backscatter_store/mbes_store.hpp"

namespace marine_mbes_backscatter_store
{

/// @brief A backscatter value resolved from the store, tagged with its layer.
///
/// `mean` is corrected, relative MBES backscatter (ADR-0007 D2/D3);
/// `standard_error` is the confidence-scaled Welford estimate uncertainty (D4),
/// and `sample_sd` the within-node dispersion — the Welford sufficient statistics
/// (#248 amendment A.1). This is a perception / cartographic product, **not** a
/// navigation or costmap input (ADR-0007 Consequences, safety non-goal).
struct BackscatterSample
{
  float mean;             ///< Corrected relative backscatter (Welford mean).
  float standard_error;   ///< Confidence-scaled standard error of the mean.
  float sample_sd;        ///< Sample standard deviation (within-node dispersion).
  SourceLayer source;     ///< Which layer this value came from.
};

/// @brief The layer that has data at @p cell.
///
/// Walks `source_layers_by_priority` (the sole `Survey` layer since #248) and
/// returns the first with usable data (`hasData()`). `std::nullopt` means no
/// layer has backscatter here.
std::optional<BackscatterSample> bestSource(
  const MbesBackscatterStore & store, const gggs::CellIndex & cell);

/// @brief Visit every GGGS cell overlapping the geographic box, with its best source.
///
/// Iterates the cells covered by the lat/lon box [@p minimum, @p maximum] at the
/// store's level and invokes @p visitor with each `CellIndex` and its
/// `bestSource` result (`std::nullopt` = no data). Region form of `bestSource`;
/// work is bounded by the requested box, so callers control cost.
void forEachCellBestSource(
  const MbesBackscatterStore & store,
  const geographic_msgs::msg::GeoPoint & minimum,
  const geographic_msgs::msg::GeoPoint & maximum,
  const std::function<void(const gggs::CellIndex &,
  const std::optional<BackscatterSample> &)> & visitor);

}  // namespace marine_mbes_backscatter_store

#endif  // MARINE_MBES_BACKSCATTER_STORE__QUERY_HPP_
