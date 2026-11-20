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

// Internal to the package (not installed): registers GDAL's drivers once per
// process for tile_io.cpp and export_geotiff.cpp.

#ifndef GDAL_REGISTRATION_HPP_
#define GDAL_REGISTRATION_HPP_

#include <gdal.h>

namespace marine_tiled_raster_store
{

/// Register GDAL's drivers once per process, safely from any thread.
///
/// `GDALAllRegister()` is not safe to call concurrently: with GDAL 3.12 two
/// threads calling it race inside `GDALDriverManager::AutoLoadDrivers` even
/// after the drivers are registered (heap corruption under concurrent
/// `exportGeoTiff()`, #418 / #415). A function-local static is initialised
/// exactly once, and other threads wait for it (C++11), so this is the only
/// call the store makes.
///
/// The cost is that registration is once per process, not per operation: a
/// host that calls `GDALDestroyDriverManager()` and then uses the store again
/// gets "GTiff driver unavailable" (or a failed open) instead of a fresh
/// registration, unless it calls `GDALAllRegister()` itself, from one thread,
/// first.
inline void ensureGdalRegistered()
{
  static const bool registered = (GDALAllRegister(), true);
  (void)registered;
}

}  // namespace marine_tiled_raster_store

#endif  // GDAL_REGISTRATION_HPP_
