3#ifndef KD_TREE_H
#define KD_TREE_H

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <vector>

// ============================================================
// 数据结构定义
// ============================================================

struct Point {
    double coord[2]; // [0]=经度(longitude), [1]=纬度(latitude)
    int id;          // 唯一标识

    Point() : coord{0, 0}, id(-1) {}
    Point(double lon, double lat, int id) : coord{lon, lat}, id(id) {}
};

struct KdNode {
    Point point;
    KdNode* left;
    KdNode* right;
    int size;     // 子树中活节点数（替罪羊核心）
    int total;    // 子树总节点数（含已删除）
    bool deleted; // 惰性删除标记

    KdNode()
        : left(nullptr), right(nullptr), size(0), total(0), deleted(false) {}
    KdNode(const Point& p)
        : point(p), left(nullptr), right(nullptr), size(1), total(1),
          deleted(false) {}
};

// ============================================================
// Haversine 距离（km）
// ============================================================

inline double haversine(const Point& a, const Point& b) {
    constexpr double R = 6371.0; // 地球半径 km
    double dLat = (b.coord[1] - a.coord[1]) * M_PI / 180.0;
    double dLon = (b.coord[0] - a.coord[0]) * M_PI / 180.0;
    double lat1 = a.coord[1] * M_PI / 180.0;
    double lat2 = b.coord[1] * M_PI / 180.0;

    double h = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(lat1) * std::cos(lat2) * std::sin(dLon / 2) *
                   std::sin(dLon / 2);
    return 2.0 * R * std::asin(std::sqrt(h));
}

// 坐标轴上的分量距离（用于 KNN 剪枝）
inline double axis_dist_km(const Point& a, const Point& b, int axis) {
    Point pa = a, pb = a;
    pb.coord[axis] = b.coord[axis];
    return haversine(pa, pb);
}

// ============================================================
// KNN 结果：最大堆（距离, Point）
// ============================================================

struct KnnEntry {
    double dist;
    Point point;
    bool operator<(const KnnEntry& o) const { return dist < o.dist; }
};
using KnnHeap = std::priority_queue<KnnEntry>;

// ============================================================
// 替罪羊 K-d Tree
// ============================================================

class Kd_Tree {
  private:
    KdNode* root_;
    static constexpr double ALPHA = 0.75;

    // ---------- 辅助函数 ----------

    static int get_size(KdNode* n) { return n ? n->size : 0; }
    static int get_total(KdNode* n) { return n ? n->total : 0; }

    static void update(KdNode* n) {
        if (!n)
            return;
        n->size =
            (!n->deleted ? 1 : 0) + get_size(n->left) + get_size(n->right);
        n->total = 1 + get_total(n->left) + get_total(n->right);
    }

    static bool is_unbalanced(KdNode* n) {
        if (!n)
            return false;
        int lt = get_total(n->left), rt = get_total(n->right);
        int t = n->total;
        // 子树大小超过 α × 总大小 → 不平衡
        return (lt > ALPHA * t + 1) || (rt > ALPHA * t + 1);
    }

    // 检查惰性删除是否过多，需要重建
    static bool too_many_deleted(KdNode* n) {
        if (!n)
            return false;
        return n->size < (1.0 - ALPHA) * n->total;
    }

    // ---------- 拍扁：收集活节点 ----------

    void flatten(KdNode* node, std::vector<Point>& out) {
        if (!node)
            return;
        flatten(node->left, out);
        if (!node->deleted) {
            out.push_back(node->point);
        }
        flatten(node->right, out);
    }

    // 释放子树内存
    void destroy(KdNode* node) {
        if (!node)
            return;
        destroy(node->left);
        destroy(node->right);
        delete node;
    }

    // ---------- 构建平衡 K-d 子树 ----------

    KdNode* build_recursive(std::vector<Point>& pts, int l, int r, int depth) {
        if (l > r)
            return nullptr;

        int axis = depth % 2;
        int mid = (l + r) / 2;

        // 按当前轴的中位数划分
        std::nth_element(pts.begin() + l, pts.begin() + mid, pts.begin() + r + 1,
                         [axis](const Point& a, const Point& b) {
                             return a.coord[axis] < b.coord[axis];
                         });

        KdNode* node = new KdNode(pts[mid]);
        node->left = build_recursive(pts, l, mid - 1, depth + 1);
        node->right = build_recursive(pts, mid + 1, r, depth + 1);
        update(node);
        return node;
    }

    // ---------- 重建子树（替罪羊核心操作）----------

    KdNode* rebuild(KdNode* node, int depth) {
        std::vector<Point> pts;
        flatten(node, pts);
        destroy(node);
        if (pts.empty())
            return nullptr;
        return build_recursive(pts, 0, (int)pts.size() - 1, depth);
    }

    // ---------- 插入 ----------

