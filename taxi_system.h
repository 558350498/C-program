#ifndef taxi_system_h
#define taxi_system_h

#include "hand_kd_tree.h"
#include <unordered_map>
#include <vector>

enum class TaxiStatus { free, occupy, offline };

class Taxi {
public:
  Taxi() : id(-1), x(0.0), y(0.0), status(TaxiStatus::free) {} // Default constructor required for unordered_map sometimes
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
  std::unique_ptr<KdNode> free_taxi_root;
  KnnPQ heap;
  Kd_Tree kd_tree;

  std::vector<std::vector<int>> taxi_to_customer;
  std::vector<int> customer_id_array;

public:
  void add_taxi(int id, double x, double y) {
    taxi_map.emplace(id, Taxi(id, x, y));
    Point coords(x, y, id);
    free_taxi_root =
        kd_tree.insert(std::move(free_taxi_root), coords, 0, false);
  }

  bool aabb_query(Taxi taxi, const Point &target, double radius) {
    BoundingBox box(target);
    box.max_coords[0] += radius;
    box.max_coords[1] += radius;
    box.min_coords[0] -= radius;
    box.min_coords[1] -= radius;
    if (box.min_dist_sq(target) > radius * radius)
      return false;
    return true;
  }

  void knn_query(const Point &target, double radius) {
    int customer_id = target.id;
    kd_tree.knn_search(free_taxi_root.get(), target, 1, 0, heap);
    while (!heap.empty()) {
      KnnEntry top = heap.top();
      heap.pop();
      if (aabb_query(taxi_map[top.point.id], top.point, radius)) {
        taxi_to_customer[customer_id].push_back(top.point.id);
      }
    }
  }

  void assign_taxi(int customer_id) {}
};

#endif // taxi_system_h
