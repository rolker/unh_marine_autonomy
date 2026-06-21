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

#ifndef MARINE_BATHYMETRY_STORE__EPOCH_HPP_
#define MARINE_BATHYMETRY_STORE__EPOCH_HPP_

#include <cstdint>
#include <stdexcept>
#include <string>

namespace marine_bathymetry_store
{

/// @brief A dated instance of a source layer (ADR-0002 Amendment A1).
///
/// An epoch is a **labeled key**, by convention the local acquisition date in
/// ISO-8601 form (`"2026-06-10"`). Labels must sort chronologically as plain
/// strings — ISO dates do — because epoch resolution walks a `std::map` keyed
/// by this string newest-first. The label is also used as an on-disk directory
/// name, so it must be filesystem-safe (see `validateEpochLabel`).
///
/// Epochs are never fused across days: a cell surveyed on N days keeps N
/// records, and differencing two epochs yields a change map (A1.1).
using Epoch = std::string;

/// @brief How an epoch's surface was produced (ADR-0002 §A1.2).
///
/// Ordering rule: `Replayed` (the authoritative end-of-day compaction, one
/// CUBE run over the full day) supersedes `LiveFused` (incrementally merged
/// live session snapshots) for the same epoch — **never the reverse**. Once
/// an epoch is `Replayed` it is immutable.
///
/// This is the **compaction-maturity** axis and is orthogonal to the
/// `SourceRegistry` per-cell source index (the **platform/sensor** axis,
/// ADR-0005 D2/D8): one is epoch-scoped, the other cell-scoped, and they never
/// alias. The store carries both (ADR-0002 Amendment A1, OQ1 resolution).
enum class Provenance : uint8_t
{
  LiveFused = 0,  ///< Built incrementally from live session snapshots.
  Replayed = 1,   ///< Authoritative full-day replay (compaction product).
};

/// @brief On-disk / manifest token for a provenance (`"live-fused"` / `"replayed"`).
inline std::string provenanceToken(Provenance provenance)
{
  switch (provenance) {
    case Provenance::LiveFused: return "live-fused";
    case Provenance::Replayed: return "replayed";
  }
  throw std::runtime_error("provenanceToken: unknown Provenance");
}

/// @brief Parse a provenance token; throws std::invalid_argument on anything else.
inline Provenance provenanceFromToken(const std::string & token)
{
  if (token == "live-fused") {
    return Provenance::LiveFused;
  }
  if (token == "replayed") {
    return Provenance::Replayed;
  }
  throw std::invalid_argument("provenanceFromToken: unknown token '" + token + "'");
}

/// @brief Throw std::invalid_argument unless @p label is a usable epoch key.
///
/// Requires a non-empty label of `[0-9A-Za-z._-]` characters that is neither
/// `"."` nor `".."` — i.e. safe as a single directory-name component and free
/// of path separators. (Chronological ordering is a convention the caller
/// owns: use ISO-8601 dates.)
inline void validateEpochLabel(const Epoch & label)
{
  if (label.empty() || label == "." || label == "..") {
    throw std::invalid_argument("validateEpochLabel: empty or reserved label");
  }
  for (const char c : label) {
    const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
      (c >= 'a' && c <= 'z') || c == '.' || c == '_' || c == '-';
    if (!ok) {
      throw std::invalid_argument(
              "validateEpochLabel: character '" + std::string(1, c) +
              "' not allowed in epoch label '" + label + "'");
    }
  }
}

}  // namespace marine_bathymetry_store

#endif  // MARINE_BATHYMETRY_STORE__EPOCH_HPP_
