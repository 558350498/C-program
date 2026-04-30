#include "dispatch_replay.h"

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

  const DispatchReplayMetrics metrics =
      simulator.run(drivers, requests, options);
  REQUIRE(metrics.total_requests == 2);
  REQUIRE(metrics.assigned_requests == 2);
  REQUIRE(metrics.completed_requests == 2);
  REQUIRE(metrics.unserved_requests == 0);
  REQUIRE(metrics.batch_runs == 25);
  REQUIRE(metrics.mcmf_assigned_total == 2);
  REQUIRE(metrics.applied_pickup_cost_total == 50);

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
  REQUIRE(unserved_metrics.mcmf_assigned_total == 0);

  return 0;
}
