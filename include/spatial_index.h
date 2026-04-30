#pragma once

#include "taxi_domain.h"

#include <cstddef>
#include <vector>

class ISpatialIndex {
public:
  virtual ~ISpatialIndex() = default;

  virtual bool upsert(const Point &point) = 0;
  virtual bool erase(int id) = 0;
  virtual std::vector<Point> radius_search(const Point &center,
                                           double radius) const = 0;
  virtual void rebuild(const std::vector<Point> &points) = 0;
  virtual std::size_t size() const = 0;
  virtual void clear() = 0;
};
