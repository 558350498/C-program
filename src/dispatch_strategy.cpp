#include "nearest_free_taxi_strategy.h"

#include <algorithm>
#include <iostream>
#include <vector>

std::optional<int> NearestFreeTaxiStrategy::select_taxi(
    const Point &customer_location, double radius,
    const std::unordered_map<int, Taxi> &taxi_map,
    ISpatialIndex &spatial_index) {
  if (radius < 0.0) {
    std::cerr << "[NearestFreeTaxiStrategy::select_taxi] rejected negative "
                 "radius="
              << radius << '\n';
    return std::nullopt;
  }

  std::vector<Point> nearby_taxis =
      spatial_index.radius_search(customer_location, radius);
  std::vector<Point> free_taxis;
  std::vector<int> stale_ids;

  for (const auto &candidate : nearby_taxis) {
    const auto it = taxi_map.find(candidate.id);
    if (it == taxi_map.end() || it->second.status != TaxiStatus::free) {
      stale_ids.push_back(candidate.id);
      continue;
    }
    free_taxis.push_back(candidate);
  }

  for (const int stale_id : stale_ids) {
    if (!spatial_index.erase(stale_id)) {
      std::clog << "[NearestFreeTaxiStrategy::select_taxi] stale taxi_id="
                << stale_id << " could not be removed from index\n";
    } else {
      std::clog << "[NearestFreeTaxiStrategy::select_taxi] cleaned stale taxi_id="
                << stale_id << '\n';
    }
  }

  if (free_taxis.empty()) {
    std::clog << "[NearestFreeTaxiStrategy::select_taxi] no free taxi found "
                 "within radius="
              << radius << '\n';
    return std::nullopt;
  }

  std::sort(free_taxis.begin(), free_taxis.end(),
            [&customer_location](const Point &lhs, const Point &rhs) {
              return dist_sq(lhs, customer_location) <
                     dist_sq(rhs, customer_location);
            });

  return free_taxis.front().id;
}

