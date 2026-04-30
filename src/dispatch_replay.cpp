#include "dispatch_replay.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <queue>
#include <unordered_map>

namespace {

enum class ReplayEventType {
  trip_complete = 0,
  request_arrival = 1,
  batch_dispatch = 2,
  pickup_arrival = 3
};

struct ReplayEvent {
  TimeSeconds time;
  ReplayEventType type;
  int request_id;
};

struct ReplayEventCompare {
  bool operator()(const ReplayEvent &lhs, const ReplayEvent &rhs) const {
    if (lhs.time != rhs.time) {
      return lhs.time > rhs.time;
    }
    return static_cast<int>(lhs.type) > static_cast<int>(rhs.type);
  }
};

int sum_pickup_cost(const std::vector<Assignment> &assignments) {
  int total = 0;
  for (const auto &assignment : assignments) {
    total += assignment.pickup_cost;
  }
  return total;
}

std::vector<DriverSnapshot>
collect_available_drivers(const std::unordered_map<int, DriverSnapshot> &drivers,
                          TimeSeconds batch_time) {
  std::vector<DriverSnapshot> available;
  available.reserve(drivers.size());
  for (const auto &[taxi_id, driver] : drivers) {
    (void)taxi_id;
    if (is_available_for_batch(driver, batch_time)) {
      available.push_back(driver);
    }
  }
  return available;
}

std::vector<PassengerRequest> collect_pending_requests(
    const std::vector<PassengerRequest> &requests,
    const std::unordered_map<int, std::unique_ptr<RequestContext>> &contexts,
    TimeSeconds batch_time) {
  std::vector<PassengerRequest> pending;
  pending.reserve(requests.size());
  for (const auto &request : requests) {
    const auto context_it = contexts.find(request.request_id);
    if (context_it == contexts.end()) {
      continue;
    }
    if (context_it->second->status() == RequestStatus::pending &&
        is_request_ready_for_batch(request, batch_time)) {
      pending.push_back(request);
    }
  }
  return pending;
}

const PassengerRequest *find_request(const std::vector<PassengerRequest> &requests,
                                     int request_id) {
  for (const auto &request : requests) {
    if (request.request_id == request_id) {
      return &request;
    }
  }
  return nullptr;
}

} // namespace

