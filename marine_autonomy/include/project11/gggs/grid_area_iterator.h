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
