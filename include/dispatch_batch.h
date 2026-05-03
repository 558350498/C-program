#pragma once

#include "kd_tree_spatial_index.h"
#include "taxi_domain.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using TileId = int;
using TimeSeconds = long long;

constexpr TileId invalid_tile_id = -1;

struct PassengerRequest {
  int request_id;
  int customer_id;
  TimeSeconds request_time;
  Point pickup_location;
  Point dropoff_location;
  TileId pickup_tile;
  TileId dropoff_tile;

  PassengerRequest()
      : request_id(-1), customer_id(-1), request_time(0),
        pickup_location(), dropoff_location(), pickup_tile(invalid_tile_id),
        dropoff_tile(invalid_tile_id) {}

  PassengerRequest(int request_id_value, int customer_id_value,
                   TimeSeconds request_time_value, const Point &pickup,
                   const Point &dropoff, TileId pickup_tile_value,
                   TileId dropoff_tile_value)
      : request_id(request_id_value), customer_id(customer_id_value),
        request_time(request_time_value), pickup_location(pickup),
        dropoff_location(dropoff), pickup_tile(pickup_tile_value),
        dropoff_tile(dropoff_tile_value) {}
};

struct TripRecord {
  int trip_id;
  int taxi_id;
  TimeSeconds pickup_time;
  TimeSeconds dropoff_time;
  TileId pickup_tile;
  TileId dropoff_tile;

  TripRecord()
      : trip_id(-1), taxi_id(-1), pickup_time(0), dropoff_time(0),
        pickup_tile(invalid_tile_id), dropoff_tile(invalid_tile_id) {}

  TripRecord(int trip_id_value, int taxi_id_value,
             TimeSeconds pickup_time_value, TimeSeconds dropoff_time_value,
             TileId pickup_tile_value, TileId dropoff_tile_value)
      : trip_id(trip_id_value), taxi_id(taxi_id_value),
        pickup_time(pickup_time_value), dropoff_time(dropoff_time_value),
        pickup_tile(pickup_tile_value), dropoff_tile(dropoff_tile_value) {}
};

struct DriverSnapshot {
  int taxi_id;
  Point location;
  TileId current_tile;
  TaxiStatus status;
  TimeSeconds available_time;

  DriverSnapshot()
      : taxi_id(-1), location(), current_tile(invalid_tile_id),
        status(TaxiStatus::offline), available_time(0) {}

  DriverSnapshot(int taxi_id_value, const Point &location_value,
                 TileId current_tile_value, TaxiStatus status_value,
                 TimeSeconds available_time_value)
      : taxi_id(taxi_id_value), location(location_value),
        current_tile(current_tile_value), status(status_value),
        available_time(available_time_value) {}
};

struct Assignment {
  int taxi_id;
  int request_id;
  int pickup_cost;

  Assignment() : taxi_id(-1), request_id(-1), pickup_cost(0) {}

  Assignment(int taxi_id_value, int request_id_value, int pickup_cost_value)
      : taxi_id(taxi_id_value), request_id(request_id_value),
        pickup_cost(pickup_cost_value) {}
};

struct CandidateEdge {
  int taxi_id;
  int request_id;
  int pickup_cost;

  CandidateEdge() : taxi_id(-1), request_id(-1), pickup_cost(0) {}

  CandidateEdge(int taxi_id_value, int request_id_value,
                int pickup_cost_value)
      : taxi_id(taxi_id_value), request_id(request_id_value),
        pickup_cost(pickup_cost_value) {}
};

struct CandidateEdgeStats {
  std::size_t total_drivers;
  std::size_t total_requests;
  std::size_t available_drivers;
  std::size_t ready_requests;
  std::size_t candidate_edges;
  std::size_t requests_with_edges;
  std::size_t requests_without_edges;

  CandidateEdgeStats()
      : total_drivers(0), total_requests(0), available_drivers(0),
        ready_requests(0), candidate_edges(0), requests_with_edges(0),
        requests_without_edges(0) {}
};

