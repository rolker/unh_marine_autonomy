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

#include "marine_mbes_backscatter_store/registry.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

/// @file
/// @brief `SourceRegistry` implementation: in-memory interning plus atomic
/// `registry.json` persistence (ADR-0005 D2/D8). Mirrors
/// `marine_bathymetry_store::SourceRegistry`; `marine_backscatter::writeRegistry`
/// is write-only / single-source and does not cover the load/intern path here.

namespace marine_mbes_backscatter_store
{

namespace
{
constexpr const char * kRegistryFile = "registry.json";
constexpr const char * kRegistryTmpFile = "registry.json.tmp";

nlohmann::json recordToJson(uint16_t index, const SourceRecord & r)
{
  return nlohmann::json{
    {"index", index},
    {"source_id", r.source_id},
    {"platform", r.platform},
    {"sensor", r.sensor},
    {"sensor_class", r.sensor_class},
    {"campaign", r.campaign},
    {"calibration_ref", r.calibration_ref},
  };
}

/// Read a string field, defaulting to empty if absent (forward-compatible load).
std::string jsonStr(const nlohmann::json & j, const char * key)
{
  const auto it = j.find(key);
  return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string{};
}
}  // namespace

uint16_t SourceRegistry::registerSource(const SourceRecord & record)
{
  const auto existing = by_source_id_.find(record.source_id);
  if (existing != by_source_id_.end()) {
    return existing->second;
  }
  // Indices are 1-based (0 reserved); the next index is size() + 1.
  if (by_index_.size() >= std::numeric_limits<uint16_t>::max()) {
    throw std::overflow_error("SourceRegistry: exhausted all 65535 source indices");
  }
  const uint16_t index = static_cast<uint16_t>(by_index_.size() + 1);
  by_index_.push_back(record);
  by_source_id_.emplace(record.source_id, index);
  return index;
}

std::optional<SourceRecord> SourceRegistry::lookup(uint16_t index) const
{
  if (index == kUnset || index > by_index_.size()) {
    return std::nullopt;
  }
  return by_index_[static_cast<std::size_t>(index) - 1];
}

void SourceRegistry::saveRegistry(const std::string & store_root_dir) const
{
  namespace fs = std::filesystem;
  fs::create_directories(store_root_dir);

  nlohmann::json sources = nlohmann::json::array();
  for (std::size_t i = 0; i < by_index_.size(); ++i) {
    sources.push_back(recordToJson(static_cast<uint16_t>(i + 1), by_index_[i]));
  }
  const nlohmann::json doc{
    {"version", 1},
    {"sources", std::move(sources)},
  };

  const fs::path tmp = fs::path(store_root_dir) / kRegistryTmpFile;
  const fs::path final = fs::path(store_root_dir) / kRegistryFile;
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      throw std::runtime_error("SourceRegistry::saveRegistry: could not open " + tmp.string());
    }
    out << doc.dump(2) << '\n';
    out.flush();
    if (!out) {
      throw std::runtime_error("SourceRegistry::saveRegistry: write failed for " + tmp.string());
    }
  }
  // Atomic publish: rename over the existing registry. rename() on the same
  // filesystem is atomic, so a reader sees either the old or the new file whole.
  fs::rename(tmp, final);
}

void SourceRegistry::loadRegistry(const std::string & store_root_dir)
{
  namespace fs = std::filesystem;
  const fs::path path = fs::path(store_root_dir) / kRegistryFile;
  if (!fs::is_regular_file(path)) {
    return;   // fresh store: no registry yet
  }

  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("SourceRegistry::loadRegistry: could not open " + path.string());
  }
  nlohmann::json doc;
  try {
    in >> doc;
  } catch (const nlohmann::json::parse_error & e) {
    throw std::runtime_error(
            "SourceRegistry::loadRegistry: malformed " + path.string() + ": " + e.what());
  }

  by_index_.clear();
  by_source_id_.clear();
  const auto sources = doc.find("sources");
  if (sources == doc.end() || !sources->is_array()) {
    return;   // empty / sourceless registry
  }
  // Records carry an explicit "index"; validate them against the expected
  // position (1-based, contiguous, monotonically increasing by +1).  A
  // reordered, sparse, or duplicate hand-edited registry.json would produce
  // mismatches between the stored index and the reconstructed maps — silently
  // delivering wrong provenance.  Reject early with a clear error.
  for (const auto & entry : *sources) {
    SourceRecord r{
      jsonStr(entry, "source_id"), jsonStr(entry, "platform"),
      jsonStr(entry, "sensor"), jsonStr(entry, "sensor_class"),
      jsonStr(entry, "campaign"), jsonStr(entry, "calibration_ref")};
    const uint16_t expected = static_cast<uint16_t>(by_index_.size() + 1);
    const auto idx_it = entry.find("index");
    if (idx_it == entry.end() || !idx_it->is_number_unsigned()) {
      throw std::runtime_error(
              "SourceRegistry::loadRegistry: entry for source_id=\"" + r.source_id +
              "\" is missing a valid \"index\" field (expected " +
              std::to_string(expected) + ")");
    }
    const uint16_t stored = idx_it->get<uint16_t>();
    if (stored != expected) {
      throw std::runtime_error(
              "SourceRegistry::loadRegistry: entry for source_id=\"" + r.source_id +
              "\" has stored index " + std::to_string(stored) +
              " but expected " + std::to_string(expected) +
              " (indices must be contiguous from 1; the registry may have been"
              " reordered, has gaps, or contains duplicate entries)");
    }
    by_index_.push_back(r);
    by_source_id_.emplace(r.source_id, expected);
  }
}

}  // namespace marine_mbes_backscatter_store
