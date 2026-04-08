#ifndef dispatch_service_h
#define dispatch_service_h

#include "taxi_query_service.h"

#include <vector>

class DispatchService {
public:
  template <typename UpdateStatusFn, typename RebuildFn>
  static std::vector<Point>
  assign_one(ITaxiSpatialIndex &spatial_index,
             const std::unordered_map<int, Taxi> &taxi_map,
             const IAssignmentPolicy &assignment_policy,
             const Point &customer_location, double radius,
             UpdateStatusFn &&update_taxi_status,
             RebuildFn &&rebuild_index) {
    std::vector<Point> free_taxis = TaxiQueryService::query_free_candidates(
        spatial_index, taxi_map, customer_location, radius, rebuild_index);

    auto assigned_opt = assignment_policy.choose_taxi(free_taxis, customer_location);
    if (!assigned_opt.has_value()) {
      return {};
    }

    Point assigned_taxi = assigned_opt.value();
    if (!update_taxi_status(assigned_taxi.id, TaxiStatus::occupy)) {
      return {};
    }

    return {assigned_taxi};
  }
};

#endif // dispatch_service_h
