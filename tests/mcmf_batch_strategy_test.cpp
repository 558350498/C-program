#include "mcmf_batch_strategy.h"

#include <vector>

#define REQUIRE(condition)                                                      \
  do {                                                                         \
    if (!(condition)) {                                                        \
      return 1;                                                                \
    }                                                                          \
  } while (false)

namespace {

bool has_assignment(const std::vector<Assignment> &assignments, int taxi_id,
                    int request_id, int pickup_cost) {
  for (const auto &assignment : assignments) {
    if (assignment.taxi_id == taxi_id && assignment.request_id == request_id &&
        assignment.pickup_cost == pickup_cost) {
      return true;
    }
  }
  return false;
}

} // namespace

int main() {
  McmfBatchStrategy strategy;

  REQUIRE(strategy.assign(std::vector<CandidateEdge>()).empty());

  std::vector<CandidateEdge> greedy_trap_edges;
  greedy_trap_edges.emplace_back(1, 101, 1);
  greedy_trap_edges.emplace_back(1, 102, 2);
  greedy_trap_edges.emplace_back(2, 101, 2);

  const auto greedy_result = greedy_batch_assign(greedy_trap_edges);
  REQUIRE(greedy_result.size() == 1);
  REQUIRE(has_assignment(greedy_result, 1, 101, 1));

  const auto mcmf_result = strategy.assign(greedy_trap_edges);
  REQUIRE(mcmf_result.size() == 2);
  REQUIRE(has_assignment(mcmf_result, 2, 101, 2));
  REQUIRE(has_assignment(mcmf_result, 1, 102, 2));

  std::vector<CandidateEdge> min_cost_edges;
  min_cost_edges.emplace_back(1, 201, 10);
  min_cost_edges.emplace_back(1, 202, 1);
  min_cost_edges.emplace_back(2, 201, 2);
  min_cost_edges.emplace_back(2, 202, 9);

  const auto min_cost_result = strategy.assign(min_cost_edges);
  REQUIRE(min_cost_result.size() == 2);
  REQUIRE(has_assignment(min_cost_result, 2, 201, 2));
  REQUIRE(has_assignment(min_cost_result, 1, 202, 1));

  std::vector<CandidateEdge> invalid_edges;
  invalid_edges.emplace_back(-1, 301, 1);
  invalid_edges.emplace_back(1, -1, 1);
  invalid_edges.emplace_back(1, 301, -1);
  REQUIRE(strategy.assign(invalid_edges).empty());

  std::vector<DriverSnapshot> drivers;
  drivers.emplace_back(1, Point(0.0, 0.0, 1), 1, TaxiStatus::free, 100);
  drivers.emplace_back(2, Point(2.0, 0.0, 2), 1, TaxiStatus::free, 100);

  std::vector<PassengerRequest> requests;
  requests.emplace_back(401, 4001, 100, Point(0.0, 0.0, 401),
                        Point(10.0, 0.0, 401), 1, 1);
  requests.emplace_back(402, 4002, 100, Point(2.0, 0.0, 402),
                        Point(10.0, 0.0, 402), 1, 1);

  BatchDispatchInput batch(100, drivers, requests);
  const CandidateEdgeOptions options(3.0, 10.0, 0, true);
  const auto batch_result = strategy.assign(batch, options);
  REQUIRE(batch_result.size() == 2);
  REQUIRE(has_assignment(batch_result, 1, 401, 0));
  REQUIRE(has_assignment(batch_result, 2, 402, 0));

  return 0;
}
