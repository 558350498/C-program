#include "taxi_system.h"

int main() {
  taxi_system system;
  return system.create_taxi() < 0;
}
