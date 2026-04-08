#include "taxi_system.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <unordered_map>

namespace {

class FakeSpatialIndex final : public ITaxiSpatialIndex {
public:
  bool insert(const Point &point) override {
    points_[point.id] = point;
    return true;
  }

  bool remove(int id) override { return points_.erase(id) > 0; }

  std::vector<Point> range_search(const Point &center, double radius) const override {
    std::vector<Point> result;
    double rsq = radius * radius;
    for (const auto &[id, point] : points_) {
      (void)id;
      double dx = point.coords[0] - center.coords[0];
      double dy = point.coords[1] - center.coords[1];
      if (dx * dx + dy * dy <= rsq) {
        result.push_back(point);
      }
    }
    return result;
  }

  void clear() override { points_.clear(); }

private:
  std::unordered_map<int, Point> points_;
};

class HighestIdPolicy final : public IAssignmentPolicy {
public:
  std::optional<Point>
  choose_taxi(const std::vector<Point> &free_taxis,
              const Point &customer_location) const override {
    (void)customer_location;
    if (free_taxis.empty()) {
      return std::nullopt;
    }
    return *std::max_element(
        free_taxis.begin(), free_taxis.end(),
        [](const Point &a, const Point &b) { return a.id < b.id; });
  }
};

double dist_sq(double x1, double y1, double x2, double y2) {
  double dx = x1 - x2;
  double dy = y1 - y2;
  return dx * dx + dy * dy;
}

void interface_injection_test() {
  auto fake_index = std::make_unique<FakeSpatialIndex>();
  auto highest_id_policy = std::make_unique<HighestIdPolicy>();
  taxi_system sys(std::move(fake_index), std::move(highest_id_policy));

  assert(sys.add_taxi(1));
  assert(sys.add_taxi(2));
  assert(sys.online(1, 0.0, 0.0));
  assert(sys.online(2, 0.1, 0.1));

  auto assigned = sys.knn_query_free_taxi(Point(0.0, 0.0, -1), 1.0);
  assert(assigned.size() == 1);
  assert(assigned.front().id == 2);
}

void basic_flow_test() {
  taxi_system sys;
  assert(sys.add_taxi(1001));
  assert(!sys.add_taxi(1001));
  assert(sys.online(1001, 10.0, 20.0));
  assert(!sys.online(1001, 10.0, 20.0));

  auto assigned = sys.knn_query_free_taxi(Point(10.1, 20.1, -1), 1.0);
  assert(assigned.size() == 1);
  assert(assigned.front().id == 1001);

  const Taxi *taxi = sys.get_taxi(1001);
  assert(taxi != nullptr);
  assert(taxi->status == TaxiStatus::occupy);

  assert(sys.update_taxi_status(1001, TaxiStatus::free));
  assert(sys.update_taxi_position(1001, 15.0, 20.0));
  assert(sys.offline(1001));

  assigned = sys.knn_query_free_taxi(Point(15.0, 20.0, -1), 10.0);
  assert(assigned.empty());
}

void multi_agent_random_test() {
  taxi_system sys;
  std::unordered_map<int, Taxi> mirror;

  constexpr int kTaxiCount = 400;
  std::mt19937 rng(7);
  std::uniform_real_distribution<double> coord(-1000.0, 1000.0);
  std::uniform_real_distribution<double> radius(1.0, 200.0);
  std::uniform_int_distribution<int> id_dist(1, kTaxiCount);

  for (int id = 1; id <= kTaxiCount; ++id) {
    assert(sys.add_taxi(id));
    Taxi t(id, coord(rng), coord(rng));
    t.status = TaxiStatus::offline;
    mirror[id] = t;
    assert(sys.online(id, mirror[id].x, mirror[id].y));
    mirror[id].status = TaxiStatus::free;
  }

  for (int step = 0; step < 2000; ++step) {
    int op = step % 5;
    int id = id_dist(rng);

    if (op == 0) {
      double x = coord(rng), y = coord(rng);
      assert(sys.update_taxi_position(id, x, y));
      mirror[id].x = x;
      mirror[id].y = y;
    } else if (op == 1) {
      assert(sys.update_taxi_status(id, TaxiStatus::occupy));
      mirror[id].status = TaxiStatus::occupy;
    } else if (op == 2) {
      if (mirror[id].status == TaxiStatus::offline) {
        assert(sys.online(id, mirror[id].x, mirror[id].y));
        mirror[id].status = TaxiStatus::free;
      } else {
        assert(sys.update_taxi_status(id, TaxiStatus::free));
        mirror[id].status = TaxiStatus::free;
      }
    } else if (op == 3) {
      assert(sys.offline(id));
      mirror[id].status = TaxiStatus::offline;
    } else {
      Point customer(coord(rng), coord(rng), -1);
      double r = radius(rng);
      auto assigned = sys.knn_query_free_taxi(customer, r);

      int expected_id = -1;
      double best_dist = std::numeric_limits<double>::infinity();
      for (const auto &[tid, taxi] : mirror) {
        if (taxi.status != TaxiStatus::free) {
          continue;
        }
        double d = dist_sq(customer.coords[0], customer.coords[1], taxi.x, taxi.y);
        if (d <= r * r && (d < best_dist || (d == best_dist && tid < expected_id))) {
          expected_id = tid;
          best_dist = d;
        }
      }

      if (expected_id == -1) {
        assert(assigned.empty());
      } else {
        assert(assigned.size() == 1);
        assert(assigned.front().id == expected_id);
        mirror[expected_id].status = TaxiStatus::occupy;
      }
    }

    int mirror_free = 0;
    for (const auto &[tid, taxi] : mirror) {
      (void)tid;
      if (taxi.status == TaxiStatus::free) {
        ++mirror_free;
      }
    }
    assert(sys.free_taxi_count() == mirror_free);
  }
}

} // namespace

int main() {
  interface_injection_test();
  basic_flow_test();
  multi_agent_random_test();
  std::cout << "taxi_system debug test passed\n";
  return 0;
}
