#pragma once

#include "dispatch_batch.h"
#include "dispatch_strategy.h"
#include "requestcontext.h"
#include "spatial_index.h"
#include "taxi_domain.h"

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

class TaxiSystem {
public:
  explicit TaxiSystem(std::unique_ptr<ISpatialIndex> spatial_index = nullptr,
                      std::unique_ptr<IDispatchStrategy> dispatch_strategy =
                          nullptr);
  ~TaxiSystem();

  int create_taxi();
  bool register_taxi(int id);
  bool set_taxi_online(int id, double x, double y);
  bool set_taxi_offline(int id);
  bool update_taxi_position(int id, double x, double y);
  bool update_taxi_status(int id, TaxiStatus status);
  bool apply_assignment(IRequestContext &request,
                        const Assignment &assignment);
  std::optional<int> dispatch_nearest(IRequestContext &request, double radius);
  std::optional<int> dispatch_nearest(int customer_id, double x, double y,
                                      double radius);
  bool start_trip(IRequestContext &request);
  bool complete_trip(IRequestContext &request);
  bool cancel_request(IRequestContext &request);

private:
  std::unordered_map<int, Taxi> taxi_map_;
  std::unique_ptr<ISpatialIndex> spatial_index_;
  std::unique_ptr<IDispatchStrategy> dispatch_strategy_;
  int next_taxi_id_;

  Taxi *find_taxi(int id);
  std::vector<Point> collect_free_taxi_points() const;
  bool rebuild_spatial_index(const char *operation);
  bool remove_from_spatial_index(const Taxi &taxi, const char *operation);
  bool upsert_into_spatial_index(const Taxi &taxi, const char *operation);
  bool release_occupied_taxi(int id, const char *operation);
};

using taxi_system = TaxiSystem;
