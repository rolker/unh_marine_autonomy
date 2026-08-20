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

#ifndef MARINE_BATHYMETRY_STORE__REGISTRY_HPP_
#define MARINE_BATHYMETRY_STORE__REGISTRY_HPP_

#include <string>

/// @file
/// @brief Store-level coarse provenance metadata, persisted as `registry.json` at
/// the store root (ADR-0005 #248 amendment).
///
/// This replaces the pre-#248 per-cell `SourceRegistry`/`SourceRecord` interning
/// table. For the single-platform deployment the winning source is constant across
/// a store, so per-cell provenance carried no information; instead a single
/// `StoreMetadata` record — who made this store — is written once at the root. The
/// multi-platform per-cell interning contract (ADR-0005 D2/D8) is retained in the
/// ADR and reintroduced when a second platform contributes.

namespace marine_bathymetry_store
{

/// @brief Coarse, store-level provenance recorded once at the store root.
///
/// The ADR-0005 D3 core fields that matter at store granularity, plus a
/// `survey`/`date` pair. Persisted as a flat `registry.json` sidecar (kept for
/// filename stability; its schema is now `StoreMetadata`, not the interning table).
/// All fields are optional (default empty).
struct StoreMetadata
{
  std::string platform;  ///< Contributing platform (e.g. "bizzy").
  std::string sensor;    ///< Contributing sensor (e.g. "m3").
  std::string survey;    ///< Survey / campaign id (e.g. "massabesic-2026").
  std::string date;      ///< Acquisition date (ISO-8601, e.g. "2026-06-30").
  /// Source vertical datum the import converted FROM (e.g. "mllw", #315).
  /// Stored values are always ellipsoidal; this records what the source was
  /// referenced to before conversion. Empty when the source was already
  /// ellipsoidal (or converted by constant scale/offset). Absent from
  /// pre-#315 registries, which load with it empty — an additive field, no
  /// schema-version bump.
  std::string datum;

  /// @brief True if every field is empty (nothing worth persisting).
  bool empty() const noexcept
  {
    return platform.empty() && sensor.empty() && survey.empty() &&
           date.empty() && datum.empty();
  }

  /// @brief Write `registry.json` under @p store_root_dir atomically.
  ///
  /// Writes `registry.json.tmp` then `std::filesystem::rename()`s it over
  /// `registry.json`, so a crash mid-write never leaves a partial file. Creates
  /// @p store_root_dir if needed.
  /// @throws std::runtime_error / std::filesystem::filesystem_error on failure.
  void save(const std::string & store_root_dir) const;

  /// @brief Load `registry.json` from @p store_root_dir, replacing current
  ///        contents. No-op (leaves the metadata empty) if the file is absent.
  /// @throws std::runtime_error on a malformed registry file.
  void load(const std::string & store_root_dir);
};

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__REGISTRY_HPP_
