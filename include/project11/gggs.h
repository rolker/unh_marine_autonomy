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

// based on "A global geographic grid system for visualizing bathymetry" by Colin Ware et al.
// https://gi.copernicus.org/articles/9/375/2020/
//
// A system for indexing tiles in a quad tree using geographic coordinates.
// At level 0, tiles are 8x8 degrees for most of the earth. Tiles at or above
// 72 degrees of latitude cover 24 degrees of longitude while tiles at or
// above 80 degrees of latitude cover 72 degrees of longitude. Similar
// scale changes are also applied towards the south pole.

#endif
