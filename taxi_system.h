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
    if (it->second.status != TaxiStatus::offline) return;
    
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
    taxi_to_customer.clear();
    int customer_id = target.id;
    if (customer_id < 0)
      return;
    if (customer_id >= static_cast<int>(taxi_to_customer.size()))
      taxi_to_customer.resize(customer_id + 1);

    std::vector<Point> nearby_taxis = kd_tree.range_search(target, radius);
    if(nearby_taxis.empty()) {
      knn_query(target, radius * 2);
    } else {
      for (const Point &taxi_point : nearby_taxis) {
        taxi_to_customer[customer_id].push_back(taxi_point.id);
      }
    }
  }

  bool update_taxi_status(int id, TaxiStatus new_status) {
    auto it = taxi_map.find(id);
    if (it == taxi_map.end()) return false;
    
    Taxi &taxi = it->second;
    if (taxi.status == new_status) return true;
    
    if (taxi.status == TaxiStatus::free && new_status != TaxiStatus::free) {
      bool is_removed = kd_tree.remove(id);
      if (!is_removed) {
        rebuild_kd_tree_from_taxi_map();
        is_removed = kd_tree.remove(id);
        if (!is_removed) {
          return false;
        }
        taxi.status = new_status;
        return true;
      }
      taxi.status = new_status;
      return true;
    } else if (taxi.status != TaxiStatus::free && new_status == TaxiStatus::free) {
      bool is_inserted = kd_tree.insert(Point(taxi.x, taxi.y, id));
      if (!is_inserted) {
        rebuild_kd_tree_from_taxi_map();
        is_inserted = kd_tree.insert(Point(taxi.x, taxi.y, id));
        if (!is_inserted) {
          return false;
        }
        taxi.status = new_status;
        return true;
      }
      taxi.status = new_status;
      return true;
    }

    taxi.status = new_status;
    return true;
  }

  void rebuild_kd_tree_from_taxi_map() {
    kd_tree.clear();
    for(const auto& [id, taxi] : taxi_map) {
      if (taxi.status == TaxiStatus::free) {
        kd_tree.insert(Point(taxi.x, taxi.y, id));
      }
    }
    return;
  }
};

#endif // taxi_system_h
