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

#include <gtest/gtest.h>

#include "marine_backscatter/quality.hpp"

using marine_backscatter::grazingQuality;

TEST(Quality, PeaksMidSwathFloorsAtOne)
{
  // 45 deg grazing (altitude == ground range) -> sin(90) = 1 -> ~full scale.
  EXPECT_GT(grazingQuality(10.0, 10.0), 65000u);
  // Near nadir (ground << altitude) and far range (ground >> altitude) collapse
  // to ~0 but are floored to 1 (a real return, never no-data).
  EXPECT_GE(grazingQuality(10.0, 0.1), 1u);
  EXPECT_GE(grazingQuality(0.1, 100.0), 1u);
  // Mid-swath beats both near-nadir and far.
  EXPECT_GT(grazingQuality(10.0, 10.0), grazingQuality(10.0, 0.5));
  EXPECT_GT(grazingQuality(10.0, 10.0), grazingQuality(10.0, 200.0));
}
