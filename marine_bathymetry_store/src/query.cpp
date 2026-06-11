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

#include "marine_bathymetry_store/query.hpp"

#include <cmath>

namespace marine_bathymetry_store
{

namespace
{

/// Resolve a single layer's sample at a cell, or nullopt if it has no usable data.
std::optional<DepthSample> sampleFor(
  const BathymetryStore & store, SourceLayer layer, const gggs::CellIndex & cell)
{
  const auto c = store.get(layer, cell);
  if (!c || !c->hasData()) {
    return std::nullopt;
  }
  return DepthSample{c->depth, c->uncertainty, c->timestamp, layer};
}

}  // namespace

std::optional<DepthSample> bestSource(
  const BathymetryStore & store, const gggs::CellIndex & cell)
{
  for (const SourceLayer layer : source_layers_by_priority) {
    if (auto sample = sampleFor(store, layer, cell)) {
      return sample;
    }
  }
  return std::nullopt;
}

std::optional<DepthSample> shallowestReliable(
  const BathymetryStore & store, const gggs::CellIndex & cell, double max_uncertainty)
{
  std::optional<DepthSample> shallowest;
  for (const SourceLayer layer : source_layers_by_priority) {
    const auto sample = sampleFor(store, layer, cell);
    if (!sample) {
      continue;
    }
    // A NaN uncertainty is never reliable; otherwise require it within tolerance.
    if (std::isnan(sample->uncertainty) || sample->uncertainty > max_uncertainty) {
      continue;
    }
    // depth is ellipsoidal height (up-positive): shallower == greater height.
    if (!shallowest || sample->depth > shallowest->depth) {
      shallowest = sample;
    }
  }
  return shallowest;
}

void forEachCellBestSource(
  const BathymetryStore & store,
  const geographic_msgs::msg::GeoPoint & minimum,
  const geographic_msgs::msg::GeoPoint & maximum,
  const std::function<void(const gggs::CellIndex &,
  const std::optional<DepthSample> &)> & visitor)
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

}  // namespace marine_bathymetry_store
