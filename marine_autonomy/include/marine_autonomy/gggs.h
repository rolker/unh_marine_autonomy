// Copyright 2016-2020 Roland Arsenault
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the Roland Arsenault nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

/// @file gggs.h
/// @brief Umbrella header for the Global Geographic Grid System (GGGS).
///
/// Include this single header to access all GGGS types. Typical usage:
///
/// @code
/// #include "marine_autonomy/gggs.h"
///
/// // Pick a level based on desired cell resolution (~1 m cells)
/// auto level = gggs::Level::fromCellSize(1.0);
///
/// // Get the grid containing a geographic position
/// auto grid = level.gridIndex(43.07, -70.76);
///
/// // Get the specific cell within that grid
/// auto cell = level.cellIndex(gggs::geoPoint(43.07, -70.76));
///
/// // Iterate over all grids in a bounding box
/// auto from = level.gridIndex(43.0, -71.0);
/// auto to   = level.gridIndex(44.0, -70.0);
/// for (gggs::GridAreaIterator git(from, to); git.valid(); git.next()) {
///   // Iterate over cells within each grid
///   for (gggs::CellAreaIterator cit(*git); cit.valid(); cit.next()) {
///     // Process cell at cit->row(), cit->column()
///   }
/// }
/// @endcode

#ifndef PROJECT11_GGGS_H
#define PROJECT11_GGGS_H

#include "gggs/core.h"
#include "gggs/level_spec.h"
#include "gggs/grid_index.h"
#include "gggs/cell_index.h"
#include "gggs/bounds.h"
#include "gggs/level.h"
#include "gggs/grid_area_iterator.h"
#include "gggs/cell_area_iterator.h"

#endif
