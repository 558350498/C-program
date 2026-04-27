#pragma once

#include "spatial_index.h"
#include "taxi_domain.h"

#include <optional>
#include <unordered_map>

class IDispatchStrategy {
public:
  virtual ~IDispatchStrategy() = default;

  virtual std::optional<int>
  select_taxi(const Point &customer_location, double radius,
              const std::unordered_map<int, Taxi> &taxi_map,
              ISpatialIndex &spatial_index) = 0;
};
