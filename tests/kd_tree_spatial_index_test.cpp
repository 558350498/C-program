#include "kd_tree_spatial_index.h"

#include <cassert>

int main() {
  KdTreeSpatialIndex index;

  assert(index.size() == 0U);
  assert(index.radius_search(Point(0.0, 0.0, -1), 1.0).empty());
  assert(index.radius_search(Point(0.0, 0.0, -1), -1.0).empty());
  assert(!index.erase(42));

  assert(index.upsert(Point(0.0, 0.0, 1)));
  assert(index.upsert(Point(2.0, 0.0, 2)));
  assert(index.upsert(Point(4.0, 0.0, 3)));
  assert(index.size() == 3U);

  auto nearby = index.radius_search(Point(1.0, 0.0, 100), 1.2);
  assert(nearby.size() == 2U);

  assert(index.upsert(Point(10.0, 0.0, 2)));
  assert(index.size() == 3U);
  nearby = index.radius_search(Point(2.0, 0.0, 101), 0.5);
  assert(nearby.empty());
  nearby = index.radius_search(Point(10.0, 0.0, 102), 0.5);
  assert(nearby.size() == 1U);
  assert(nearby.front().id == 2);

  assert(index.erase(1));
  assert(index.size() == 2U);
  assert(!index.erase(1));

  index.rebuild({Point(1.0, 1.0, 7), Point(2.0, 2.0, 8), Point(3.0, 3.0, 7)});
  assert(index.size() == 2U);
  nearby = index.radius_search(Point(3.0, 3.0, 103), 0.2);
  assert(nearby.size() == 1U);
  assert(nearby.front().id == 7);

  index.clear();
  assert(index.size() == 0U);
  assert(index.radius_search(Point(0.0, 0.0, 104), 100.0).empty());

  return 0;
}
