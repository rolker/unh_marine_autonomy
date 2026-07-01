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

#include "marine_bathymetry_store/registry.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

/// @file
/// @brief `StoreMetadata` persistence: a flat, atomic `registry.json` sidecar
/// recording coarse store-level provenance (ADR-0005 #248 amendment).

namespace marine_bathymetry_store
{

namespace
{
constexpr const char * kRegistryFile = "registry.json";
constexpr const char * kRegistryTmpFile = "registry.json.tmp";

/// Read a string field, defaulting to empty if absent (forward-compatible load).
std::string jsonStr(const nlohmann::json & j, const char * key)
{
  const auto it = j.find(key);
  return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string{};
}
}  // namespace

void StoreMetadata::save(const std::string & store_root_dir) const
{
  namespace fs = std::filesystem;
  fs::create_directories(store_root_dir);

  const nlohmann::json doc{
    {"version", 2},
    {"platform", platform},
    {"sensor", sensor},
    {"survey", survey},
    {"date", date},
  };

  const fs::path tmp = fs::path(store_root_dir) / kRegistryTmpFile;
  const fs::path final = fs::path(store_root_dir) / kRegistryFile;
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      throw std::runtime_error("StoreMetadata::save: could not open " + tmp.string());
    }
    out << doc.dump(2) << '\n';
    out.flush();
    if (!out) {
      throw std::runtime_error("StoreMetadata::save: write failed for " + tmp.string());
    }
  }
  // Atomic publish: rename over the existing file. rename() on the same
  // filesystem is atomic, so a reader sees either the old or the new file whole.
  // On rename failure, remove the tmp file to avoid leaving an orphan.
  try {
    fs::rename(tmp, final);
  } catch (...) {
    std::error_code ec;
    fs::remove(tmp, ec);
    throw;
  }
}

void StoreMetadata::load(const std::string & store_root_dir)
{
  namespace fs = std::filesystem;
  const fs::path path = fs::path(store_root_dir) / kRegistryFile;
  if (!fs::is_regular_file(path)) {
    return;   // fresh store: no metadata yet
  }

  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("StoreMetadata::load: could not open " + path.string());
  }
  nlohmann::json doc;
  try {
    in >> doc;
  } catch (const nlohmann::json::parse_error & e) {
    throw std::runtime_error(
            "StoreMetadata::load: malformed " + path.string() + ": " + e.what());
  }

  platform = jsonStr(doc, "platform");
  sensor = jsonStr(doc, "sensor");
  survey = jsonStr(doc, "survey");
  date = jsonStr(doc, "date");
}

}  // namespace marine_bathymetry_store