DispatchReplayMetrics DispatchReplaySimulator::run(
    const std::vector<DriverSnapshot> &initial_drivers,
    const std::vector<PassengerRequest> &requests,
    const DispatchReplayOptions &options) const {
  DispatchReplayMetrics metrics;
  metrics.total_requests = requests.size();

  if (options.batch_interval_seconds <= 0 || options.end_time < options.start_time) {
    metrics.unserved_requests = metrics.total_requests;
    return metrics;
  }

  TaxiSystem system;
  std::unordered_map<int, DriverSnapshot> drivers;
  for (const auto &driver : initial_drivers) {
    if (driver.taxi_id < 0) {
      continue;
    }

    if (!system.register_taxi(driver.taxi_id)) {
      continue;
    }

    DriverSnapshot snapshot = driver;
    snapshot.status = TaxiStatus::offline;
    drivers.emplace(snapshot.taxi_id, snapshot);
    if (driver.status == TaxiStatus::free) {
      if (system.set_taxi_online(driver.taxi_id, driver.location.coords[0],
                                 driver.location.coords[1])) {
        drivers[driver.taxi_id].status = TaxiStatus::free;
        drivers[driver.taxi_id].available_time = driver.available_time;
      }
    }
  }

  std::priority_queue<ReplayEvent, std::vector<ReplayEvent>, ReplayEventCompare>
      events;
  std::unordered_map<int, std::unique_ptr<RequestContext>> request_contexts;

  for (TimeSeconds time = options.start_time; time <= options.end_time;
       time += options.batch_interval_seconds) {
    events.push(ReplayEvent{time, ReplayEventType::batch_dispatch, -1});
  }

  for (const auto &request : requests) {
    if (!is_valid_batch_request(request)) {
      continue;
    }
    events.push(
        ReplayEvent{request.request_time, ReplayEventType::request_arrival,
                    request.request_id});
  }

  while (!events.empty()) {
    const ReplayEvent event = events.top();
    events.pop();

    if (event.type == ReplayEventType::request_arrival) {
      const PassengerRequest *request = find_request(requests, event.request_id);
      if (!request || request_contexts.count(request->request_id) != 0) {
        continue;
      }

      request_contexts.emplace(
          request->request_id,
          std::make_unique<RequestContext>(
              request->request_id, request->customer_id,
              request->pickup_location, request->dropoff_location));
      continue;
    }

    if (event.type == ReplayEventType::batch_dispatch) {
      const auto available_drivers = collect_available_drivers(drivers, event.time);
      const auto pending_requests =
          collect_pending_requests(requests, request_contexts, event.time);
      if (available_drivers.empty() || pending_requests.empty()) {
        ++metrics.batch_runs;
        continue;
      }

      BatchDispatchInput batch(event.time, available_drivers, pending_requests);
      const auto candidate_edges =
          generate_candidate_edges(batch, options.candidate_options);
      const auto greedy_assignments = greedy_batch_assign(candidate_edges);
      const auto mcmf_assignments = mcmf_strategy_.assign(candidate_edges);

      ++metrics.batch_runs;
      metrics.greedy_assigned_total += greedy_assignments.size();
      metrics.mcmf_assigned_total += mcmf_assignments.size();
      metrics.greedy_cost_total += sum_pickup_cost(greedy_assignments);
      metrics.mcmf_cost_total += sum_pickup_cost(mcmf_assignments);

      for (const auto &assignment : mcmf_assignments) {
        const auto context_it = request_contexts.find(assignment.request_id);
        const PassengerRequest *request =
            find_request(requests, assignment.request_id);
        const auto driver_it = drivers.find(assignment.taxi_id);
        if (context_it == request_contexts.end() || !request ||
            driver_it == drivers.end()) {
          continue;
        }

        if (!system.apply_assignment(*context_it->second, assignment)) {
          continue;
        }

        driver_it->second.status = TaxiStatus::occupy;
        driver_it->second.available_time =
            event.time + assignment.pickup_cost + options.trip_duration_seconds;
        ++metrics.assigned_requests;
        metrics.applied_pickup_cost_total += assignment.pickup_cost;

        events.push(ReplayEvent{event.time + assignment.pickup_cost,
                                ReplayEventType::pickup_arrival,
                                assignment.request_id});
        events.push(ReplayEvent{event.time + assignment.pickup_cost +
                                    options.trip_duration_seconds,
                                ReplayEventType::trip_complete,
                                assignment.request_id});
      }
      continue;
    }

    const auto context_it = request_contexts.find(event.request_id);
    const PassengerRequest *request = find_request(requests, event.request_id);
    if (context_it == request_contexts.end() || !request) {
      continue;
    }

    RequestContext &context = *context_it->second;
    const std::optional<int> taxi_id = context.taxi_id();
    if (!taxi_id) {
      continue;
    }

    if (event.type == ReplayEventType::pickup_arrival) {
      system.start_trip(context);
      continue;
    }

    if (event.type == ReplayEventType::trip_complete) {
      system.update_taxi_position(*taxi_id, request->dropoff_location.coords[0],
                                  request->dropoff_location.coords[1]);
      if (system.complete_trip(context)) {
        const auto driver_it = drivers.find(*taxi_id);
        if (driver_it != drivers.end()) {
          driver_it->second.location = request->dropoff_location;
          driver_it->second.status = TaxiStatus::free;
          driver_it->second.available_time = event.time;
          driver_it->second.current_tile = request->dropoff_tile;
        }
        ++metrics.completed_requests;
      }
    }
  }

  for (const auto &request : requests) {
    const auto context_it = request_contexts.find(request.request_id);
    if (context_it == request_contexts.end() ||
        context_it->second->status() == RequestStatus::pending) {
      ++metrics.unserved_requests;
    }
  }

  return metrics;
}
