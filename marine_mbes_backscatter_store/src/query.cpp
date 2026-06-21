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

#include "marine_mbes_backscatter_store/query.hpp"

namespace marine_mbes_backscatter_store
{

namespace
{

/// Resolve a single layer's sample at a cell, or nullopt if it has no usable data.
std::optional<IntensitySample> sampleFor(
  const MbesBackscatterStore & store, SourceLayer layer, const gggs::CellIndex & cell)
{
  const auto c = store.get(layer, cell);
  if (!c || !c->hasData()) {
    return std::nullopt;
  }
  return IntensitySample{c->intensity, c->intensity_variance, c->timestamp, layer};
}

}  // namespace

std::optional<IntensitySample> bestSource(
  const MbesBackscatterStore & store, const gggs::CellIndex & cell)
{
  for (const SourceLayer layer : source_layers_by_priority) {
    if (auto sample = sampleFor(store, layer, cell)) {
      return sample;
    }
  }
  return std::nullopt;
}

void forEachCellBestSource(
  const MbesBackscatterStore & store,
  const geographic_msgs::msg::GeoPoint & minimum,
  const geographic_msgs::msg::GeoPoint & maximum,
  const std::function<void(const gggs::CellIndex &,
  const std::optional<IntensitySample> &)> & visitor)
{
  const gggs::Level & level = store.level();
  gggs::GridAreaIterator grid_it(
    level.gridIndex(minimum.latitude, minimum.longitude),
    level.gridIndex(maximum.latitude, maximum.longitude));
  for (; grid_it.valid(); grid_it.next()) {
    for (gggs::CellAreaIterator cell_it(*grid_it, minimum, maximum);
      cell_it.valid(); cell_it.next())
    {
      visitor(*cell_it, bestSource(store, *cell_it));
    }
  }
}

}  // namespace marine_mbes_backscatter_store
