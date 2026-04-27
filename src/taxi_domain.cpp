#include "taxi_domain.h"

double dist_sq(const Point &lhs, const Point &rhs) {
  const double dx = lhs.coords[0] - rhs.coords[0];
  const double dy = lhs.coords[1] - rhs.coords[1];
  return dx * dx + dy * dy;
}

Point make_point(const Taxi &taxi) { return Point(taxi.x, taxi.y, taxi.id); }

const char *to_string(TaxiStatus status) {
  switch (status) {
  case TaxiStatus::free:
    return "free";
  case TaxiStatus::occupy:
    return "occupy";
  case TaxiStatus::offline:
    return "offline";
  default:
    return "unknown";
  }
}
