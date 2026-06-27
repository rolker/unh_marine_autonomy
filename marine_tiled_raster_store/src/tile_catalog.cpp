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

#include "marine_tiled_raster_store/tile_catalog.hpp"

namespace marine_tiled_raster_store
{

// ---- TileCatalogBuilder ----------------------------------------------------

void TileCatalogBuilder::update(const gggs::GridIndex & index, TileVersion version)
{
  auto it = versions_.find(index);
  if (it == versions_.end()) {
    versions_.emplace(index, version);
  } else if (version > it->second) {
    it->second = version;  // newest-wins; an older update is ignored
  }
}

void TileCatalogBuilder::remove(const gggs::GridIndex & index)
{
  versions_.erase(index);
}

std::optional<TileVersion> TileCatalogBuilder::versionOf(const gggs::GridIndex & index) const
{
  auto it = versions_.find(index);
  if (it == versions_.end()) {
    return std::nullopt;
  }
  return it->second;
}

TileCatalog TileCatalogBuilder::buildCatalog(TileVersion generation_time) const
{
  TileCatalog catalog;
  catalog.generation_time = generation_time;
  catalog.entries.reserve(versions_.size());
  for (const auto & [index, version] : versions_) {
    catalog.entries.push_back(TileCatalogEntry{index, version});
  }
  return catalog;
}

// ---- TileCatalogReconciler -------------------------------------------------

void TileCatalogReconciler::markHave(const gggs::GridIndex & index, TileVersion version)
{
  auto it = cache_.find(index);
  if (it == cache_.end()) {
    cache_.emplace(index, version);
  } else if (version > it->second) {
    it->second = version;  // newest-wins; a reordered older arrival is ignored
  }
}

void TileCatalogReconciler::drop(const gggs::GridIndex & index)
{
  cache_.erase(index);
}

bool TileCatalogReconciler::has(const gggs::GridIndex & index) const
{
  return cache_.find(index) != cache_.end();
}

std::optional<TileVersion> TileCatalogReconciler::versionOf(const gggs::GridIndex & index) const
{
  auto it = cache_.find(index);
  if (it == cache_.end()) {
    return std::nullopt;
  }
  return it->second;
}

ReconcileResult TileCatalogReconciler::reconcile(const TileCatalog & catalog) const
{
  ReconcileResult result;

  // Index the catalog for lookup of held tiles below. A well-formed catalog has
  // unique indices; if one repeats (malformed source), keep the newest version
  // so the request/prune decisions stay conservative.
  std::map<gggs::GridIndex, TileVersion> catalog_versions;
  for (const auto & entry : catalog.entries) {
    auto it = catalog_versions.find(entry.index);
    if (it == catalog_versions.end()) {
      catalog_versions.emplace(entry.index, entry.version);
    } else if (entry.version > it->second) {
      it->second = entry.version;
    }
  }

  // Request: catalog tiles we are missing or hold at an older version.
  for (const auto & [index, version] : catalog_versions) {
    auto held = cache_.find(index);
    if (held == cache_.end() || held->second < version) {
      result.to_request.push_back(index);
    }
  }

  // Prune: held tiles absent from the catalog, but only if older than the
  // catalog's generation time — so a late/reordered catalog cannot delete a
  // just-pushed fresh tile (ADR-0008 D4b).
  for (const auto & [index, version] : cache_) {
    if (catalog_versions.find(index) == catalog_versions.end() &&
      version < catalog.generation_time)
    {
      result.to_prune.push_back(index);
    }
  }

  return result;
}

}  // namespace marine_tiled_raster_store
