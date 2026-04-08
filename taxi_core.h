#ifndef taxi_core_h
#define taxi_core_h

#include "kd_tree.h"

#include <algorithm>
#include <memory>
#include <optional>
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

class ITaxiSpatialIndex {
public:
  virtual ~ITaxiSpatialIndex() = default;
  virtual bool insert(const Point &point) = 0;
  virtual bool remove(int id) = 0;
  virtual std::vector<Point> range_search(const Point &center, double radius) const = 0;
  virtual void clear() = 0;
};

class KdTreeSpatialIndex final : public ITaxiSpatialIndex {
public:
  bool insert(const Point &point) override { return tree_.insert(point); }
  bool remove(int id) override { return tree_.remove(id); }
  std::vector<Point> range_search(const Point &center, double radius) const override {
    return tree_.range_search(center, radius);
  }
  void clear() override { tree_.clear(); }

private:
  Kd_Tree tree_;
};

class IAssignmentPolicy {
public:
  virtual ~IAssignmentPolicy() = default;
  virtual std::optional<Point>
  choose_taxi(const std::vector<Point> &free_taxis,
              const Point &customer_location) const = 0;
};

class NearestTaxiPolicy final : public IAssignmentPolicy {
public:
  std::optional<Point>
  choose_taxi(const std::vector<Point> &free_taxis,
              const Point &customer_location) const override {
    if (free_taxis.empty()) {
      return std::nullopt;
    }

    auto best_it = std::min_element(
        free_taxis.begin(), free_taxis.end(),
        [&customer_location](const Point &a, const Point &b) {
          const double da = Dist_Calculateor::dist_sq(a, customer_location);
          const double db = Dist_Calculateor::dist_sq(b, customer_location);
          if (da != db) {
            return da < db;
          }
          return a.id < b.id;
        });

    return *best_it;
  }
};

class ITaxiSystem {
public:
  virtual ~ITaxiSystem() = default;
  virtual bool add_taxi(int id) = 0;
  virtual int new_taxi() = 0;
  virtual bool online(int id, double x, double y) = 0;
  virtual bool offline(int id) = 0;
  virtual bool update_taxi_position(int id, double x, double y) = 0;
  virtual bool update_taxi_status(int id, TaxiStatus new_status) = 0;
  virtual std::vector<Point> knn_query_free_taxi(const Point &customer_location,
                                                 double radius) = 0;
};

#endif // taxi_core_h
