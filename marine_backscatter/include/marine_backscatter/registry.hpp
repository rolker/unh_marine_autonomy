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

#ifndef MARINE_BACKSCATTER__REGISTRY_HPP_
#define MARINE_BACKSCATTER__REGISTRY_HPP_

#include <cstdint>
#include <string>

/// @file
/// @brief The ADR-0005 provenance registry sidecar (shared across backscatter layers).

namespace marine_backscatter
{

/// @brief Escape a string for embedding in a JSON value (operator-typed registry
///   fields may contain quotes / backslashes / control chars).
std::string jsonEscape(const std::string & s);

/// @brief Write the `registry.json` sidecar resolving the per-cell **local source
///   index** to its provenance record (ADR-0005). v1 is single-source, write-once.
///   TODO(#179): multi-source / reimport over the same dir must MERGE append-only
///   (ADR-0005 D8), not overwrite.
void writeRegistry(
  const std::string & path, std::uint16_t source_id, const std::string & platform,
  const std::string & sensor, const std::string & sensor_class, const std::string & campaign);

}  // namespace marine_backscatter

#endif  // MARINE_BACKSCATTER__REGISTRY_HPP_
