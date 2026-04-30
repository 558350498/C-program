#pragma once

#include <array>

enum class TaxiStatus { free, occupy, offline };

struct Point {
  std::array<double, 2> coords;
  int id;

  Point() : coords{0.0, 0.0}, id(-1) {}
  Point(double x, double y, int id_value) : coords{x, y}, id(id_value) {}
};

struct Taxi {
  int id;
  double x;
  double y;
  TaxiStatus status;

  Taxi() : id(-1), x(0.0), y(0.0), status(TaxiStatus::free) {}
  Taxi(int id_value, double x_value, double y_value,
       TaxiStatus status_value = TaxiStatus::free)
      : id(id_value), x(x_value), y(y_value), status(status_value) {}
};

double dist_sq(const Point &lhs, const Point &rhs);
Point make_point(const Taxi &taxi);
const char *to_string(TaxiStatus status);
