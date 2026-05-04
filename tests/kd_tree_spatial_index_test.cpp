#include "kd_tree_spatial_index.h"

#include <cassert>
#include <unordered_map>

int main() {
  KdTreeSpatialIndex index;

  assert(index.size() == 0U);
  assert(index.radius_search(Point(0.0, 0.0, -1), 1.0).empty());
  assert(index.radius_search(Point(0.0, 0.0, -1), -1.0).empty());
  assert(!index.erase(42));
  assert(!index.erase(-1));
  assert(!index.upsert(Point(0.0, 0.0, -7)));

  assert(index.upsert(Point(0.0, 0.0, 1)));
  assert(index.upsert(Point(2.0, 0.0, 2)));
  assert(index.upsert(Point(4.0, 0.0, 3)));
  assert(index.size() == 3U);

  auto nearby = index.radius_search(Point(1.0, 0.0, 100), 1.2);
  assert(nearby.size() == 2U);
  nearby = index.radius_search(Point(2.0, 0.0, 100), 0.0);
  assert(nearby.size() == 1U);
  assert(nearby.front().id == 2);
  nearby = index.radius_search(Point(0.0, 0.0, 100), 2.0);
  assert(nearby.size() == 2U);

  auto query = index.radius_query(Point(1.0, 0.0, 100), 3.0);
  assert(query.size() == 3U);
  assert(query[0].id == 1);
  assert(query[0].distance_sq == 1.0);
  assert(query[1].id == 2);
  assert(query[1].distance_sq == 1.0);
  assert(query[2].id == 3);
  assert(query[2].distance_sq == 9.0);
  assert(index.radius_query(Point(1.0, 0.0, 100), -1.0).empty());

  auto nearest = index.nearest_k(Point(1.1, 0.0, 100), 2);
  assert(nearest.size() == 2U);
  assert(nearest[0].id == 2);
  assert(nearest[1].id == 1);
  assert(index.nearest_k(Point(1.1, 0.0, 100), 0).empty());
  nearest = index.nearest_k(Point(1.1, 0.0, 100), 99);
  assert(nearest.size() == 3U);

  assert(index.upsert(Point(10.0, 0.0, 2)));
  assert(index.size() == 3U);
  nearby = index.radius_search(Point(2.0, 0.0, 101), 0.5);
  assert(nearby.empty());
  nearby = index.radius_search(Point(10.0, 0.0, 102), 0.5);
  assert(nearby.size() == 1U);
  assert(nearby.front().id == 2);
  query = index.radius_query(Point(2.0, 0.0, 101), 0.5);
  assert(query.empty());
  nearest = index.nearest_k(Point(10.0, 0.0, 102), 1);
  assert(nearest.size() == 1U);
  assert(nearest.front().id == 2);
  assert(index.upsert(Point(12.0, 0.0, 2)));
  assert(index.upsert(Point(2.0, 0.0, 2)));
  assert(index.size() == 3U);
  nearby = index.radius_search(Point(12.0, 0.0, 102), 0.5);
  assert(nearby.empty());
  nearby = index.radius_search(Point(2.0, 0.0, 102), 0.5);
  assert(nearby.size() == 1U);
  assert(nearby.front().id == 2);

  assert(index.erase(1));
  assert(index.size() == 2U);
  assert(!index.erase(1));
  nearby = index.radius_search(Point(0.0, 0.0, 102), 0.1);
  assert(nearby.empty());
  query = index.radius_query(Point(0.0, 0.0, 102), 100.0);
  for (const auto &result : query) {
    assert(result.id != 1);
  }
  assert(index.upsert(Point(-5.0, 0.0, 1)));
  assert(index.size() == 3U);
  nearby = index.radius_search(Point(-5.0, 0.0, 102), 0.1);
  assert(nearby.size() == 1U);
  assert(nearby.front().id == 1);
  assert(index.erase(1));
  assert(!index.erase(1));

  for (int i = 0; i < 32; ++i) {
    assert(index.upsert(Point(static_cast<double>(i), 1.0, 1000 + i)));
  }
  for (int i = 0; i < 32; i += 2) {
    assert(index.erase(1000 + i));
  }

  nearby = index.radius_search(Point(16.0, 1.0, 200), 20.0);
  std::unordered_map<int, bool> found_ids;
  for (const auto &point : nearby) {
    found_ids[point.id] = true;
  }
  assert(!found_ids[1000]);
  assert(found_ids[1001]);
  assert(!found_ids[1016]);
  assert(found_ids[1017]);

  index.rebuild({Point(-1.0, -1.0, -9), Point(1.0, 1.0, 7),
                 Point(2.0, 2.0, 8), Point(3.0, 3.0, 7)});
  assert(index.size() == 2U);
  nearby = index.radius_search(Point(3.0, 3.0, 103), 0.2);
  assert(nearby.size() == 1U);
  assert(nearby.front().id == 7);
  query = index.radius_query(Point(3.0, 3.0, 103), 0.2);
  assert(query.size() == 1U);
  assert(query.front().id == 7);
  nearest = index.nearest_k(Point(3.0, 3.0, 103), 5);
  assert(nearest.size() == 2U);
  assert(nearest[0].id == 7);
  assert(nearest[1].id == 8);

  nearby = index.radius_search(Point(1.0, 1.0, 103), 0.2);
  assert(nearby.empty());
  nearby = index.radius_search(Point(-1.0, -1.0, 104), 0.2);
  assert(nearby.empty());

  index.rebuild({Point(10.0, 10.0, 20), Point(13.0, 10.0, 21),
                 Point(10.0, 14.0, 22)});
  assert(index.size() == 3U);
  nearby = index.radius_search(Point(3.0, 3.0, 106), 100.0);
  assert(nearby.size() == 3U);
  nearby = index.radius_search(Point(3.0, 3.0, 106), 1.0);
  assert(nearby.empty());
  nearby = index.radius_search(Point(10.0, 10.0, 106), 5.0);
  assert(nearby.size() == 3U);

  index.rebuild({});
  assert(index.size() == 0U);
  assert(index.radius_search(Point(3.0, 3.0, 105), 10.0).empty());
  assert(index.upsert(Point(6.0, 6.0, 30)));
  assert(index.size() == 1U);

  index.clear();
  index.clear();
  assert(index.size() == 0U);
  assert(index.radius_search(Point(0.0, 0.0, 106), 100.0).empty());
  assert(index.upsert(Point(7.0, 7.0, 31)));
  nearby = index.radius_search(Point(7.0, 7.0, 107), 0.0);
  assert(nearby.size() == 1U);
  assert(nearby.front().id == 31);

  return 0;
}
