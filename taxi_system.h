#ifndef taxi_system_h
#define taxi_system_h

#include "kd_tree.h"
#include <unordered_map>
#include <vector>

enum class TaxiStatus { free, occupy, offline };

class Taxi {
public:
  Taxi() : id(-1), x(0.0), y(0.0), status(TaxiStatus::free) {}
  Taxi(int id, double x, double y)
      : id(id), x(x), y(y), status(TaxiStatus::free) {}

  int id;
  double x;
  double y;
  TaxiStatus status;
};

class taxi_system {
private:
  std::unordered_map<int, Taxi> taxi_map;
  Kd_Tree kd_tree;

  std::vector<std::vector<int>> taxi_to_customer;
  std::vector<int> customer_id_array;

public:
  void add_taxi(int id, double x, double y) {
    auto [it, inserted] = taxi_map.emplace(id, Taxi(id, x, y));
    if (!inserted)
      return;

    Point coords(x, y, id);
    kd_tree.insert(coords);
  }

  void knn_query(const Point &target, double radius) {
    int customer_id = target.id;
    if (customer_id < 0)
      return;
    if (customer_id >= static_cast<int>(taxi_to_customer.size()))
      taxi_to_customer.resize(customer_id + 1);

    taxi_to_customer[customer_id].clear();
    auto nearest = kd_tree.knn(target, 1);
    if (nearest.empty())
      return;

    if (Dist_Calculateor::dist_sq(nearest.front(), target) <= radius * radius)
      taxi_to_customer[customer_id].push_back(nearest.front().id);
  }

  void assign_taxi(int customer_id) {}
};

#endif // taxi_system_h
