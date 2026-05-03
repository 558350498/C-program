#include "dispatch_replay.h"

#include <algorithm>
#include <iomanip>
#include <memory>
#include <optional>
#include <queue>
#include <sstream>
#include <unordered_set>
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
  return run_report(initial_drivers, requests, options).metrics;
}

DispatchReplayReport DispatchReplaySimulator::run_report(
    const std::vector<DriverSnapshot> &initial_drivers,
    const std::vector<PassengerRequest> &requests,
    const DispatchReplayOptions &options) const {
  DispatchReplayReport report;
  DispatchReplayMetrics &metrics = report.metrics;
  metrics.total_requests = requests.size();

  if (options.batch_interval_seconds <= 0 || options.end_time < options.start_time) {
    metrics.unserved_requests = metrics.total_requests;
    return report;
  }

  TaxiSystem system(nullptr, nullptr, options.taxi_system_logging_enabled);
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
  std::unordered_set<int> requests_without_candidate_edges;

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

      DispatchReplayBatchLog batch_log;
      batch_log.batch_time = event.time;
      batch_log.available_drivers = available_drivers.size();
      batch_log.pending_requests = pending_requests.size();

      if (available_drivers.empty() || pending_requests.empty()) {
        ++metrics.batch_runs;
        report.batch_logs.push_back(batch_log);
        continue;
      }

      BatchDispatchInput batch(event.time, available_drivers, pending_requests);
      const auto candidate_result =
          generate_candidate_edges_with_stats(batch, options.candidate_options);
      const auto &candidate_edges = candidate_result.edges;
      const auto greedy_assignments = greedy_batch_assign(candidate_edges);
      const auto mcmf_assignments = mcmf_strategy_.assign(candidate_edges);
      const int greedy_cost = sum_pickup_cost(greedy_assignments);
      const int mcmf_cost = sum_pickup_cost(mcmf_assignments);

      ++metrics.batch_runs;
      metrics.candidate_edges_total += candidate_result.stats.candidate_edges;
      metrics.requests_with_edges_total +=
          candidate_result.stats.requests_with_edges;
      metrics.requests_without_edges_total +=
          candidate_result.stats.requests_without_edges;
      metrics.greedy_assigned_total += greedy_assignments.size();
      metrics.mcmf_assigned_total += mcmf_assignments.size();
      metrics.greedy_cost_total += greedy_cost;
      metrics.mcmf_cost_total += mcmf_cost;

      batch_log.candidate_edges = candidate_result.stats.candidate_edges;
      batch_log.requests_with_edges = candidate_result.stats.requests_with_edges;
      batch_log.requests_without_edges =
          candidate_result.stats.requests_without_edges;
      batch_log.greedy_assigned = greedy_assignments.size();
      batch_log.mcmf_assigned = mcmf_assignments.size();
      batch_log.greedy_cost = greedy_cost;
      batch_log.mcmf_cost = mcmf_cost;

      std::unordered_set<int> requests_with_edges;
      requests_with_edges.reserve(candidate_edges.size());
      for (const auto &edge : candidate_edges) {
        requests_with_edges.insert(edge.request_id);
      }
      for (const auto &request : pending_requests) {
        if (requests_with_edges.count(request.request_id) == 0) {
          requests_without_candidate_edges.insert(request.request_id);
        }
      }

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
        if (event.time >= request->request_time) {
          metrics.wait_time_total +=
              event.time - request->request_time;
        }
        ++batch_log.applied_assignments;
        batch_log.applied_pickup_cost += assignment.pickup_cost;

        events.push(ReplayEvent{event.time + assignment.pickup_cost,
                                ReplayEventType::pickup_arrival,
                                assignment.request_id});
        events.push(ReplayEvent{event.time + assignment.pickup_cost +
                                    options.trip_duration_seconds,
                                ReplayEventType::trip_complete,
                                assignment.request_id});
      }
      report.batch_logs.push_back(batch_log);
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

  metrics.unique_requests_without_edges = requests_without_candidate_edges.size();

  return report;
}

double completion_rate(const DispatchReplayMetrics &metrics) {
  if (metrics.total_requests == 0) {
    return 0.0;
  }
  return static_cast<double>(metrics.completed_requests) /
         static_cast<double>(metrics.total_requests);
}

double assignment_rate(const DispatchReplayMetrics &metrics) {
  if (metrics.total_requests == 0) {
    return 0.0;
  }
  return static_cast<double>(metrics.assigned_requests) /
         static_cast<double>(metrics.total_requests);
}

double average_applied_pickup_cost(const DispatchReplayMetrics &metrics) {
  if (metrics.assigned_requests == 0) {
    return 0.0;
  }
  return static_cast<double>(metrics.applied_pickup_cost_total) /
         static_cast<double>(metrics.assigned_requests);
}

double average_assignment_wait_time(const DispatchReplayMetrics &metrics) {
  if (metrics.assigned_requests == 0) {
    return 0.0;
  }
  return static_cast<double>(metrics.wait_time_total) /
         static_cast<double>(metrics.assigned_requests);
}

std::string format_dispatch_replay_report(const DispatchReplayReport &report,
                                          bool include_batch_logs) {
  const DispatchReplayMetrics &metrics = report.metrics;
  std::ostringstream stream;
  stream << "Dispatch replay summary\n";
  stream << "requests total=" << metrics.total_requests
         << " assigned=" << metrics.assigned_requests
         << " completed=" << metrics.completed_requests
         << " unserved=" << metrics.unserved_requests << '\n';
  stream << std::fixed << std::setprecision(2);
  stream << "rates assignment=" << assignment_rate(metrics) * 100.0
         << "% completion=" << completion_rate(metrics) * 100.0 << "%\n";
  stream << "batches runs=" << metrics.batch_runs
         << " candidate_edges=" << metrics.candidate_edges_total
         << " requests_with_edges="
         << metrics.requests_with_edges_total
         << " requests_without_edges="
         << metrics.requests_without_edges_total
         << " unique_requests_without_edges="
         << metrics.unique_requests_without_edges << '\n';
  stream << "strategy greedy_assigned=" << metrics.greedy_assigned_total
         << " greedy_cost=" << metrics.greedy_cost_total
         << " mcmf_assigned=" << metrics.mcmf_assigned_total
         << " mcmf_cost=" << metrics.mcmf_cost_total << '\n';
  stream << "applied pickup_cost_total=" << metrics.applied_pickup_cost_total
         << " pickup_cost_avg=" << average_applied_pickup_cost(metrics)
         << " assignment_wait_avg=" << average_assignment_wait_time(metrics)
         << '\n';

  if (!include_batch_logs || report.batch_logs.empty()) {
    return stream.str();
  }

  stream << "Batch logs\n";
  for (const auto &log : report.batch_logs) {
    stream << "t=" << log.batch_time << " drivers=" << log.available_drivers
           << " pending=" << log.pending_requests
           << " edges=" << log.candidate_edges
           << " req_with_edges=" << log.requests_with_edges
           << " req_without_edges=" << log.requests_without_edges
           << " greedy=" << log.greedy_assigned << '/' << log.greedy_cost
           << " mcmf=" << log.mcmf_assigned << '/' << log.mcmf_cost
           << " applied=" << log.applied_assignments << '/'
           << log.applied_pickup_cost << '\n';
  }

  return stream.str();
}
