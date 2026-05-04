#include "dispatch_replay.h"

#include <cstddef>
#include <string>
#include <vector>

#define REQUIRE(condition)                                                      \
  do {                                                                         \
    if (!(condition)) {                                                        \
      return 1;                                                                \
    }                                                                          \
  } while (false)

int main() {
  const auto same_metrics = [](const DispatchReplayMetrics &lhs,
                               const DispatchReplayMetrics &rhs) {
    return lhs.total_requests == rhs.total_requests &&
           lhs.assigned_requests == rhs.assigned_requests &&
           lhs.completed_requests == rhs.completed_requests &&
           lhs.unserved_requests == rhs.unserved_requests &&
           lhs.batch_runs == rhs.batch_runs &&
           lhs.candidate_edges_total == rhs.candidate_edges_total &&
           lhs.requests_with_edges_total == rhs.requests_with_edges_total &&
           lhs.requests_without_edges_total ==
               rhs.requests_without_edges_total &&
           lhs.unique_requests_without_edges ==
               rhs.unique_requests_without_edges &&
           lhs.greedy_assigned_total == rhs.greedy_assigned_total &&
           lhs.mcmf_assigned_total == rhs.mcmf_assigned_total &&
           lhs.greedy_cost_total == rhs.greedy_cost_total &&
           lhs.mcmf_cost_total == rhs.mcmf_cost_total &&
           lhs.applied_pickup_cost_total == rhs.applied_pickup_cost_total &&
           lhs.wait_time_total == rhs.wait_time_total;
  };

  const auto same_batch_logs =
      [](const std::vector<DispatchReplayBatchLog> &lhs,
         const std::vector<DispatchReplayBatchLog> &rhs) {
        if (lhs.size() != rhs.size()) {
          return false;
        }
        for (std::size_t index = 0; index < lhs.size(); ++index) {
          if (lhs[index].batch_time != rhs[index].batch_time ||
              lhs[index].available_drivers != rhs[index].available_drivers ||
              lhs[index].pending_requests != rhs[index].pending_requests ||
              lhs[index].candidate_edges != rhs[index].candidate_edges ||
              lhs[index].requests_with_edges != rhs[index].requests_with_edges ||
              lhs[index].requests_without_edges !=
                  rhs[index].requests_without_edges ||
              lhs[index].greedy_assigned != rhs[index].greedy_assigned ||
              lhs[index].mcmf_assigned != rhs[index].mcmf_assigned ||
              lhs[index].applied_assignments !=
                  rhs[index].applied_assignments ||
              lhs[index].greedy_cost != rhs[index].greedy_cost ||
              lhs[index].mcmf_cost != rhs[index].mcmf_cost ||
              lhs[index].applied_pickup_cost !=
                  rhs[index].applied_pickup_cost) {
            return false;
          }
        }
        return true;
      };
  const auto same_request_outcomes =
      [](const std::vector<DispatchReplayRequestOutcome> &lhs,
         const std::vector<DispatchReplayRequestOutcome> &rhs) {
        if (lhs.size() != rhs.size()) {
          return false;
        }
        for (std::size_t index = 0; index < lhs.size(); ++index) {
          if (lhs[index].request_id != rhs[index].request_id ||
              lhs[index].pending_batch_count !=
                  rhs[index].pending_batch_count ||
              lhs[index].candidate_batch_count !=
                  rhs[index].candidate_batch_count ||
              lhs[index].candidate_edge_count !=
                  rhs[index].candidate_edge_count ||
              lhs[index].assigned != rhs[index].assigned ||
              lhs[index].completed != rhs[index].completed ||
              lhs[index].taxi_id != rhs[index].taxi_id ||
              lhs[index].assignment_time != rhs[index].assignment_time ||
              lhs[index].pickup_time != rhs[index].pickup_time ||
              lhs[index].completion_time != rhs[index].completion_time ||
              lhs[index].wait_time != rhs[index].wait_time ||
              lhs[index].pickup_cost != rhs[index].pickup_cost) {
            return false;
          }
        }
        return true;
      };

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
  DispatchReplayOptions indexed_options(
      0, 720, 30, 600, CandidateEdgeOptions(10.0, 10.0, 0, false), true);
  const DispatchReplayReport indexed_report =
      simulator.run_report(drivers, requests, indexed_options);
  REQUIRE(same_metrics(report.metrics, indexed_report.metrics));
  REQUIRE(same_batch_logs(report.batch_logs, indexed_report.batch_logs));
  REQUIRE(same_request_outcomes(report.request_outcomes,
                                indexed_report.request_outcomes));

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
  REQUIRE(report.request_outcomes.size() == 2);
  REQUIRE(report.request_outcomes[0].request_id == 101);
  REQUIRE(report.request_outcomes[0].pending_batch_count == 1);
  REQUIRE(report.request_outcomes[0].candidate_batch_count == 1);
  REQUIRE(report.request_outcomes[0].candidate_edge_count == 1);
  REQUIRE(report.request_outcomes[0].assigned);
  REQUIRE(report.request_outcomes[0].completed);
  REQUIRE(report.request_outcomes[0].taxi_id == 1);
  REQUIRE(report.request_outcomes[0].assignment_time == 30);
  REQUIRE(report.request_outcomes[0].pickup_time == 80);
  REQUIRE(report.request_outcomes[0].completion_time == 680);
  REQUIRE(report.request_outcomes[0].wait_time == 20);
  REQUIRE(report.request_outcomes[0].pickup_cost == 50);
  REQUIRE(report.request_outcomes[1].request_id == 102);
  REQUIRE(report.request_outcomes[1].candidate_edge_count == 1);
  REQUIRE(report.request_outcomes[1].assigned);
  REQUIRE(report.request_outcomes[1].completed);

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
  const std::string batch_csv = format_dispatch_replay_batch_logs_csv(report);
  REQUIRE(batch_csv.find("batch_time,available_drivers,pending_requests") ==
          0);
  REQUIRE(batch_csv.find("30,1,1,1,1,0,1,50,1,50,1,50") !=
          std::string::npos);
  const std::string outcome_csv =
      format_dispatch_replay_request_outcomes_csv(report);
  REQUIRE(outcome_csv.find("request_id,pending_batch_count") == 0);
  REQUIRE(outcome_csv.find("101,1,1,1,1,1,1,1,30,80,680,20,50") !=
          std::string::npos);

  std::vector<PassengerRequest> unserved_requests;
  unserved_requests.emplace_back(201, 2001, 10, Point(100.0, 100.0, 201),
                                 Point(200.0, 200.0, 201), 9, 9);

  DispatchReplayOptions no_candidate_options(
      0, 60, 30, 600, CandidateEdgeOptions(1.0, 10.0, 0, false));

  const DispatchReplayReport unserved_report =
      simulator.run_report(drivers, unserved_requests, no_candidate_options);
  const DispatchReplayMetrics unserved_metrics = unserved_report.metrics;
  REQUIRE(unserved_metrics.total_requests == 1);
  REQUIRE(unserved_metrics.assigned_requests == 0);
  REQUIRE(unserved_metrics.completed_requests == 0);
  REQUIRE(unserved_metrics.unserved_requests == 1);
  REQUIRE(unserved_metrics.candidate_edges_total == 0);
  REQUIRE(unserved_metrics.requests_without_edges_total == 2);
  REQUIRE(unserved_metrics.unique_requests_without_edges == 1);
  REQUIRE(unserved_metrics.mcmf_assigned_total == 0);
  REQUIRE(unserved_report.request_outcomes.size() == 1);
  REQUIRE(unserved_report.request_outcomes[0].request_id == 201);
  REQUIRE(unserved_report.request_outcomes[0].pending_batch_count == 2);
  REQUIRE(unserved_report.request_outcomes[0].candidate_batch_count == 0);
  REQUIRE(unserved_report.request_outcomes[0].candidate_edge_count == 0);
  REQUIRE(!unserved_report.request_outcomes[0].assigned);
  REQUIRE(!unserved_report.request_outcomes[0].completed);

  return 0;
}
