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

#include "marine_backscatter/registry.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

namespace marine_backscatter
{

std::string jsonEscape(const std::string & s)
{
  std::string out;
  out.reserve(s.size() + 8);
  for (const char ch : s) {
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(ch));
          out += buf;
        } else {
          out += ch;
        }
    }
  }
  return out;
}

void writeRegistry(
  const std::string & path, std::uint16_t source_id, const std::string & platform,
  const std::string & sensor, const std::string & sensor_class, const std::string & campaign)
{
  // ADR-0005: per-cell band is the compact LOCAL index; the registry resolves it
  // to the (eventually origin-namespaced, ADR-0005 D4) global source-id + record.
  // TODO(#179): v1 writes a single-source registry write-once; multi-source / a
  // reimport over the same out_dir must MERGE append-only (ADR-0005 D8), not
  // overwrite — load existing + union before writing.
  std::ofstream r(path);
  r << "{\n  \"version\": 1,\n  \"sources\": {\n    \"" << source_id << "\": {\n"
    << "      \"source_id\": " << source_id << ",\n"
    << "      \"platform\": \"" << jsonEscape(platform) << "\",\n"
    << "      \"sensor\": \"" << jsonEscape(sensor) << "\",\n"
    << "      \"sensor_class\": \"" << jsonEscape(sensor_class) << "\",\n"
    << "      \"campaign\": \"" << jsonEscape(campaign) << "\"\n"
    << "    }\n  }\n}\n";
}

}  // namespace marine_backscatter