struct CandidateEdgeGenerationResult {
  std::vector<CandidateEdge> edges;
  CandidateEdgeStats stats;
};

struct CandidateEdgeOptions {
  double radius;
  double seconds_per_distance_unit;
  std::size_t max_edges_per_request;
  bool same_tile_only;

  CandidateEdgeOptions()
      : radius(0.0), seconds_per_distance_unit(1.0),
        max_edges_per_request(0), same_tile_only(false) {}

  CandidateEdgeOptions(double radius_value,
                       double seconds_per_distance_unit_value,
                       std::size_t max_edges_per_request_value = 0,
                       bool same_tile_only_value = false)
      : radius(radius_value),
        seconds_per_distance_unit(seconds_per_distance_unit_value),
        max_edges_per_request(max_edges_per_request_value),
        same_tile_only(same_tile_only_value) {}
};

struct BatchDispatchInput {
  TimeSeconds batch_time;
  std::vector<DriverSnapshot> drivers;
  std::vector<PassengerRequest> requests;

  BatchDispatchInput() : batch_time(0), drivers(), requests() {}

  BatchDispatchInput(TimeSeconds batch_time_value,
                     std::vector<DriverSnapshot> driver_values,
                     std::vector<PassengerRequest> request_values)
      : batch_time(batch_time_value), drivers(std::move(driver_values)),
        requests(std::move(request_values)) {}
};

inline bool is_available_for_batch(const DriverSnapshot &driver,
                                   TimeSeconds batch_time) {
  return driver.taxi_id >= 0 && driver.status == TaxiStatus::free &&
         driver.available_time <= batch_time;
}

inline bool is_valid_batch_request(const PassengerRequest &request) {
  return request.request_id >= 0 && request.customer_id >= 0;
}

inline bool is_request_ready_for_batch(const PassengerRequest &request,
                                       TimeSeconds batch_time) {
  return is_valid_batch_request(request) && request.request_time <= batch_time;
}

inline bool is_same_pickup_tile(const DriverSnapshot &driver,
                                const PassengerRequest &request) {
  return driver.current_tile != invalid_tile_id &&
         request.pickup_tile != invalid_tile_id &&
         driver.current_tile == request.pickup_tile;
}

inline int estimate_pickup_cost(const Point &driver_location,
                                const Point &pickup_location,
                                double seconds_per_distance_unit = 1.0) {
  if (seconds_per_distance_unit <= 0.0) {
    return std::numeric_limits<int>::max();
  }

  const double distance = std::sqrt(dist_sq(driver_location, pickup_location));
  const double cost = distance * seconds_per_distance_unit;
  if (!std::isfinite(cost) ||
      cost >= static_cast<double>(std::numeric_limits<int>::max())) {
    return std::numeric_limits<int>::max();
  }

  return static_cast<int>(std::llround(cost));
}

inline std::vector<CandidateEdge>
normalize_candidate_edges(std::vector<CandidateEdge> edges) {
  edges.erase(std::remove_if(edges.begin(), edges.end(),
                             [](const CandidateEdge &edge) {
                               return edge.taxi_id < 0 ||
                                      edge.request_id < 0 ||
                                      edge.pickup_cost < 0;
                             }),
              edges.end());

  std::sort(edges.begin(), edges.end(),
            [](const CandidateEdge &lhs, const CandidateEdge &rhs) {
              if (lhs.taxi_id != rhs.taxi_id) {
                return lhs.taxi_id < rhs.taxi_id;
              }
              if (lhs.request_id != rhs.request_id) {
                return lhs.request_id < rhs.request_id;
              }
              return lhs.pickup_cost < rhs.pickup_cost;
            });

  std::vector<CandidateEdge> normalized;
  normalized.reserve(edges.size());
  for (const auto &edge : edges) {
    if (!normalized.empty() &&
        normalized.back().taxi_id == edge.taxi_id &&
        normalized.back().request_id == edge.request_id) {
      continue;
    }
    normalized.push_back(edge);
  }

  return normalized;
}

