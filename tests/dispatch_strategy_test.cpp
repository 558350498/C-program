#include "kd_tree_spatial_index.h"
#include "nearest_free_taxi_strategy.h"

#include <cassert>
#include <unordered_map>

int main() {
  NearestFreeTaxiStrategy strategy;

  KdTreeSpatialIndex negative_radius_index;
  std::unordered_map<int, Taxi> empty_map;
  assert(!strategy
              .select_taxi(Point(0.0, 0.0, 1), -1.0, empty_map,
                           negative_radius_index)
              .has_value());

  KdTreeSpatialIndex stale_index;
  assert(stale_index.upsert(Point(0.0, 0.0, 999)));
  assert(stale_index.upsert(Point(0.4, 0.0, 2)));

  std::unordered_map<int, Taxi> valid_map;
  valid_map.emplace(2, Taxi(2, 0.4, 0.0, TaxiStatus::free));

  auto picked =
      strategy.select_taxi(Point(0.0, 0.0, 100), 1.0, valid_map, stale_index);
  assert(picked.has_value());
  assert(*picked == 2);
  assert(!stale_index.erase(999));

  KdTreeSpatialIndex occupied_index;
  assert(occupied_index.upsert(Point(0.0, 0.0, 1)));
  assert(occupied_index.upsert(Point(0.3, 0.0, 2)));

  std::unordered_map<int, Taxi> mixed_map;
  mixed_map.emplace(1, Taxi(1, 0.0, 0.0, TaxiStatus::occupy));
  mixed_map.emplace(2, Taxi(2, 0.3, 0.0, TaxiStatus::free));

  picked =
      strategy.select_taxi(Point(0.0, 0.0, 101), 1.0, mixed_map, occupied_index);
  assert(picked.has_value());
  assert(*picked == 2);
  assert(!occupied_index.erase(1));

  KdTreeSpatialIndex only_stale_index;
  assert(only_stale_index.upsert(Point(0.0, 0.0, 3)));

  std::unordered_map<int, Taxi> only_stale_map;
  only_stale_map.emplace(3, Taxi(3, 0.0, 0.0, TaxiStatus::occupy));

  picked = strategy.select_taxi(Point(0.0, 0.0, 102), 1.0, only_stale_map,
                                only_stale_index);
  assert(!picked.has_value());
  assert(!only_stale_index.erase(3));

  return 0;
}
