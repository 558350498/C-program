#include "dispatch_batch.h"

#include <cassert>
#include <vector>

int main() {
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

  return 0;
}