inline CandidateEdgeGenerationResult generate_candidate_edges_with_stats(
    const BatchDispatchInput &batch, const CandidateEdgeOptions &options) {
  CandidateEdgeGenerationResult result;
  result.stats.total_drivers = batch.drivers.size();
  result.stats.total_requests = batch.requests.size();

  for (const auto &driver : batch.drivers) {
    if (is_available_for_batch(driver, batch.batch_time)) {
      ++result.stats.available_drivers;
    }
  }

  if (options.radius < 0.0 || options.seconds_per_distance_unit <= 0.0) {
    return result;
  }

  const double radius_sq = options.radius * options.radius;
  for (const auto &request : batch.requests) {
    if (!is_request_ready_for_batch(request, batch.batch_time)) {
      continue;
    }
    ++result.stats.ready_requests;

    std::vector<CandidateEdge> request_edges;
    for (const auto &driver : batch.drivers) {
      if (!is_available_for_batch(driver, batch.batch_time)) {
        continue;
      }
      if (options.same_tile_only && !is_same_pickup_tile(driver, request)) {
        continue;
      }

      if (dist_sq(driver.location, request.pickup_location) > radius_sq) {
        continue;
      }

      const int cost = estimate_pickup_cost(
          driver.location, request.pickup_location,
          options.seconds_per_distance_unit);
      if (cost == std::numeric_limits<int>::max()) {
        continue;
      }

      request_edges.emplace_back(driver.taxi_id, request.request_id, cost);
    }

    request_edges = normalize_candidate_edges(std::move(request_edges));

    std::sort(request_edges.begin(), request_edges.end(),
              [](const CandidateEdge &lhs, const CandidateEdge &rhs) {
                if (lhs.pickup_cost != rhs.pickup_cost) {
                  return lhs.pickup_cost < rhs.pickup_cost;
                }
                return lhs.taxi_id < rhs.taxi_id;
              });

    if (options.max_edges_per_request > 0 &&
        request_edges.size() > options.max_edges_per_request) {
      request_edges.resize(options.max_edges_per_request);
    }

    if (request_edges.empty()) {
      ++result.stats.requests_without_edges;
    } else {
      ++result.stats.requests_with_edges;
    }

    result.edges.insert(result.edges.end(), request_edges.begin(),
                        request_edges.end());
  }

  result.stats.candidate_edges = result.edges.size();
  return result;
}

inline std::vector<CandidateEdge>
generate_candidate_edges(const BatchDispatchInput &batch,
                         const CandidateEdgeOptions &options) {
  return generate_candidate_edges_with_stats(batch, options).edges;
}

