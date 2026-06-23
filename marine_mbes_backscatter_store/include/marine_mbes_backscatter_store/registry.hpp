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

#ifndef MARINE_MBES_BACKSCATTER_STORE__REGISTRY_HPP_
#define MARINE_MBES_BACKSCATTER_STORE__REGISTRY_HPP_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

/// @file
/// @brief Store-wide source registry: maps each per-cell `source_index` to a
/// provenance record (ADR-0005 D2/D8). Persisted as `registry.json` at the store
/// root with an atomic write-then-rename.
///
/// This is the MBES store's **own** registry, mirroring `marine_bathymetry_store`'s
/// `SourceRegistry`: `marine_backscatter::writeRegistry` is write-only and
/// single-source (`writeRegistry(path, source_id, platform, sensor, sensor_class,
/// campaign)`), so it cannot back the multi-record load/intern path the store and
/// its tests need. The schema matches ADR-0005 D3 with an MBES-specific
/// `calibration_ref` extension (empty until a beam-pattern calibration exists —
/// the M3 is AGC/relative, ADR-0007 D6).

namespace marine_mbes_backscatter_store
{

/// @brief One source's provenance record.
///
/// `source_id` is the origin-namespaced, never-reused wide id (ADR-0005 D4); the
/// remaining fields are the ADR-0005 D3 core schema plus the MBES-specific
/// `calibration_ref` extension. The `source_id` is the **identity** of a record —
/// `registerSource` is idempotent on it (re-registering the same `source_id`
/// returns the existing index).
struct SourceRecord
{
  std::string source_id;        ///< Origin-namespaced wide id (ADR-0005 D4).
  std::string platform;         ///< Contributing platform (e.g. "bizzyboat").
  std::string sensor;           ///< Contributing sensor (e.g. "kongsberg-m3").
  std::string sensor_class;     ///< Sensor class (e.g. "mbes-backscatter").
  std::string campaign;         ///< Acquisition campaign / deployment.
  std::string calibration_ref;  ///< MBES extension: beam-pattern calibration ref.
};

/// @brief Interns `SourceRecord`s to small local indices and persists them.
///
/// Index 0 is reserved as the **no-data/unset sentinel** (ADR-0005 D4) and is
/// never assigned to a real record; the first registered source gets index 1.
/// Indices are assigned densely in registration order and are stable for the
/// life of the store directory (they are the values written into the per-cell
/// source-index tiles, so reassigning them would corrupt existing tiles).
class SourceRegistry
{
public:
  /// @brief The reserved no-data/unset source index.
  static constexpr uint16_t kUnset = 0;

  /// @brief Intern @p record, returning its local index; idempotent on `source_id`.
  ///
  /// If a record with the same `source_id` is already registered, its existing
  /// index is returned (the rest of @p record is ignored). Otherwise a new index
  /// (the next unused value, starting at 1) is assigned and returned.
  ///
  /// @note A registered source is held only in memory until persisted: call the
  ///   store `save()` (which invokes `saveRegistry()`), or `saveRegistry()`
  ///   directly, to write it.
  /// @throws std::overflow_error if all 65535 real indices are exhausted.
  uint16_t registerSource(const SourceRecord & record);

  /// @brief Look up the record for @p index, or `std::nullopt` if unregistered
  ///        (including `index == kUnset`).
  std::optional<SourceRecord> lookup(uint16_t index) const;

  /// @brief Number of registered sources (excludes the reserved index 0).
  std::size_t size() const noexcept {return by_index_.size();}

  /// @brief Whether any source is registered.
  bool empty() const noexcept {return by_index_.empty();}

  /// @brief Write `registry.json` under @p store_root_dir atomically.
  ///
  /// Writes `registry.json.tmp` then `std::filesystem::rename()`s it over
  /// `registry.json`, so a crash mid-write never leaves a partial file. Creates
  /// @p store_root_dir if needed.
  /// @throws std::runtime_error / std::filesystem::filesystem_error on failure.
  void saveRegistry(const std::string & store_root_dir) const;

  /// @brief Load `registry.json` from @p store_root_dir, replacing current
  ///        contents. No-op (leaves the registry empty) if the file is absent.
  /// @throws std::runtime_error on a malformed registry file.
  void loadRegistry(const std::string & store_root_dir);

private:
  /// index (1-based) -> record. by_index_[i] is the record for index i+1.
  std::vector<SourceRecord> by_index_;
  /// source_id -> local index, for idempotent registration / lookup.
  std::map<std::string, uint16_t> by_source_id_;
};

}  // namespace marine_mbes_backscatter_store

#endif  // MARINE_MBES_BACKSCATTER_STORE__REGISTRY_HPP_
