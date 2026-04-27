#include "kd_tree_spatial_index.h"
#include "nearest_free_taxi_strategy.h"
#include "taxi_system.h"

#include <cassert>
#include <unordered_map>

int main() {
  TaxiSystem system;

  const int first = system.create_taxi();
  const int second = system.create_taxi();
  assert(first == 0);
  assert(second == 1);

  assert(!system.register_taxi(first));
  assert(!system.register_taxi(-1));
  assert(system.register_taxi(10));

  assert(system.set_taxi_online(first, 0.0, 0.0));
  assert(system.set_taxi_online(second, 5.0, 0.0));
  assert(!system.set_taxi_online(999, 0.0, 0.0));

  auto assigned = system.dispatch_nearest(100, 0.5, 0.0, 10.0);
  assert(assigned.has_value());
  assert(*assigned == first);

  assert(!system.dispatch_nearest(101, 0.5, 0.0, -1.0).has_value());

  assert(system.update_taxi_position(first, 1.0, 0.0));
  assert(system.update_taxi_status(first, TaxiStatus::free));

  assigned = system.dispatch_nearest(102, 0.8, 0.0, 2.0);
  assert(assigned.has_value());
  assert(*assigned == first);

  assert(system.set_taxi_offline(second));
  assert(!system.update_taxi_position(second, 6.0, 0.0));
  assert(!system.update_taxi_status(second, TaxiStatus::free));

  KdTreeSpatialIndex stale_index;
  assert(stale_index.upsert(Point(0.15, 0.0, 999)));
  assert(stale_index.upsert(Point(0.0, 0.0, 1)));

  std::unordered_map<int, Taxi> taxi_map;
  taxi_map.emplace(1, Taxi(1, 0.0, 0.0, TaxiStatus::free));

  NearestFreeTaxiStrategy strategy;
  const auto picked =
      strategy.select_taxi(Point(0.1, 0.0, 200), 1.0, taxi_map, stale_index);
  assert(picked.has_value());
  assert(*picked == 1);
  assert(!stale_index.erase(999));

  return 0;
}
