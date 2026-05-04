#pragma once

#include "dispatch_batch.h"
#include "mcmf_batch_strategy.h"
#include "requestcontext.h"
#include "taxi_system.h"

#include <cstddef>
#include <string>
#include <vector>

struct DispatchReplayOptions {
  TimeSeconds start_time;
  TimeSeconds end_time;
  TimeSeconds batch_interval_seconds;
  TimeSeconds trip_duration_seconds;
  CandidateEdgeOptions candidate_options;
  bool use_indexed_candidate_edges;
  bool taxi_system_logging_enabled;

  DispatchReplayOptions()
      : start_time(0), end_time(0), batch_interval_seconds(30),
        trip_duration_seconds(600), candidate_options(),
        use_indexed_candidate_edges(false),
        taxi_system_logging_enabled(false) {}

  DispatchReplayOptions(TimeSeconds start_time_value,
                        TimeSeconds end_time_value,
                        TimeSeconds batch_interval_seconds_value,
                        TimeSeconds trip_duration_seconds_value,
                        const CandidateEdgeOptions &candidate_options_value,
                        bool use_indexed_candidate_edges_value = false,
                        bool taxi_system_logging_enabled_value = false)
      : start_time(start_time_value), end_time(end_time_value),
        batch_interval_seconds(batch_interval_seconds_value),
        trip_duration_seconds(trip_duration_seconds_value),
        candidate_options(candidate_options_value),
        use_indexed_candidate_edges(use_indexed_candidate_edges_value),
        taxi_system_logging_enabled(taxi_system_logging_enabled_value) {}
};

struct DispatchReplayMetrics {
  std::size_t total_requests;
  std::size_t assigned_requests;
  std::size_t completed_requests;
  std::size_t unserved_requests;
  std::size_t batch_runs;
  std::size_t candidate_edges_total;
  std::size_t requests_with_edges_total;
  std::size_t requests_without_edges_total;
  std::size_t unique_requests_without_edges;
  std::size_t greedy_assigned_total;
  std::size_t mcmf_assigned_total;
  int greedy_cost_total;
  int mcmf_cost_total;
  int applied_pickup_cost_total;
  TimeSeconds wait_time_total;
  long long candidate_generation_time_microseconds;
  long long matching_time_microseconds;
  long long replay_time_microseconds;

  DispatchReplayMetrics()
      : total_requests(0), assigned_requests(0), completed_requests(0),
        unserved_requests(0), batch_runs(0), candidate_edges_total(0),
        requests_with_edges_total(0), requests_without_edges_total(0),
        unique_requests_without_edges(0), greedy_assigned_total(0),
        mcmf_assigned_total(0), greedy_cost_total(0), mcmf_cost_total(0),
        applied_pickup_cost_total(0), wait_time_total(0),
        candidate_generation_time_microseconds(0),
        matching_time_microseconds(0), replay_time_microseconds(0) {}
};

struct DispatchReplayBatchLog {
  TimeSeconds batch_time;
  std::size_t available_drivers;
  std::size_t pending_requests;
  std::size_t candidate_edges;
  std::size_t requests_with_edges;
  std::size_t requests_without_edges;
  std::size_t greedy_assigned;
  std::size_t mcmf_assigned;
  std::size_t applied_assignments;
  int greedy_cost;
  int mcmf_cost;
  int applied_pickup_cost;
  long long candidate_generation_time_microseconds;
  long long matching_time_microseconds;

  DispatchReplayBatchLog()
      : batch_time(0), available_drivers(0), pending_requests(0),
        candidate_edges(0), requests_with_edges(0), requests_without_edges(0),
        greedy_assigned(0), mcmf_assigned(0), applied_assignments(0),
        greedy_cost(0), mcmf_cost(0), applied_pickup_cost(0),
        candidate_generation_time_microseconds(0),
        matching_time_microseconds(0) {}
};

struct DispatchReplayRequestOutcome {
  int request_id;
  std::size_t pending_batch_count;
  std::size_t candidate_batch_count;
  std::size_t candidate_edge_count;
  bool assigned;
  bool completed;
  int taxi_id;
  TimeSeconds assignment_time;
  TimeSeconds pickup_time;
  TimeSeconds completion_time;
  TimeSeconds wait_time;
  int pickup_cost;

  DispatchReplayRequestOutcome()
      : request_id(-1), pending_batch_count(0), candidate_batch_count(0),
        candidate_edge_count(0), assigned(false), completed(false),
        taxi_id(-1), assignment_time(0), pickup_time(0), completion_time(0),
        wait_time(0), pickup_cost(0) {}

  explicit DispatchReplayRequestOutcome(int request_id_value)
      : request_id(request_id_value), pending_batch_count(0),
        candidate_batch_count(0), candidate_edge_count(0), assigned(false),
        completed(false), taxi_id(-1), assignment_time(0), pickup_time(0),
        completion_time(0), wait_time(0), pickup_cost(0) {}
};

struct DispatchReplayReport {
  DispatchReplayMetrics metrics;
  std::vector<DispatchReplayBatchLog> batch_logs;
  std::vector<DispatchReplayRequestOutcome> request_outcomes;
};

class DispatchReplaySimulator {
public:
  DispatchReplayMetrics run(const std::vector<DriverSnapshot> &initial_drivers,
                            const std::vector<PassengerRequest> &requests,
                            const DispatchReplayOptions &options) const;

  DispatchReplayReport
  run_report(const std::vector<DriverSnapshot> &initial_drivers,
             const std::vector<PassengerRequest> &requests,
             const DispatchReplayOptions &options) const;

private:
  McmfBatchStrategy mcmf_strategy_;
};

double completion_rate(const DispatchReplayMetrics &metrics);
double assignment_rate(const DispatchReplayMetrics &metrics);
double average_applied_pickup_cost(const DispatchReplayMetrics &metrics);
double average_assignment_wait_time(const DispatchReplayMetrics &metrics);
double candidate_generation_time_ms(const DispatchReplayMetrics &metrics);
double matching_time_ms(const DispatchReplayMetrics &metrics);
double replay_time_ms(const DispatchReplayMetrics &metrics);
std::string format_dispatch_replay_report(const DispatchReplayReport &report,
                                          bool include_batch_logs = true);
std::string
format_dispatch_replay_batch_logs_csv(const DispatchReplayReport &report);
std::string
format_dispatch_replay_request_outcomes_csv(const DispatchReplayReport &report);
