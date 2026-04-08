#include "kd_tree.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <random>
#include <unordered_map>
#include <vector>

namespace {

double dist_sq(const Point &a, const Point &b) {
  return Dist_Calculateor::dist_sq(a, b);
}

std::vector<Point> brute_range(const std::unordered_map<int, Point> &active,
                               const Point &center, double radius) {
  std::vector<Point> result;
  const double radius_sq = radius * radius;
  for (const auto &[id, p] : active) {
    (void)id;
    if (dist_sq(p, center) <= radius_sq) {
      result.push_back(p);
    }
  }
  std::sort(result.begin(), result.end(), [](const Point &a, const Point &b) {
    return a.id < b.id;
  });
  return result;
}

std::vector<Point> brute_knn(const std::unordered_map<int, Point> &active,
                             const Point &target, int k) {
  std::vector<Point> points;
  points.reserve(active.size());
  for (const auto &[id, p] : active) {
    (void)id;
    points.push_back(p);
  }
  std::sort(points.begin(), points.end(), [&target](const Point &a, const Point &b) {
    double da = dist_sq(a, target);
    double db = dist_sq(b, target);
    if (da != db) {
      return da < db;
    }
    return a.id < b.id;
  });
  if (k < static_cast<int>(points.size())) {
    points.resize(k);
  }
  return points;
}

void assert_same_ids(std::vector<Point> a, std::vector<Point> b) {
  std::sort(a.begin(), a.end(), [](const Point &x, const Point &y) { return x.id < y.id; });
  std::sort(b.begin(), b.end(), [](const Point &x, const Point &y) { return x.id < y.id; });
  assert(a.size() == b.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    assert(a[i].id == b[i].id);
  }
}

void run_basic_regression() {
  Kd_Tree tree;

  assert(tree.knn(Point(0.0, 0.0, -1), 0).empty());
  assert(tree.range_search(Point(0.0, 0.0, -1), -1.0).empty());

  for (int i = 0; i < 8; ++i) {
    assert(tree.insert(Point(static_cast<double>(i), 0.0, i + 1)));
  }

  auto nearest = tree.knn(Point(3.1, 0.0, 100), 1);
  assert(nearest.size() == 1);
  assert(nearest.front().id == 4);

  auto range = tree.range_search(Point(3.0, 0.0, 200), 1.01);
  assert(range.size() == 3);

  for (int i = 0; i < 8; ++i) {
    assert(tree.remove(i + 1));
  }

  assert(tree.knn(Point(3.0, 0.0, 300), 1).empty());
  assert(tree.range_search(Point(3.0, 0.0, 300), 100.0).empty());
  assert(tree.total_size() == 0);
}

void run_randomized_multi_agent_checks() {
  Kd_Tree tree;
  std::unordered_map<int, Point> active;

  constexpr int kAgentCount = 600;
  std::mt19937 rng(42);
  std::uniform_real_distribution<double> coord(-500.0, 500.0);

  for (int id = 1; id <= kAgentCount; ++id) {
    Point p(coord(rng) + id * 1e-4, coord(rng) - id * 1e-4, id);
    assert(tree.insert(p));
    active[id] = p;
  }

  std::uniform_int_distribution<int> id_dist(1, kAgentCount);
  std::uniform_real_distribution<double> radius_dist(0.0, 300.0);

  for (int step = 0; step < 1200; ++step) {
    int op = step % 3;

    if (op == 0) {
      int id = id_dist(rng);
      bool existed = active.count(id) > 0;
      bool removed = tree.remove(id);
      assert(removed == existed);
      if (existed) {
        active.erase(id);
      }
    } else if (op == 1) {
      int id = id_dist(rng);
      Point p(coord(rng) + id * 1e-4, coord(rng) - id * 1e-4, id);
      bool existed = active.count(id) > 0;
      bool inserted = tree.insert(p);
      assert(inserted != existed);
      if (!existed) {
        active[id] = p;
      }
    } else {
      Point query(coord(rng), coord(rng), -1);
      int k = 1 + (step % 7);
      auto got_knn = tree.knn(query, k);
      auto expect_knn = brute_knn(active, query, k);

      assert(got_knn.size() == expect_knn.size());
      for (std::size_t i = 0; i < got_knn.size(); ++i) {
        assert(got_knn[i].id == expect_knn[i].id);
      }

      double radius = radius_dist(rng);
      auto got_range = tree.range_search(query, radius);
      auto expect_range = brute_range(active, query, radius);
      assert_same_ids(got_range, expect_range);
    }

    assert(tree.active_size() == static_cast<int>(active.size()));
  }
}

}  // namespace

int main() {
  run_basic_regression();
  run_randomized_multi_agent_checks();
  std::cout << "kd_tree debug test passed\n";
  return 0;
}
