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

#ifndef PROJECT11_GGGS_GRID_AREA_ITERATOR_H
#define PROJECT11_GGGS_GRID_AREA_ITERATOR_H

#include "grid_index.h"

namespace gggs
{

/// Iterates over a rectangular area of GridIndexs.
class GridAreaIterator
{
public:
  GridAreaIterator(GridIndex from, GridIndex to):
    from_(from), to_(to), current_(from)
  {
    verifySameLevel(from, to);
  }

  const GridIndex& operator*() const
  {
    return current_;
  }

  const GridIndex* operator->() const
  {
    return &current_;
  }

  bool reset()
  {
    current_ = from_;
    return valid();
  }

  bool valid() const
  {
    if(current_.valid())
    {
      return
        current_.row() >= from_.row() &&
        current_.column() >= from_.column() &&
        current_.row() <= to_.row() &&
        current_.column() <= to_.column();
    }
    return false;
  }

  bool next()
  {
    if(valid())
    {
      auto new_row = current_.row();
      auto new_column = current_.column()+1;
      if(new_column > to_.column())
      {
        new_row += 1;
        new_column = from_.column();
      }
      if(new_row > to_.row())
        current_ = GridIndex();
      else
        current_ = GridIndex(current_.level_, new_row, new_column);
    }
    return valid();
  }

private:
  GridIndex from_;
  GridIndex to_;
  GridIndex current_;
};

} // namespace gggs

#endif
