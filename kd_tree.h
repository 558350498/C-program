#ifndef KD_TREE
#define KD_TREE

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <queue>
#include <vector>
#include <unordered_map>

class Point {
public:
  std::array<double, 2> coords;
  int id;

  Point() : coords{0.0, 0.0}, id(-1) {}
  Point(double lon, double lat, int i) : coords{lon, lat}, id(i) {}
};

class BoundingBox {
public:
  std::array<double, 2> min_coords;
  std::array<double, 2> max_coords;

  BoundingBox() {
    min_coords.fill(0x3f3f3f3f);
    max_coords.fill(-0x3f3f3f3f);
  }

  explicit BoundingBox(const Point &p) {
    min_coords = p.coords;
    max_coords = p.coords;
  }

  void extend(const Point &p) {
    for (int i = 0; i < 2; i++) {
      min_coords[i] = std::min(min_coords[i], p.coords[i]);
      max_coords[i] = std::max(max_coords[i], p.coords[i]);
    }
  }

  void extend(const BoundingBox &other) {
    for (int i = 0; i < 2; i++) {
      min_coords[i] = std::min(min_coords[i], other.min_coords[i]);
      max_coords[i] = std::max(max_coords[i], other.max_coords[i]);
    }
  }

  double min_dist_sq(const Point &target) const {
    double dist = 0;
    for (int i = 0; i < 2; i++) {
      double clamped =
          std::max(min_coords[i], std::min(target.coords[i], max_coords[i]));
      double d = target.coords[i] - clamped;
      dist += d * d;
    }
    return dist;
  }
};

class KdNode {
public:
  Point point;
  std::unique_ptr<KdNode> Left;
  std::unique_ptr<KdNode> Right;
  BoundingBox box;
  int size;
  int total;
  bool is_deleted;

  KdNode()
      : Left(nullptr), Right(nullptr), size(0), total(0), is_deleted(false) {}
  KdNode(const Point &a)
      : point(a), Left(nullptr), Right(nullptr), box(a), size(1), total(1),
        is_deleted(false) {}

  void update_all_info() {
    size = is_deleted ? 0 : 1;
    total = 1;
    box = BoundingBox(point);

    if (Left) {
      size += Left->size;
      total += Left->total;
      box.extend(Left->box);
    }
    if (Right) {
      size += Right->size;
      total += Right->total;
      box.extend(Right->box);
    }
  }
};

class Dist_Calculateor {
public:
  static inline double dist_sq(const Point &a, const Point &b) {
    double dx = (a.coords[0] - b.coords[0]);
    double dy = (a.coords[1] - b.coords[1]);
    return dx * dx + dy * dy;
  }

  static inline double manhattan_dist(const Point &a, const Point &b) {
    return std::abs(a.coords[0] - b.coords[0]) +
           std::abs(a.coords[1] - b.coords[1]);
  }
};

class KnnEntry {
public:
  double dist;
  Point point;
  bool operator<(const KnnEntry &a) const { return dist < a.dist; }
};
using KnnPQ = std::priority_queue<KnnEntry, std::vector<KnnEntry>, std::less<KnnEntry>>;


class Kd_Tree {
private:
  static constexpr double ALPHA = 0.75;
  std::unordered_map<int, Point> id_to_point;
  std::unique_ptr<KdNode> root_;

  static int get_total(const std::unique_ptr<KdNode> &p) {
    return p ? p->total : 0;
  }
  static int get_size(const std::unique_ptr<KdNode> &p) {
    return p ? p->size : 0;
  }

  static bool is_unbalanced(const KdNode *node) {
    if (!node)
      return false;
    int L_total = get_total(node->Left);
    int R_total = get_total(node->Right);

    return (L_total > ALPHA * node->total + 1 ||
            R_total > ALPHA * node->total + 1);
  }

  static bool needs_rebuild(const KdNode *node) {
    if (!node) return false;
    return node->size < (1.0 - ALPHA) * node->total;
  }

  std::unique_ptr<KdNode> build_rec(std::vector<Point> &pts, int l, int r, int depth) {
    if (l > r)
      return nullptr;
    int d = !(depth & 1);
    int mid = (l + r) >> 1;
    std::nth_element(pts.begin() + l, pts.begin() + mid, pts.begin() + r + 1,
                     [d](const Point &a, const Point &b) {
                       return a.coords[d] < b.coords[d];
                     });
    auto node = std::make_unique<KdNode>(pts[mid]);
    node->Left = build_rec(pts, l, mid - 1, depth + 1);
    node->Right = build_rec(pts, mid + 1, r, depth + 1);
    node->update_all_info();
    return node;
  }

