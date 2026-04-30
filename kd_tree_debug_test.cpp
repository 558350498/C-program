#include "include/kd_tree_spatial_index.h"

#include <cassert>
#include <iostream>

int main() {
  KdTreeSpatialIndex index;

  assert(index.radius_search(Point(0.0, 0.0, -1), -1.0).empty());
  assert(index.upsert(Point(1.0, 0.0, 1)));
  assert(index.upsert(Point(2.0, 0.0, 2)));
  assert(index.upsert(Point(3.0, 0.0, 3)));

  const auto nearby = index.radius_search(Point(2.0, 0.0, 100), 1.1);
  assert(nearby.size() == 3U);

  assert(index.erase(1));
  assert(index.erase(2));
  assert(index.erase(3));
  assert(index.radius_search(Point(2.0, 0.0, 101), 10.0).empty());

  std::cout << "kd_tree_spatial_index debug test passed\n";
  return 0;
}