    // 返回新根，scapegoat 指向需要重建的节点的父指针
    KdNode* insert_recursive(KdNode* node, const Point& p, int depth,
                             KdNode**& scapegoat, int& sg_depth) {
        if (!node) {
            return new KdNode(p);
        }

        int axis = depth % 2;
        if (p.coord[axis] <= node->point.coord[axis]) {
            node->left = insert_recursive(node->left, p, depth + 1, scapegoat,
                                          sg_depth);
        } else {
            node->right = insert_recursive(node->right, p, depth + 1,
                                           scapegoat, sg_depth);
        }
        update(node);

        // 回溯时检查是否不平衡，记录最高的不平衡节点
        if (is_unbalanced(node)) {
            // scapegoat 将在外层处理
            sg_depth = depth;
        }
        return node;
    }

    // ---------- 删除（惰性）----------

    bool remove_recursive(KdNode* node, int point_id) {
        if (!node)
            return false;
        if (!node->deleted && node->point.id == point_id) {
            node->deleted = true;
            node->size--;
            return true;
        }
        // 需要遍历两个子树（因为删除按 ID 不按坐标）
        if (remove_recursive(node->left, point_id)) {
            update(node);
            return true;
        }
        if (remove_recursive(node->right, point_id)) {
            update(node);
            return true;
        }
        return false;
    }

    void knn_recursive(KdNode* node, const Point& target, int k, int depth,
                       KnnHeap& heap) {
        if (!node)
            return;

        // 计算当前点到目标的距离
        if (!node->deleted) {
            double d = haversine(node->point, target);
            if ((int)heap.size() < k) {
                heap.push({d, node->point});
            } else if (d < heap.top().dist) {
                heap.pop();
                heap.push({d, node->point});
            }
        }

        int axis = depth % 2;
        double diff = target.coord[axis] - node->point.coord[axis];

        // 先搜索更近的子树
        KdNode* near_child = (diff <= 0) ? node->left : node->right;
        KdNode* far_child = (diff <= 0) ? node->right : node->left;

        knn_recursive(near_child, target, k, depth + 1, heap);

        // 剪枝：判断是否需要搜索另一侧
        double axis_d = axis_dist_km(target, node->point, axis);
        if ((int)heap.size() < k || axis_d < heap.top().dist) {
            knn_recursive(far_child, target, k, depth + 1, heap);
        }
    }

    // ---------- 范围查询 ----------

    void range_recursive(KdNode* node, const Point& center, double radius_km,
                         int depth, std::vector<Point>& result) {
        if (!node)
            return;

        if (!node->deleted) {
            double d = haversine(node->point, center);
            if (d <= radius_km) {
                result.push_back(node->point);
            }
        }

        int axis = depth % 2;
        double axis_d = axis_dist_km(center, node->point, axis);

        // 两侧都可能有符合条件的点
        if (center.coord[axis] - node->point.coord[axis] <= 0) {
            // target 在左侧
            range_recursive(node->left, center, radius_km, depth + 1, result);
            if (axis_d <= radius_km) {
                range_recursive(node->right, center, radius_km, depth + 1,
                                result);
            }
        } else {
            range_recursive(node->right, center, radius_km, depth + 1, result);
            if (axis_d <= radius_km) {
                range_recursive(node->left, center, radius_km, depth + 1,
                                result);
            }
        }
    }

  public:
    Kd_Tree() : root_(nullptr) {}

    ~Kd_Tree() { destroy(root_); }

    // 批量构建 O(n log n)
    void build(std::vector<Point>& points) {
        destroy(root_);
        if (points.empty()) {
            root_ = nullptr;
            return;
        }
        root_ = build_recursive(points, 0, (int)points.size() - 1, 0);
    }

    // 插入 + 替罪羊自动重构
    void insert(const Point& p) {
        KdNode** scapegoat = nullptr;
        int sg_depth = -1;
        root_ = insert_recursive(root_, p, 0, scapegoat, sg_depth);

        // 如果发现不平衡，从根开始找到该深度的节点并重建
        if (sg_depth >= 0) {
            // 简化处理：直接重建整棵树
            // 对于课设规模的数据，这已经足够高效
            root_ = rebuild(root_, 0);
        }
    }

    // 惰性删除 + 阈值触发重建
    void remove(int point_id) {
        if (remove_recursive(root_, point_id)) {
            // 检查是否删除节点过多，触发全局重建
            if (too_many_deleted(root_)) {
                root_ = rebuild(root_, 0);
            }
        }
    }

    // KNN 查询：返回距离 target 最近的 k 个点
    std::vector<Point> knn(const Point& target, int k) {
        KnnHeap heap;
        knn_recursive(root_, target, k, 0, heap);

        std::vector<Point> result;
        result.reserve(heap.size());
        while (!heap.empty()) {
            result.push_back(heap.top().point);
            heap.pop();
        }
        // 从近到远排序
        std::reverse(result.begin(), result.end());
        return result;
    }

    // 范围查询：返回 center 半径 radius_km 内的所有点
    std::vector<Point> range_search(const Point& center, double radius_km) {
        std::vector<Point> result;
        range_recursive(root_, center, radius_km, 0, result);
        return result;
    }

    // 查询树中活节点数
    int size() const { return get_size(root_); }

    // 判断树是否为空
    bool empty() const { return root_ == nullptr || root_->size == 0; }
};

#endif // KD_TREE_H