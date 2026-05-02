#include "dispatch_replay.h"
#include "taxi_system.h"

#include <iostream>
#include <vector>

int main() {
  TaxiSystem system;
  system.set_logging_enabled(false);

  const int taxi_a = system.create_taxi();
  const int taxi_b = system.create_taxi();

  system.set_taxi_online(taxi_a, 0.0, 0.0);
  system.set_taxi_online(taxi_b, 5.0, 0.0);

  RequestContext request(1001, 1001, Point(1.0, 0.0, 1001),
                         Point(4.0, 0.0, 1001));
  const auto assigned = system.dispatch_nearest(request, 10.0);
  if (assigned) {
    std::cout << "assigned taxi_id=" << *assigned << '\n';
    system.start_trip(request);
    system.complete_trip(request);
  } else {
    std::cout << "no taxi assigned\n";
  }

  DispatchReplaySimulator simulator;
  std::vector<DriverSnapshot> drivers;
  drivers.emplace_back(1, Point(0.0, 0.0, 1), 1, TaxiStatus::free, 0);

  std::vector<PassengerRequest> requests;
  requests.emplace_back(101, 1001, 10, Point(3.0, 4.0, 101),
                        Point(10.0, 0.0, 101), 1, 2);
  requests.emplace_back(102, 1002, 700, Point(10.0, 0.0, 102),
                        Point(20.0, 0.0, 102), 2, 3);

  const DispatchReplayOptions replay_options(
      0, 720, 30, 600, CandidateEdgeOptions(10.0, 10.0));
  const DispatchReplayReport report =
      simulator.run_report(drivers, requests, replay_options);
  std::cout << format_dispatch_replay_report(report, false);

  return 0;
}
