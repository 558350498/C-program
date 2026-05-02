#include "dispatch_replay.h"
#include "dispatch_replay_io.h"

#include <cassert>
#include <cstdio>
#include <fstream>

namespace {

void write_file(const char *path, const char *content) {
  std::ofstream file(path);
  file << content;
}

} // namespace

int main() {
  const char *requests_path = "dispatch_replay_io_requests_test.csv";
  const char *drivers_path = "dispatch_replay_io_drivers_test.csv";
  const char *bad_requests_path = "dispatch_replay_io_bad_requests_test.csv";

  write_file(requests_path,
             "request_id,customer_id,request_time,pickup_x,pickup_y,"
             "dropoff_x,dropoff_y,pickup_tile,dropoff_tile\n"
             "101,1001,10,3.0,4.0,10.0,0.0,1,2\n"
             "102,1002,700,10.0,0.0,20.0,0.0,2,3\n");

  write_file(drivers_path,
             "taxi_id,x,y,tile,available_time,status\n"
             "1,0.0,0.0,1,0,free\n"
             "2,5.0,0.0,2,30,offline\n");

  const auto request_result = load_passenger_requests_csv(requests_path);
  assert(request_result.ok());
  assert(request_result.requests.size() == 2);
  assert(request_result.requests[0].request_id == 101);
  assert(request_result.requests[0].customer_id == 1001);
  assert(request_result.requests[0].request_time == 10);
  assert(request_result.requests[0].pickup_location.coords[0] == 3.0);
  assert(request_result.requests[0].pickup_tile == 1);

  const auto driver_result = load_driver_snapshots_csv(drivers_path);
  assert(driver_result.ok());
  assert(driver_result.drivers.size() == 2);
  assert(driver_result.drivers[0].taxi_id == 1);
  assert(driver_result.drivers[0].status == TaxiStatus::free);
  assert(driver_result.drivers[1].taxi_id == 2);
  assert(driver_result.drivers[1].status == TaxiStatus::offline);

  DispatchReplaySimulator simulator;
  const DispatchReplayOptions options(
      0, 720, 30, 600, CandidateEdgeOptions(10.0, 10.0, 0, false));
  const DispatchReplayReport report =
      simulator.run_report(driver_result.drivers, request_result.requests,
                           options);
  assert(report.metrics.total_requests == 2);
  assert(report.metrics.assigned_requests == 2);
  assert(report.metrics.completed_requests == 2);

  write_file(bad_requests_path,
             "request_id,customer_id,request_time,pickup_x,pickup_y,"
             "dropoff_x,dropoff_y,pickup_tile,dropoff_tile\n"
             "bad,1001,10,3.0,4.0,10.0,0.0,1,2\n"
             "103,1003,20,1.0,1.0,2.0,2.0,1,2\n");

  const auto bad_request_result =
      load_passenger_requests_csv(bad_requests_path);
  assert(!bad_request_result.ok());
  assert(bad_request_result.requests.size() == 1);
  assert(bad_request_result.errors.size() == 1);

  std::remove(requests_path);
  std::remove(drivers_path);
  std::remove(bad_requests_path);

  return 0;
}