inline CandidateEdgeGenerationResult generate_candidate_edges_indexed_with_stats(
    const BatchDispatchInput &batch, const CandidateEdgeOptions &options) {
  CandidateEdgeGenerationResult result;
  result.stats.total_drivers = batch.drivers.size();
  result.stats.total_requests = batch.requests.size();

  for (const auto &driver : batch.drivers) {
    if (is_available_for_batch(driver, batch.batch_time)) {
      ++result.stats.available_drivers;
    }
  }

  if (options.radius < 0.0 || options.seconds_per_distance_unit <= 0.0) {
    return result;
  }

  KdTreeSpatialIndex driver_index;
  std::unordered_map<int, DriverSnapshot> drivers_by_id;
  drivers_by_id.reserve(batch.drivers.size());

  for (const auto &driver : batch.drivers) {
    if (!is_available_for_batch(driver, batch.batch_time)) {
      continue;
    }
    drivers_by_id[driver.taxi_id] = driver;
    driver_index.upsert(Point(driver.location.coords[0],
                              driver.location.coords[1], driver.taxi_id));
  }

  for (const auto &request : batch.requests) {
    if (!is_request_ready_for_batch(request, batch.batch_time)) {
      continue;
    }
    ++result.stats.ready_requests;

    std::vector<CandidateEdge> request_edges;
    const auto nearby_drivers =
        driver_index.radius_query(request.pickup_location, options.radius);
    for (const auto &candidate : nearby_drivers) {
      const auto driver_it = drivers_by_id.find(candidate.id);
      if (driver_it == drivers_by_id.end()) {
        continue;
      }

      const DriverSnapshot &driver = driver_it->second;
      if (options.same_tile_only && !is_same_pickup_tile(driver, request)) {
        continue;
      }

      const int cost = estimate_pickup_cost(
          driver.location, request.pickup_location,
          options.seconds_per_distance_unit);
      if (cost == std::numeric_limits<int>::max()) {
        continue;
      }

      request_edges.emplace_back(driver.taxi_id, request.request_id, cost);
    }

    request_edges = normalize_candidate_edges(std::move(request_edges));

    std::sort(request_edges.begin(), request_edges.end(),
              [](const CandidateEdge &lhs, const CandidateEdge &rhs) {
                if (lhs.pickup_cost != rhs.pickup_cost) {
                  return lhs.pickup_cost < rhs.pickup_cost;
                }
                return lhs.taxi_id < rhs.taxi_id;
              });

    if (options.max_edges_per_request > 0 &&
        request_edges.size() > options.max_edges_per_request) {
      request_edges.resize(options.max_edges_per_request);
    }

    if (request_edges.empty()) {
      ++result.stats.requests_without_edges;
    } else {
      ++result.stats.requests_with_edges;
    }

    result.edges.insert(result.edges.end(), request_edges.begin(),
                        request_edges.end());
  }

  result.stats.candidate_edges = result.edges.size();
  return result;
}

inline std::vector<CandidateEdge>
generate_candidate_edges_indexed(const BatchDispatchInput &batch,
                                 const CandidateEdgeOptions &options) {
  return generate_candidate_edges_indexed_with_stats(batch, options).edges;
}

inline std::vector<CandidateEdge>
generate_candidate_edges(const BatchDispatchInput &batch, double radius,
                         double seconds_per_distance_unit = 1.0) {
  return generate_candidate_edges(
      batch, CandidateEdgeOptions(radius, seconds_per_distance_unit));
}

inline std::vector<Assignment>
greedy_batch_assign(std::vector<CandidateEdge> edges) {
  edges = normalize_candidate_edges(std::move(edges));
  std::sort(edges.begin(), edges.end(),
            [](const CandidateEdge &lhs, const CandidateEdge &rhs) {
              if (lhs.pickup_cost != rhs.pickup_cost) {
                return lhs.pickup_cost < rhs.pickup_cost;
              }
              if (lhs.request_id != rhs.request_id) {
                return lhs.request_id < rhs.request_id;
              }
              return lhs.taxi_id < rhs.taxi_id;
            });

  std::unordered_set<int> used_taxis;
  std::unordered_set<int> served_requests;
  std::vector<Assignment> assignments;

  for (const auto &edge : edges) {
    if (edge.taxi_id < 0 || edge.request_id < 0) {
      continue;
    }
    if (used_taxis.count(edge.taxi_id) != 0 ||
        served_requests.count(edge.request_id) != 0) {
      continue;
    }

    used_taxis.insert(edge.taxi_id);
    served_requests.insert(edge.request_id);
    assignments.emplace_back(edge.taxi_id, edge.request_id, edge.pickup_cost);
  }

  return assignments;
}

inline std::vector<Assignment>
greedy_batch_assign(const BatchDispatchInput &batch, double radius,
                    double seconds_per_distance_unit = 1.0) {
  return greedy_batch_assign(
      generate_candidate_edges(batch, radius, seconds_per_distance_unit));
}
