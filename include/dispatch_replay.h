#pragma once

#include "dispatch_batch.h"
#include "mcmf_batch_strategy.h"
#include "requestcontext.h"
#include "taxi_system.h"

#include <cstddef>
#include <vector>

struct DispatchReplayOptions {
  TimeSeconds start_time;
  TimeSeconds end_time;
  TimeSeconds batch_interval_seconds;
  TimeSeconds trip_duration_seconds;
  CandidateEdgeOptions candidate_options;

  DispatchReplayOptions()
      : start_time(0), end_time(0), batch_interval_seconds(30),
        trip_duration_seconds(600), candidate_options() {}

  DispatchReplayOptions(TimeSeconds start_time_value,
                        TimeSeconds end_time_value,
                        TimeSeconds batch_interval_seconds_value,
                        TimeSeconds trip_duration_seconds_value,
                        const CandidateEdgeOptions &candidate_options_value)
      : start_time(start_time_value), end_time(end_time_value),
        batch_interval_seconds(batch_interval_seconds_value),
        trip_duration_seconds(trip_duration_seconds_value),
        candidate_options(candidate_options_value) {}
};

struct DispatchReplayMetrics {
  std::size_t total_requests;
  std::size_t assigned_requests;
  std::size_t completed_requests;
  std::size_t unserved_requests;
  std::size_t batch_runs;
  std::size_t greedy_assigned_total;
  std::size_t mcmf_assigned_total;
  int greedy_cost_total;
  int mcmf_cost_total;
  int applied_pickup_cost_total;

  DispatchReplayMetrics()
      : total_requests(0), assigned_requests(0), completed_requests(0),
        unserved_requests(0), batch_runs(0), greedy_assigned_total(0),
        mcmf_assigned_total(0), greedy_cost_total(0), mcmf_cost_total(0),
        applied_pickup_cost_total(0) {}
};

class DispatchReplaySimulator {
public:
  DispatchReplayMetrics run(const std::vector<DriverSnapshot> &initial_drivers,
                            const std::vector<PassengerRequest> &requests,
                            const DispatchReplayOptions &options) const;

private:
  McmfBatchStrategy mcmf_strategy_;
};
