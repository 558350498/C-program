#pragma once

#include "dispatch_strategy.h"

class NearestFreeTaxiStrategy : public IDispatchStrategy {
public:
  std::optional<int>
  select_taxi(const Point &customer_location, double radius,
              const std::unordered_map<int, Taxi> &taxi_map,
              ISpatialIndex &spatial_index) override;
};
