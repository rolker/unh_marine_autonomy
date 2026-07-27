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

#include "s102/fetch.hpp"

#include <cpl_vsi.h>
#include <openssl/evp.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace marine_bathymetry_store::s102
{

namespace
{

constexpr const char * kRegistryName = "tiles.json";

/// Reduce an identifier to a single filesystem-safe path component: keep
/// [A-Za-z0-9._-], map anything else (including any stray separators) to '_'.
std::string sanitizeComponent(const std::string & id)
{
  std::string out;
  out.reserve(id.size());
  for (const char ch : id) {
    const bool safe = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
      (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
    out.push_back(safe ? ch : '_');
  }
  return out;
}

/// Map a catalog URL / test path to something VSIFOpenL can read.
std::string vsiReadablePath(const std::string & url)
{
  if (url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0) {
    return "/vsicurl/" + url;
  }
  if (url.rfind("file://", 0) == 0) {
    return url.substr(7);
  }
  return url;  // plain local path (tests)
}

nlohmann::json loadRegistry(const fs::path & path)
{
  if (!fs::exists(path)) {
    return nlohmann::json::object();
  }
  std::ifstream in(path);
  nlohmann::json registry;
  try {
    in >> registry;
  } catch (const nlohmann::json::exception & e) {
    // A corrupt registry is surfaced, never silently rebuilt over — the
    // operator decides whether to delete it (cheap: tiles refetch).
    throw std::runtime_error(
            "corrupt tile-cache registry " + path.string() + ": " + e.what());
  }
  if (!registry.is_object()) {
    throw std::runtime_error(
            "corrupt tile-cache registry " + path.string() + ": not an object");
  }
  return registry;
}

void saveRegistryAtomic(const fs::path & path, const nlohmann::json & registry)
{
  const fs::path tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp);
    out << registry.dump(2) << "\n";
    if (!out) {
      throw std::runtime_error("cannot write " + tmp.string());
    }
  }
  fs::rename(tmp, path);
}

}  // namespace

std::string sha256File(const std::string & path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot read " + path);
  }
  const std::unique_ptr<EVP_MD_CTX, decltype(& EVP_MD_CTX_free)> ctx(
    EVP_MD_CTX_new(), &EVP_MD_CTX_free);
  if (ctx == nullptr || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) {
    throw std::runtime_error("EVP_DigestInit failed");
  }
  std::array<char, 1 << 16> buffer;
  while (in) {
    in.read(buffer.data(), buffer.size());
    const auto got = in.gcount();
    if (got > 0 &&
      EVP_DigestUpdate(ctx.get(), buffer.data(), static_cast<size_t>(got)) != 1)
    {
      throw std::runtime_error("EVP_DigestUpdate failed");
    }
  }
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest;
  unsigned int digest_len = 0;
  if (EVP_DigestFinal_ex(ctx.get(), digest.data(), &digest_len) != 1) {
    throw std::runtime_error("EVP_DigestFinal failed");
  }
  std::string hex(static_cast<size_t>(digest_len) * 2, '0');
  static constexpr char kHex[] = "0123456789abcdef";
  for (unsigned int i = 0; i < digest_len; ++i) {
    hex[2 * i] = kHex[digest[i] >> 4];
    hex[2 * i + 1] = kHex[digest[i] & 0x0f];
  }
  return hex;
}

TileCache::TileCache(const std::string & cache_dir)
: cache_dir_(cache_dir)
{
  const fs::path tiles_dir = fs::path(cache_dir_) / "tiles";
  fs::create_directories(tiles_dir);
  // Sweep orphaned ".part" temporaries left by a download killed mid-stream:
  // each fetch streams to "<name>.part" and only renames a verified payload
  // into place, so any ".part" here is stale and safe to drop at open.
  for (const auto & entry : fs::directory_iterator(tiles_dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".part") {
      std::error_code ec;
      fs::remove(entry.path(), ec);  // best-effort; a leftover is harmless
    }
  }
  // Validate an existing registry up front so corruption fails the run at
  // open, not mid-fetch.
  loadRegistry(fs::path(cache_dir_) / kRegistryName);
}

FetchResult TileCache::fetch(const TileRecord & record, bool offline)
{
  const fs::path registry_path = fs::path(cache_dir_) / kRegistryName;
  nlohmann::json registry = loadRegistry(registry_path);

  // filename() already drops any directory components (so no "../" can survive
  // into the on-disk name), but a URL may still carry a "?query"/"#fragment"
  // tail — strip it before we read the extension.
  std::string basename = fs::path(record.url).filename().string();
  basename = basename.substr(0, basename.find_first_of("?#"));
  if (basename.empty()) {
    throw std::runtime_error("catalog URL has no filename: " + record.url);
  }
  // Key the on-disk file by tile_id (unique per tile), NOT the URL basename:
  // two distinct tile_ids whose URLs happen to share a filename would otherwise
  // collide on one cache file (self-healing via the SHA check below, but it
  // forces a needless refetch every access). Preserve the URL's extension so
  // GDAL's driver sniff still recognizes the payload.
  const std::string cache_name =
    sanitizeComponent(record.tile_id) + fs::path(basename).extension().string();
  const fs::path local = fs::path(cache_dir_) / "tiles" / cache_name;

  // Cache hit requires the registry entry at the same issuance AND a payload
  // that still hashes to the catalog digest — corruption is caught on every
  // access, not only at download time.
  if (registry.contains(record.tile_id)) {
    const auto & entry = registry[record.tile_id];
    if (entry.value("issuance", "") == record.issuance &&
      entry.value("sha256", "") == record.sha256 &&
      fs::exists(local) && sha256File(local.string()) == record.sha256)
    {
      return {local.string(), false};
    }
  }
  if (offline) {
    throw std::runtime_error(
            "offline: tile " + record.tile_id + " (issuance '" + record.issuance +
            "') is not in the cache at " + cache_dir_);
  }

  // Download via VSI so https (/vsicurl/), file:// and plain paths share one
  // path. Stream to a temp name; only a verified payload lands at `local`.
  const std::string src = vsiReadablePath(record.url);
  VSILFILE * in = VSIFOpenL(src.c_str(), "rb");
  if (in == nullptr) {
    throw std::runtime_error("cannot open " + record.url);
  }
  const fs::path tmp = local.string() + ".part";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      VSIFCloseL(in);
      throw std::runtime_error("cannot write " + tmp.string());
    }
    std::vector<char> buffer(1 << 20);
    size_t got = 0;
    while ((got = VSIFReadL(buffer.data(), 1, buffer.size(), in)) > 0) {
      out.write(buffer.data(), static_cast<std::streamsize>(got));
    }
    const bool read_error = VSIFEofL(in) == 0;
    VSIFCloseL(in);
    if (read_error || !out) {
      out.close();
      fs::remove(tmp);
      throw std::runtime_error("short read/write fetching " + record.url);
    }
  }

  const std::string actual = sha256File(tmp.string());
  if (actual != record.sha256) {
    fs::remove(tmp);
    throw std::runtime_error(
            "SHA256 mismatch for " + record.tile_id + " from " + record.url +
            ": expected " + record.sha256 + ", got " + actual);
  }
  fs::rename(tmp, local);

  registry[record.tile_id] = {
    {"issuance", record.issuance},
    {"sha256", record.sha256},
    {"filename", cache_name},
  };
  saveRegistryAtomic(registry_path, registry);
  return {local.string(), true};
}

}  // namespace marine_bathymetry_store::s102
