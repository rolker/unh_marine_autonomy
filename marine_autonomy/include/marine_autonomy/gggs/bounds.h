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

#ifndef PROJECT11_GGGS_BOUNDS_H
#define PROJECT11_GGGS_BOUNDS_H

#include "grid_index.h"

namespace gggs
{

class GridBounds
{
public:
  bool valid() const
  {
    return min_.valid() && max_.valid() && min_.level() == max_.level();
  }

  void expand(const GridIndex& index)
  {
    if(index.valid())
    {
      if(valid())
      {
        min_ = min(min_, index);
        max_ = max(max_, index);
      }
      else
      {
        min_ = index;
        max_ = index;
      }
    }
  }

  const GridIndex& minimum() const
  {
    return min_;
  }

  const GridIndex& maximum() const
  {
    return max_;
  }

  uint32_t gridRowCount() const
  {
    return 1+max_.row()-min_.row();
  }

  uint32_t gridColumnCount() const
  {
    if(min_.valid())
      return 1+max_.column()-min_.column();
    return 0;
  }

  uint64_t cellRowCount() const
  {
    return gridRowCount()*cell_rows_per_grid;
  }

  uint64_t cellColumnCount() const
  {
    return gridColumnCount()*cell_columns_per_grid;
  }

  friend std::ostream& operator<< (std::ostream& stream, const GridBounds& bounds)
  {
    stream << "min: " << bounds.min_ << " max: " << bounds.max_;
    return stream;
  }


private:
  GridIndex min_;
  GridIndex max_;

};

}

#endif
