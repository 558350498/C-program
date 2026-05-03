#include "dispatch_batch.h"

#include <cassert>
#include <vector>

int main() {
  const auto assert_same_edges = [](const std::vector<CandidateEdge> &lhs,
                                    const std::vector<CandidateEdge> &rhs) {
    assert(lhs.size() == rhs.size());
    for (std::size_t index = 0; index < lhs.size(); ++index) {
      assert(lhs[index].taxi_id == rhs[index].taxi_id);
      assert(lhs[index].request_id == rhs[index].request_id);
      assert(lhs[index].pickup_cost == rhs[index].pickup_cost);
    }
  };

  const auto assert_same_stats = [](const CandidateEdgeStats &lhs,
                                    const CandidateEdgeStats &rhs) {
    assert(lhs.total_drivers == rhs.total_drivers);
    assert(lhs.total_requests == rhs.total_requests);
    assert(lhs.available_drivers == rhs.available_drivers);
    assert(lhs.ready_requests == rhs.ready_requests);
    assert(lhs.candidate_edges == rhs.candidate_edges);
    assert(lhs.requests_with_edges == rhs.requests_with_edges);
    assert(lhs.requests_without_edges == rhs.requests_without_edges);
  };

  PassengerRequest default_request;
  assert(default_request.request_id == -1);
  assert(default_request.customer_id == -1);
  assert(default_request.pickup_tile == invalid_tile_id);
  assert(default_request.dropoff_tile == invalid_tile_id);

  PassengerRequest request(100, 7, 3600, Point(1.0, 2.0, 100),
                           Point(3.0, 4.0, 100), 12, 19);
  assert(request.request_id == 100);
  assert(request.customer_id == 7);
  assert(request.request_time == 3600);
  assert(request.pickup_location.coords[0] == 1.0);
  assert(request.dropoff_location.coords[1] == 4.0);
  assert(request.pickup_tile == 12);
  assert(request.dropoff_tile == 19);

  TripRecord trip(501, 9, 3700, 4200, 12, 19);
  assert(trip.trip_id == 501);
  assert(trip.taxi_id == 9);
  assert(trip.pickup_time == 3700);
  assert(trip.dropoff_time == 4200);

  DriverSnapshot driver(9, Point(0.5, 2.0, 9), 12, TaxiStatus::free, 3600);
  assert(driver.taxi_id == 9);
  assert(driver.current_tile == 12);
  assert(driver.status == TaxiStatus::free);
  assert(driver.available_time == 3600);

  CandidateEdge edge(9, 100, 180);
  assert(edge.taxi_id == 9);
  assert(edge.request_id == 100);
  assert(edge.pickup_cost == 180);

  Assignment assignment(9, 100, 180);
  assert(assignment.taxi_id == 9);
  assert(assignment.request_id == 100);
  assert(assignment.pickup_cost == 180);

  std::vector<DriverSnapshot> drivers;
  drivers.push_back(driver);
  std::vector<PassengerRequest> requests;
  requests.push_back(request);

  BatchDispatchInput batch(3600, drivers, requests);
  assert(batch.batch_time == 3600);
  assert(batch.drivers.size() == 1);
  assert(batch.requests.size() == 1);
  assert(batch.drivers[0].taxi_id == 9);
  assert(batch.requests[0].request_id == 100);

  assert(is_available_for_batch(driver, 3600));
  assert(!is_available_for_batch(
      DriverSnapshot(10, Point(0.0, 0.0, 10), 12, TaxiStatus::occupy, 3600),
      3600));
  assert(!is_available_for_batch(
      DriverSnapshot(11, Point(0.0, 0.0, 11), 12, TaxiStatus::free, 3700),
      3600));
  assert(is_valid_batch_request(request));
  assert(!is_valid_batch_request(PassengerRequest()));
  assert(is_request_ready_for_batch(request, 3600));
  assert(!is_request_ready_for_batch(request, 3599));
  assert(is_same_pickup_tile(driver, request));

  assert(estimate_pickup_cost(Point(0.0, 0.0, 1), Point(3.0, 4.0, 2), 10.0) ==
         50);

  std::vector<DriverSnapshot> batch_drivers;
  batch_drivers.emplace_back(1, Point(0.0, 0.0, 1), 1, TaxiStatus::free, 100);
  batch_drivers.emplace_back(2, Point(5.0, 0.0, 2), 2, TaxiStatus::free, 100);
  batch_drivers.emplace_back(3, Point(1.0, 0.0, 3), 1, TaxiStatus::occupy,
                             100);
  batch_drivers.emplace_back(4, Point(2.0, 0.0, 4), 1, TaxiStatus::free, 200);

  std::vector<PassengerRequest> batch_requests;
  batch_requests.emplace_back(101, 1001, 100, Point(0.9, 0.0, 101),
                              Point(9.0, 9.0, 101), 1, 9);
  batch_requests.emplace_back(102, 1002, 100, Point(4.2, 0.0, 102),
                              Point(9.0, 9.0, 102), 2, 9);
  batch_requests.emplace_back(103, 1003, 100, Point(20.0, 0.0, 103),
                              Point(9.0, 9.0, 103), 3, 9);
  batch_requests.emplace_back(104, 1004, 200, Point(0.1, 0.0, 104),
                              Point(9.0, 9.0, 104), 1, 9);

  BatchDispatchInput matching_batch(100, batch_drivers, batch_requests);
  const auto candidate_edges =
      generate_candidate_edges(matching_batch, 2.0, 10.0);
  assert(candidate_edges.size() == 2);
  assert(candidate_edges[0].taxi_id == 1);
  assert(candidate_edges[0].request_id == 101);
  assert(candidate_edges[0].pickup_cost == 9);
  assert(candidate_edges[1].taxi_id == 2);
  assert(candidate_edges[1].request_id == 102);
  assert(candidate_edges[1].pickup_cost == 8);

  const auto candidate_result = generate_candidate_edges_with_stats(
      matching_batch, CandidateEdgeOptions(2.0, 10.0));
  assert(candidate_result.stats.total_drivers == 4);
  assert(candidate_result.stats.total_requests == 4);
  assert(candidate_result.stats.available_drivers == 2);
  assert(candidate_result.stats.ready_requests == 3);
  assert(candidate_result.stats.candidate_edges == 2);
  assert(candidate_result.stats.requests_with_edges == 2);
  assert(candidate_result.stats.requests_without_edges == 1);

  const auto indexed_candidate_result =
      generate_candidate_edges_indexed_with_stats(
          matching_batch, CandidateEdgeOptions(2.0, 10.0));
  assert_same_stats(candidate_result.stats, indexed_candidate_result.stats);
  assert_same_edges(candidate_result.edges, indexed_candidate_result.edges);

  assert(generate_candidate_edges(matching_batch, -1.0).empty());
  assert(generate_candidate_edges(matching_batch, 2.0, 0.0).empty());
  const auto bad_radius_indexed = generate_candidate_edges_indexed_with_stats(
      matching_batch, CandidateEdgeOptions(-1.0, 10.0));
  assert(bad_radius_indexed.edges.empty());
  assert(bad_radius_indexed.stats.total_drivers == 4);
  assert(bad_radius_indexed.stats.total_requests == 4);
  assert(bad_radius_indexed.stats.available_drivers == 2);
  const auto bad_cost_indexed = generate_candidate_edges_indexed_with_stats(
      matching_batch, CandidateEdgeOptions(2.0, 0.0));
  assert(bad_cost_indexed.edges.empty());
  assert(bad_cost_indexed.stats.total_drivers == 4);
  assert(bad_cost_indexed.stats.total_requests == 4);
  assert(bad_cost_indexed.stats.available_drivers == 2);

  std::vector<DriverSnapshot> dense_drivers;
  dense_drivers.emplace_back(1, Point(0.0, 0.0, 1), 1, TaxiStatus::free, 100);
  dense_drivers.emplace_back(2, Point(0.5, 0.0, 2), 1, TaxiStatus::free, 100);
  dense_drivers.emplace_back(3, Point(1.0, 0.0, 3), 2, TaxiStatus::free, 100);

  std::vector<PassengerRequest> dense_requests;
  dense_requests.emplace_back(201, 2001, 100, Point(0.4, 0.0, 201),
                              Point(9.0, 9.0, 201), 1, 9);

  BatchDispatchInput dense_batch(100, dense_drivers, dense_requests);

  const CandidateEdgeOptions top_one_options(2.0, 10.0, 1);
  const auto top_one_edges =
      generate_candidate_edges(dense_batch, top_one_options);
  assert(top_one_edges.size() == 1);
  assert(top_one_edges[0].taxi_id == 2);
  assert(top_one_edges[0].request_id == 201);
  assert(top_one_edges[0].pickup_cost == 1);
  assert_same_edges(top_one_edges,
                    generate_candidate_edges_indexed(dense_batch,
                                                     top_one_options));

  const CandidateEdgeOptions same_tile_options(2.0, 10.0, 0, true);
  const auto same_tile_edges =
      generate_candidate_edges(dense_batch, same_tile_options);
  assert(same_tile_edges.size() == 2);
  assert(same_tile_edges[0].taxi_id == 2);
  assert(same_tile_edges[1].taxi_id == 1);
  assert_same_edges(same_tile_edges,
                    generate_candidate_edges_indexed(dense_batch,
                                                     same_tile_options));

  std::vector<CandidateEdge> duplicate_edges;
  duplicate_edges.emplace_back(1, 201, 20);
  duplicate_edges.emplace_back(1, 201, 10);
  duplicate_edges.emplace_back(2, 201, 15);
  duplicate_edges.emplace_back(-1, 201, 1);
  duplicate_edges.emplace_back(1, -1, 1);
  duplicate_edges.emplace_back(1, 201, -1);

  const auto normalized_edges = normalize_candidate_edges(duplicate_edges);
  assert(normalized_edges.size() == 2);
  assert(normalized_edges[0].taxi_id == 1);
  assert(normalized_edges[0].request_id == 201);
  assert(normalized_edges[0].pickup_cost == 10);
  assert(normalized_edges[1].taxi_id == 2);
  assert(normalized_edges[1].request_id == 201);
  assert(normalized_edges[1].pickup_cost == 15);

  std::vector<CandidateEdge> greedy_edges;
  greedy_edges.emplace_back(1, 101, 50);
  greedy_edges.emplace_back(2, 101, 20);
  greedy_edges.emplace_back(2, 102, 10);
  greedy_edges.emplace_back(3, 102, 30);

  const auto greedy_assignments = greedy_batch_assign(greedy_edges);
  assert(greedy_assignments.size() == 2);
  assert(greedy_assignments[0].taxi_id == 2);
  assert(greedy_assignments[0].request_id == 102);
  assert(greedy_assignments[0].pickup_cost == 10);
  assert(greedy_assignments[1].taxi_id == 1);
  assert(greedy_assignments[1].request_id == 101);
  assert(greedy_assignments[1].pickup_cost == 50);

  const auto baseline_assignments =
      greedy_batch_assign(matching_batch, 2.0, 10.0);
  assert(baseline_assignments.size() == 2);
  assert(baseline_assignments[0].taxi_id == 2);
  assert(baseline_assignments[0].request_id == 102);
  assert(baseline_assignments[1].taxi_id == 1);
  assert(baseline_assignments[1].request_id == 101);

  return 0;
}
