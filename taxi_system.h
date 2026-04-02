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
  std::unordered_map<int, int> customer_id_array;
  
  void add_taxi(int id) {
    if (taxi_map.count(id)) return;
    Taxi taxi(id, 0.0, 0.0);
    taxi.status = TaxiStatus::offline;
    taxi_map[id] = taxi;
  }

  void online(int id, double x, double y) {
    auto it = taxi_map.find(id);
    if (it == taxi_map.end()) return;
    
    Taxi &taxi = it->second;
    taxi.x = x;
    taxi.y = y;
    if (taxi.status != TaxiStatus::free) {
      Point coords(taxi.x, taxi.y, id);
      kd_tree.insert(coords);
    }
    taxi.status = TaxiStatus::free;
  }
public:
  void new_taxi() {
    int size_before = taxi_map.size();
    int id = size_before;
    add_taxi(id);
  }

  void knn_query(const Point &target, double radius) {
    int customer_id = target.id;
    if (customer_id < 0)
      return;
    if (customer_id >= static_cast<int>(taxi_to_customer.size()))
      taxi_to_customer.resize(customer_id + 1);

    std::vector<Point> nearby_taxis = kd_tree.range_search(target, radius);
    if(nearby_taxis.empty()) {
      // Handle the case where no nearby taxis are found
      // This could involve logging a message or taking other appropriate action
    } else {
      for (const Point &taxi_point : nearby_taxis) {
        taxi_to_customer[customer_id].push_back(taxi_point.id);
      }
    }
  }

  bool update_taxi_status(int id, TaxiStatus new_status) {
    auto it = taxi_map.find(id);
    if (it == taxi_map.end()) return;
    
    Taxi &taxi = it->second;
    if (taxi.status == new_status) return;
    
    if (taxi.status == TaxiStatus::free && new_status != TaxiStatus::free) {
      Point coords(taxi.x, taxi.y, id);
      bool removed = kd_tree.remove(id);
      if(!removed) {
        kd_tree.contains(id) ? rebuild_kd_tree_from_taxi_map() : taxi.status = new_status;
        taxi.status = new_status;
      }
    } else if (taxi.status != TaxiStatus::free && new_status == TaxiStatus::free) {
      Point coords(taxi.x, taxi.y, id);
      bool inserted = kd_tree.insert(coords);
      if(!inserted) {
        kd_tree.contains(id) ? taxi.status = new_status : rebuild_kd_tree_from_taxi_map();
      } else {
        taxi.status = new_status;
      }
    }
    
  }

  void rebuild_kd_tree_from_taxi_map() {
    std::vector<Point> points;
    kd_tree.clear(); // Clear the existing kd-tree before rebuilding
    for (const auto &taxi : taxi_map) {
      if (taxi.second.status == TaxiStatus::free) {
        points.emplace_back(taxi.second.x, taxi.second.y, taxi.first);
      }
    }
    std::unique_ptr<KdNode> new_root = kd_tree.build(points, 0, static_cast<int>(points.size()) - 1, 0);
    kd_tree.update_root_node(std::move(new_root));
  }
};

#endif // taxi_system_h
