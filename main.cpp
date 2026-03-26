#include "taxi_system.h"
#include <iostream>

int main() {
  taxi_system sys;
  sys.add_taxi(1, 0.0, 0.0);
  std::cout << "Taxi system compiled successfully." << std::endl;
  return 0;
}