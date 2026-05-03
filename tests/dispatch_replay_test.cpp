#include "dispatch_replay.h"

#include <string>
#include <vector>

#define REQUIRE(condition)                                                      \
  do {                                                                         \
    if (!(condition)) {                                                        \
      return 1;                                                                \
    }                                                                          \
  } while (false)

int main() {
  DispatchReplaySimulator simulator;

  std::vector<DriverSnapshot> drivers;
  drivers.emplace_back(1, Point(0.0, 0.0, 1), 1, TaxiStatus::free, 0);

  std::vector<PassengerRequest> requests;
  requests.emplace_back(101, 1001, 10, Point(3.0, 4.0, 101),
                        Point(10.0, 0.0, 101), 1, 2);
  requests.emplace_back(102, 1002, 700, Point(10.0, 0.0, 102),
                        Point(20.0, 0.0, 102), 2, 3);

  DispatchReplayOptions options(
      0, 720, 30, 600,
      CandidateEdgeOptions(10.0, 10.0, 0, false));

  const DispatchReplayReport report =
      simulator.run_report(drivers, requests, options);
  const DispatchReplayMetrics metrics = report.metrics;
  REQUIRE(metrics.total_requests == 2);
  REQUIRE(metrics.assigned_requests == 2);
  REQUIRE(metrics.completed_requests == 2);
  REQUIRE(metrics.unserved_requests == 0);
  REQUIRE(metrics.batch_runs == 25);
  REQUIRE(metrics.candidate_edges_total == 2);
  REQUIRE(metrics.requests_with_edges_total == 2);
  REQUIRE(metrics.requests_without_edges_total == 0);
  REQUIRE(metrics.unique_requests_without_edges == 0);
  REQUIRE(metrics.mcmf_assigned_total == 2);
  REQUIRE(metrics.applied_pickup_cost_total == 50);
  REQUIRE(metrics.wait_time_total == 40);
  REQUIRE(assignment_rate(metrics) == 1.0);
  REQUIRE(completion_rate(metrics) == 1.0);
  REQUIRE(average_applied_pickup_cost(metrics) == 25.0);
  REQUIRE(average_assignment_wait_time(metrics) == 20.0);
  REQUIRE(report.batch_logs.size() == 25);

  bool saw_first_assignment_batch = false;
  for (const auto &log : report.batch_logs) {
    if (log.batch_time == 30) {
      REQUIRE(log.available_drivers == 1);
      REQUIRE(log.pending_requests == 1);
      REQUIRE(log.candidate_edges == 1);
      REQUIRE(log.greedy_assigned == 1);
      REQUIRE(log.mcmf_assigned == 1);
      REQUIRE(log.applied_assignments == 1);
      REQUIRE(log.applied_pickup_cost == 50);
      saw_first_assignment_batch = true;
    }
  }
  REQUIRE(saw_first_assignment_batch);

  const std::string summary = format_dispatch_replay_report(report);
  REQUIRE(summary.find("Dispatch replay summary") != std::string::npos);
  REQUIRE(summary.find("candidate_edges=2") != std::string::npos);

  std::vector<PassengerRequest> unserved_requests;
  unserved_requests.emplace_back(201, 2001, 10, Point(100.0, 100.0, 201),
                                 Point(200.0, 200.0, 201), 9, 9);

  DispatchReplayOptions no_candidate_options(
      0, 60, 30, 600, CandidateEdgeOptions(1.0, 10.0, 0, false));

  const DispatchReplayMetrics unserved_metrics =
      simulator.run(drivers, unserved_requests, no_candidate_options);
  REQUIRE(unserved_metrics.total_requests == 1);
  REQUIRE(unserved_metrics.assigned_requests == 0);
  REQUIRE(unserved_metrics.completed_requests == 0);
  REQUIRE(unserved_metrics.unserved_requests == 1);
  REQUIRE(unserved_metrics.candidate_edges_total == 0);
  REQUIRE(unserved_metrics.requests_without_edges_total == 2);
  REQUIRE(unserved_metrics.unique_requests_without_edges == 1);
  REQUIRE(unserved_metrics.mcmf_assigned_total == 0);

  return 0;
}
