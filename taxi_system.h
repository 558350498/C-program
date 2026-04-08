#ifndef taxi_system_h
#define taxi_system_h

#include "dispatch_service.h"
#include "taxi_core.h"

#include <memory>
#include <unordered_map>
#include <vector>

class taxi_system : public ITaxiSystem {
private:
  std::unordered_map<int, Taxi> taxi_map;
  std::unique_ptr<ITaxiSpatialIndex> spatial_index_;
  std::unique_ptr<IAssignmentPolicy> assignment_policy_;

  std::vector<std::vector<int>> taxi_to_customer;
  std::unordered_map<int, int> customer_id_array;

public:
  explicit taxi_system(
      std::unique_ptr<ITaxiSpatialIndex> index = std::make_unique<KdTreeSpatialIndex>(),
      std::unique_ptr<IAssignmentPolicy> policy = std::make_unique<NearestTaxiPolicy>())
      : spatial_index_(std::move(index)), assignment_policy_(std::move(policy)) {}

  bool add_taxi(int id) override {
    if (taxi_map.count(id))
      return false;
    Taxi taxi(id, 0.0, 0.0);
    taxi.status = TaxiStatus::offline;
    taxi_map[id] = taxi;
    return true;
  }

  int new_taxi() override {
    int id = static_cast<int>(taxi_map.size());
    add_taxi(id);
    return id;
  }

  bool online(int id, double x, double y) override {
    auto it = taxi_map.find(id);
    if (it == taxi_map.end())
      return false;
    if (it->second.status != TaxiStatus::offline)
      return false;

    Taxi &taxi = it->second;
    taxi.x = x;
    taxi.y = y;

    if (!spatial_index_->insert(Point(taxi.x, taxi.y, id))) {
      rebuild_kd_tree_from_taxi_map();
      if (!spatial_index_->insert(Point(taxi.x, taxi.y, id))) {
        return false;
      }
    }

    taxi.status = TaxiStatus::free;
    return true;
  }

  bool offline(int id) override {
    auto it = taxi_map.find(id);
    if (it == taxi_map.end())
      return false;

    Taxi &taxi = it->second;
    if (taxi.status == TaxiStatus::offline)
      return true;

    if (taxi.status == TaxiStatus::free) {
      if (!spatial_index_->remove(id)) {
        rebuild_kd_tree_from_taxi_map();
      }
    }

    taxi.status = TaxiStatus::offline;
    return true;
  }

  bool update_taxi_position(int id, double x, double y) override {
    auto it = taxi_map.find(id);
    if (it == taxi_map.end())
      return false;

    Taxi &taxi = it->second;
    if (taxi.status == TaxiStatus::free) {
      if (!spatial_index_->remove(id)) {
        rebuild_kd_tree_from_taxi_map();
      }
    }

    taxi.x = x;
    taxi.y = y;

    if (taxi.status == TaxiStatus::free) {
      if (!spatial_index_->insert(Point(taxi.x, taxi.y, id))) {
        rebuild_kd_tree_from_taxi_map();
        if (!spatial_index_->insert(Point(taxi.x, taxi.y, id))) {
          return false;
        }
      }
    }

    return true;
  }

  std::vector<Point> knn_query_free_taxi(const Point &customer_location,
                                         double radius) override {
    return DispatchService::assign_one(
        *spatial_index_, taxi_map, *assignment_policy_, customer_location, radius,
        [this](int id, TaxiStatus status) { return update_taxi_status(id, status); },
        [this]() { rebuild_kd_tree_from_taxi_map(); });
  }

  bool update_taxi_status(int id, TaxiStatus new_status) override {
    auto it = taxi_map.find(id);
    if (it == taxi_map.end())
      return false;

    Taxi &taxi = it->second;
    if (taxi.status == new_status)
      return true;

    if (taxi.status == TaxiStatus::free && new_status != TaxiStatus::free) {
      bool is_removed = spatial_index_->remove(id);
      if (!is_removed) {
        rebuild_kd_tree_from_taxi_map();
        is_removed = spatial_index_->remove(id);
        if (!is_removed) {
          return false;
        }
      }
      taxi.status = new_status;
      return true;
    } else if (taxi.status != TaxiStatus::free && new_status == TaxiStatus::free) {
      bool is_inserted = spatial_index_->insert(Point(taxi.x, taxi.y, id));
      if (!is_inserted) {
        rebuild_kd_tree_from_taxi_map();
        is_inserted = spatial_index_->insert(Point(taxi.x, taxi.y, id));
        if (!is_inserted) {
          return false;
        }
      }
      taxi.status = new_status;
      return true;
    }

    taxi.status = new_status;
    return true;
  }

  void rebuild_kd_tree_from_taxi_map() {
    spatial_index_->clear();
    for (const auto &[id, taxi] : taxi_map) {
      (void)id;
      if (taxi.status == TaxiStatus::free) {
        spatial_index_->insert(Point(taxi.x, taxi.y, taxi.id));
      }
    }
  }

  std::size_t taxi_count() const { return taxi_map.size(); }

  int free_taxi_count() const {
    int count = 0;
    for (const auto &[id, taxi] : taxi_map) {
      (void)id;
      if (taxi.status == TaxiStatus::free) {
        ++count;
      }
    }
    return count;
  }

  const Taxi *get_taxi(int id) const {
    auto it = taxi_map.find(id);
    if (it == taxi_map.end()) {
      return nullptr;
    }
    return &it->second;
  }
};

#endif // taxi_system_h
