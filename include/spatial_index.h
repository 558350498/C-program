#pragma once

#include "taxi_domain.h"

#include <cstddef>
#include <vector>

struct SpatialQueryResult {
  int id;
  double distance_sq;

  SpatialQueryResult() : id(-1), distance_sq(0.0) {}

  SpatialQueryResult(int id_value, double distance_sq_value)
      : id(id_value), distance_sq(distance_sq_value) {}
};

class ISpatialIndex {
public:
  virtual ~ISpatialIndex() = default;

  virtual bool upsert(const Point &point) = 0;
  virtual bool erase(int id) = 0;
  virtual std::vector<Point> radius_search(const Point &center,
                                           double radius) const = 0;
  virtual std::vector<SpatialQueryResult>
  radius_query(const Point &center, double radius) const = 0;
  virtual std::vector<SpatialQueryResult>
  nearest_k(const Point &center, std::size_t k) const = 0;
  virtual void rebuild(const std::vector<Point> &points) = 0;
  virtual std::size_t size() const = 0;
  virtual void clear() = 0;
};