  std::unique_ptr<KdNode> rebuild(std::unique_ptr<KdNode> node, int depth) {
    std::vector<Point> pts;
    flatten(node.get(), pts);
    return build(pts, 0, (int)pts.size() - 1, depth);
  }
                             
  void flatten(const KdNode *node, std::vector<Point> &pts) {
    if (!node) return;
    if (!node->is_deleted)  pts.push_back(node->point);
                    
    flatten(node->Left.get(), pts);
    flatten(node->Right.get(), pts);
  }

  void insert_rec(std::unique_ptr<KdNode> &node, const Point &pt, int depth) {
    if (!node) {
      node = std::make_unique<KdNode>(pt);
      return;
    }
    int d = !(depth & 1);
    if (pt.coords[d] < node->point.coords[d])
      insert_rec(node->Left, pt, depth + 1);
    else
      insert_rec(node->Right, pt, depth + 1);

    node->update_all_info();

    if (is_unbalanced(node.get()))
      node = rebuild(std::move(node), depth);
  }

  void remove_rec(KdNode *node, const Point &pt, int depth) {
    if (!node)
      return;
    if (node->point.id == pt.id) {
      node->is_deleted = true;
    } else {
      int d = !(depth & 1);
      if (pt.coords[d] < node->point.coords[d])
        remove_rec(node->Left.get(), pt, depth + 1);
      else
        remove_rec(node->Right.get(), pt, depth + 1);
    }
    node->update_all_info();
  }

  void knn_search(const KdNode *node, const Point &target, int k, int depth, KnnPQ &heap) const {
    if (!node) return;
    if (heap.size() == static_cast<std::size_t>(k) && 
    node->box.min_dist_sq(target) >= heap.top().dist) return;
    
    if (!node->is_deleted) {
      double d = Dist_Calculateor::dist_sq(node->point, target);
      if (heap.size() < static_cast<std::size_t>(k))
        heap.push({d, node->point});
      else if (d < heap.top().dist) {
        heap.pop();
        heap.push({d, node->point});
      }
    }

    int d = !(depth & 1);
    double diff = target.coords[d] - node->point.coords[d];
    KdNode *near_child = (diff <= 0) ? node->Left.get() : node->Right.get();
    KdNode *far_child = (diff <= 0) ? node->Right.get() : node->Left.get();
    if (near_child && (heap.size() < static_cast<std::size_t>(k) || 
    near_child->box.min_dist_sq(target) < heap.top().dist))
        knn_search(near_child, target, k, depth + 1, heap);
    if (far_child && (heap.size() < static_cast<std::size_t>(k) ||
    far_child->box.min_dist_sq(target) < heap.top().dist))
        knn_search(far_child, target, k, depth + 1, heap);
    return;
  }
              
  void range_search(const KdNode *node, const Point& center, const BoundingBox &range,
  double radius_sq, std::vector<Point> &result) const {
    if (!node) return;
    if (node->box.min_dist_sq(center) > radius_sq) return;
    if (!node->is_deleted && Dist_Calculateor::dist_sq(node->point, center) <= radius_sq)
      result.push_back(node->point);
    range_search(node->Left.get(), center, range, radius_sq, result);
    range_search(node->Right.get(), center, range, radius_sq, result);
  }

public:
  std::unique_ptr<KdNode> build(std::vector<Point> &pts, int l, int r, int depth) {
    
  }

  bool insert(const Point &pt) {
    if (id_to_point.count(pt.id)) return false;
    id_to_point[pt.id] = pt;
    insert_rec(root_, pt, 0);
    return true;
  }

  bool remove(int id) {
    auto it = id_to_point.find(id);
    if (it == id_to_point.end())
      return false;
    remove_rec(root_.get(), it->second, 0);
    id_to_point.erase(it);
    return true;
  }
  
  std::vector<Point> knn(const Point& target, int k) const {
    KnnPQ heap;
    knn_search(root_.get(), target, k, 0, heap);
    std::vector<Point> result;
    while (!heap.empty()) {
      result.push_back(heap.top().point);
      heap.pop();
    }
    std::reverse(result.begin(), result.end());
    return result;
  }

  std::vector<Point> range_search(const Point& center, double radius) const {
    BoundingBox range;
    for (int i = 0; i < 2; i++) {
      range.min_coords[i] = center.coords[i] - radius;
      range.max_coords[i] = center.coords[i] + radius;
    }
    std::vector<Point> result;
    range_search(root_.get(), center, range, radius * radius, result);
    return result;
  }

};

#endif // KD_TREE
