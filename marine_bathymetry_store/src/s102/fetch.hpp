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

#ifndef S102__FETCH_HPP_
#define S102__FETCH_HPP_

#include <string>

#include "s102/catalog.hpp"

/// @file
/// @brief S-102 fetch stage (#278): SHA256-verified tile downloads into a
/// local corpus cache with an idempotency registry.
///
/// Cache layout: `<cache>/tiles/<basename(url)>` for tile payloads plus
/// `<cache>/tiles.json` (atomic temp+rename writes) recording
/// `tile_id → {issuance, sha256, filename}`. The sidecar is deliberately NOT
/// named `registry.json` — that name is taken by the store's own
/// StoreMetadata sidecar and the two must never be confused.
///
/// Fetch is the only network stage; everything downstream works offline from
/// the cache (ADR-0010 boat-as-offline-tooling).

namespace marine_bathymetry_store::s102
{

/// @brief SHA256 of a file's contents as lowercase hex.
/// @throws std::runtime_error if the file can't be read.
std::string sha256File(const std::string & path);

/// @brief Outcome of a single tile fetch.
struct FetchResult
{
  std::string local_path;   ///< cached tile payload
  bool downloaded = false;  ///< false = cache hit (verified), true = fetched now
};

/// @brief SHA256-verified tile cache.
class TileCache
{
public:
  /// @brief Open (creating if needed) the cache at @p cache_dir.
  /// @throws std::runtime_error if the directory can't be created or
  /// `tiles.json` exists but is unparseable (a corrupt registry is surfaced,
  /// never silently rebuilt over).
  explicit TileCache(const std::string & cache_dir);

  /// @brief Ensure @p record's tile is cached and verified; return its path.
  ///
  /// Cache hit: the registry lists @p record.tile_id at the same issuance
  /// AND the payload re-hashes to the catalog digest (corruption is caught
  /// on every access, not only at download time). Otherwise downloads
  /// (`https://`/`http://` via GDAL `/vsicurl/`, `file://` or plain paths
  /// for tests), verifies the digest — hard-fail on mismatch, partial file
  /// removed — and records the entry atomically.
  ///
  /// @param offline When true, never touch the network: a cache miss (or a
  /// stale/corrupt entry) is an error listing the missing tile.
  /// @throws std::runtime_error on digest mismatch, download failure, or an
  /// offline miss.
  FetchResult fetch(const TileRecord & record, bool offline = false);

  /// @brief Cache directory this instance manages.
  const std::string & dir() const {return cache_dir_;}

private:
  std::string cache_dir_;
};

}  // namespace marine_bathymetry_store::s102

#endif  // S102__FETCH_HPP_
