#include "kd_tree_spatial_index.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

namespace {

class BoundingBox {
public:
  BoundingBox()
      : min_coords{INFINITY, INFINITY}, max_coords{-INFINITY, -INFINITY} {}

  explicit BoundingBox(const Point &point)
      : min_coords(point.coords), max_coords(point.coords) {}

  void extend(const Point &point) {
    for (int dim = 0; dim < 2; ++dim) {
      min_coords[dim] = std::min(min_coords[dim], point.coords[dim]);
      max_coords[dim] = std::max(max_coords[dim], point.coords[dim]);
    }
  }

  void extend(const BoundingBox &other) {
    for (int dim = 0; dim < 2; ++dim) {
      min_coords[dim] = std::min(min_coords[dim], other.min_coords[dim]);
      max_coords[dim] = std::max(max_coords[dim], other.max_coords[dim]);
    }
  }

  double min_dist_sq(const Point &target) const {
    double distance = 0.0;
    for (int dim = 0; dim < 2; ++dim) {
      const double clamped = std::max(
          min_coords[dim], std::min(target.coords[dim], max_coords[dim]));
      const double delta = target.coords[dim] - clamped;
      distance += delta * delta;
    }
    return distance;
  }

  std::array<double, 2> min_coords;
  std::array<double, 2> max_coords;
};

struct KdNode {
  explicit KdNode(const Point &point_value)
      : point(point_value), active_count(1), total_count(1), is_deleted(false),
        box(point_value) {}

  void refresh() {
    active_count = is_deleted ? 0 : 1;
    total_count = 1;
    box = BoundingBox(point);

    if (left) {
      active_count += left->active_count;
      total_count += left->total_count;
      box.extend(left->box);
    }
    if (right) {
      active_count += right->active_count;
      total_count += right->total_count;
      box.extend(right->box);
    }
  }

  Point point;
  std::unique_ptr<KdNode> left;
  std::unique_ptr<KdNode> right;
  int active_count;
  int total_count;
  bool is_deleted;
  BoundingBox box;
};

constexpr double kAlpha = 0.75;

bool less_on_dim(const Point &lhs, const Point &rhs, int dim) {
  if (lhs.coords[dim] != rhs.coords[dim]) {
    return lhs.coords[dim] < rhs.coords[dim];
  }

  const int other_dim = dim ^ 1;
  if (lhs.coords[other_dim] != rhs.coords[other_dim]) {
    return lhs.coords[other_dim] < rhs.coords[other_dim];
  }

  return lhs.id < rhs.id;
}

int total_count(const std::unique_ptr<KdNode> &node) {
  return node ? node->total_count : 0;
}

bool is_unbalanced(const KdNode *node) {
  if (!node) {
    return false;
  }

  const int left_total = total_count(node->left);
  const int right_total = total_count(node->right);
  return left_total > kAlpha * node->total_count + 1 ||
         right_total > kAlpha * node->total_count + 1;
}

bool needs_rebuild(const KdNode *node) {
  if (!node) {
    return false;
  }
  return node->active_count < (1.0 - kAlpha) * node->total_count;
}

void flatten(const KdNode *node, std::vector<Point> &points) {
  if (!node) {
    return;
  }

  if (!node->is_deleted) {
    points.push_back(node->point);
  }

  flatten(node->left.get(), points);
  flatten(node->right.get(), points);
}

std::unique_ptr<KdNode> build_rec(std::vector<Point> &points, int left, int right,
                                  int depth) {
  if (left > right) {
    return nullptr;
  }

  const int dim = depth % 2;
  const int mid = left + (right - left) / 2;
  std::nth_element(points.begin() + left, points.begin() + mid,
                   points.begin() + right + 1,
                   [dim](const Point &lhs, const Point &rhs) {
                     return less_on_dim(lhs, rhs, dim);
                   });

  auto node = std::make_unique<KdNode>(points[mid]);
  node->left = build_rec(points, left, mid - 1, depth + 1);
  node->right = build_rec(points, mid + 1, right, depth + 1);
  node->refresh();
  return node;
}

std::unique_ptr<KdNode> rebuild_subtree(std::unique_ptr<KdNode> node, int depth) {
  std::vector<Point> points;
  flatten(node.get(), points);
  if (points.empty()) {
    return nullptr;
  }
  return build_rec(points, 0, static_cast<int>(points.size()) - 1, depth);
}

void insert_rec(std::unique_ptr<KdNode> &node, const Point &point, int depth) {
  if (!node) {
    node = std::make_unique<KdNode>(point);
    return;
  }

  const int dim = depth % 2;
  if (less_on_dim(point, node->point, dim)) {
    insert_rec(node->left, point, depth + 1);
  } else {
    insert_rec(node->right, point, depth + 1);
  }

  node->refresh();
  if (is_unbalanced(node.get())) {
    node = rebuild_subtree(std::move(node), depth);
  }
}

void remove_rec(std::unique_ptr<KdNode> &node, const Point &point, int depth,
                bool &removed) {
  if (!node) {
    return;
  }

  if (node->point.id == point.id && !node->is_deleted) {
    node->is_deleted = true;
    removed = true;
  } else {
    const int dim = depth % 2;
    if (less_on_dim(point, node->point, dim)) {
      remove_rec(node->left, point, depth + 1, removed);
    } else {
      remove_rec(node->right, point, depth + 1, removed);
    }
  }

  node->refresh();
  if (needs_rebuild(node.get())) {
    node = rebuild_subtree(std::move(node), depth);
  }
}

void range_search_rec(const KdNode *node, const Point &center, double radius_sq,
                      std::vector<Point> &result) {
  if (!node) {
    return;
  }
  if (node->box.min_dist_sq(center) > radius_sq) {
    return;
  }

  if (!node->is_deleted && dist_sq(node->point, center) <= radius_sq) {
    result.push_back(node->point);
  }

  range_search_rec(node->left.get(), center, radius_sq, result);
  range_search_rec(node->right.get(), center, radius_sq, result);
}

} // namespace

