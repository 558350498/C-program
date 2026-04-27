#include "taxi_system.h"

#include <iostream>

int main() {
  TaxiSystem system;

  const int taxi_a = system.create_taxi();
  const int taxi_b = system.create_taxi();

  system.set_taxi_online(taxi_a, 0.0, 0.0);
  system.set_taxi_online(taxi_b, 5.0, 0.0);

  const auto assigned = system.dispatch_nearest(1001, 1.0, 0.0, 10.0);
  if (assigned) {
    std::cout << "assigned taxi_id=" << *assigned << '\n';
  } else {
    std::cout << "no taxi assigned\n";
  }

  return 0;
}
