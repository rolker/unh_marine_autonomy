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
