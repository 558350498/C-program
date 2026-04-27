#pragma once

#include "spatial_index.h"

#include <memory>

class KdTreeSpatialIndex : public ISpatialIndex {
public:
  KdTreeSpatialIndex();
  ~KdTreeSpatialIndex() override;

  bool upsert(const Point &point) override;
  bool erase(int id) override;
  std::vector<Point> radius_search(const Point &center,
                                   double radius) const override;
  void rebuild(const std::vector<Point> &points) override;
  std::size_t size() const override;
  void clear() override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
