#ifndef taxi_query_service_h
#define taxi_query_service_h

#include "taxi_core.h"

#include <unordered_map>
#include <vector>

class TaxiQueryService {
public:
  template <typename RebuildFn>
  static std::vector<Point>
  query_free_candidates(ITaxiSpatialIndex &spatial_index,
                        const std::unordered_map<int, Taxi> &taxi_map,
                        const Point &customer_location, double radius,
                        RebuildFn &&rebuild_index) {
    std::vector<Point> nearby_taxis = spatial_index.range_search(customer_location, radius);
    std::vector<Point> free_taxis;
    std::vector<int> stale_taxi_ids;

    for (const auto &taxi_point : nearby_taxis) {
      auto it = taxi_map.find(taxi_point.id);
      if (it == taxi_map.end() || it->second.status != TaxiStatus::free) {
        stale_taxi_ids.push_back(taxi_point.id);
        continue;
      }
      free_taxis.push_back(taxi_point);
    }

    for (int stale_id : stale_taxi_ids) {
      if (!spatial_index.remove(stale_id)) {
        rebuild_index();
        break;
      }
    }

    return free_taxis;
  }
};

#endif // taxi_query_service_h