class KdTreeSpatialIndex::Impl {
public:
  bool contains(int id) const { return points_by_id.count(id) > 0; }

  bool insert_new(const Point &point) {
    if (contains(point.id)) {
      return false;
    }
    points_by_id.emplace(point.id, point);
    insert_rec(root, point, 0);
    return true;
  }

  bool erase_existing(int id) {
    auto it = points_by_id.find(id);
    if (it == points_by_id.end()) {
      return false;
    }

    bool removed = false;
    remove_rec(root, it->second, 0, removed);
    points_by_id.erase(it);

    if (!removed) {
      std::clog << "[KdTreeSpatialIndex::erase] taxi_id=" << id
                << " missing from tree, rebuilding index\n";
      rebuild_from_active_points();
    }
    return true;
  }

  void rebuild_from_active_points() {
    std::vector<Point> points;
    points.reserve(points_by_id.size());
    for (const auto &[id, point] : points_by_id) {
      (void)id;
      points.push_back(point);
    }

    if (points.empty()) {
      root.reset();
      return;
    }

    root = build_rec(points, 0, static_cast<int>(points.size()) - 1, 0);
  }

  std::vector<Point> radius_search(const Point &center, double radius) const {
    if (radius < 0.0 || !root) {
      return {};
    }

    std::vector<Point> result;
    range_search_rec(root.get(), center, radius * radius, result);
    return result;
  }

  std::unordered_map<int, Point> points_by_id;
  std::unique_ptr<KdNode> root;
};

KdTreeSpatialIndex::KdTreeSpatialIndex() : impl_(std::make_unique<Impl>()) {}

KdTreeSpatialIndex::~KdTreeSpatialIndex() = default;

bool KdTreeSpatialIndex::upsert(const Point &point) {
  if (point.id < 0) {
    std::cerr << "[KdTreeSpatialIndex::upsert] rejected invalid taxi_id="
              << point.id << '\n';
    return false;
  }

  if (impl_->contains(point.id) && !impl_->erase_existing(point.id)) {
    std::cerr << "[KdTreeSpatialIndex::upsert] failed to erase old point for "
                 "taxi_id="
              << point.id << '\n';
    return false;
  }

  if (!impl_->insert_new(point)) {
    std::cerr << "[KdTreeSpatialIndex::upsert] failed to insert taxi_id="
              << point.id << ", rebuilding index\n";
    impl_->points_by_id[point.id] = point;
    impl_->rebuild_from_active_points();
    return impl_->contains(point.id);
  }

  return true;
}

bool KdTreeSpatialIndex::erase(int id) {
  if (id < 0) {
    std::cerr << "[KdTreeSpatialIndex::erase] rejected invalid taxi_id=" << id
              << '\n';
    return false;
  }

  if (!impl_->erase_existing(id)) {
    std::cerr << "[KdTreeSpatialIndex::erase] taxi_id=" << id
              << " not found in index\n";
    return false;
  }

  return true;
}

std::vector<Point> KdTreeSpatialIndex::radius_search(const Point &center,
                                                     double radius) const {
  if (radius < 0.0) {
    std::cerr << "[KdTreeSpatialIndex::radius_search] rejected negative radius="
              << radius << '\n';
    return {};
  }
  return impl_->radius_search(center, radius);
}

void KdTreeSpatialIndex::rebuild(const std::vector<Point> &points) {
  impl_->points_by_id.clear();
  for (const auto &point : points) {
    if (point.id < 0) {
      std::cerr << "[KdTreeSpatialIndex::rebuild] skipped invalid taxi_id="
                << point.id << '\n';
      continue;
    }

    if (impl_->points_by_id.count(point.id)) {
      std::clog << "[KdTreeSpatialIndex::rebuild] replacing duplicate taxi_id="
                << point.id << '\n';
    }
    impl_->points_by_id[point.id] = point;
  }

  impl_->rebuild_from_active_points();
  std::clog << "[KdTreeSpatialIndex::rebuild] rebuilt active_size="
            << impl_->points_by_id.size() << '\n';
}

std::size_t KdTreeSpatialIndex::size() const {
  return impl_->points_by_id.size();
}

void KdTreeSpatialIndex::clear() {
  impl_->points_by_id.clear();
  impl_->root.reset();
}
