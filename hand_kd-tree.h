#ifndef HAND_KD_TREE_H
#define HAND_KD_TREE_H

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <vector>



class Point {
public:
    double coords[2];
    int id;

    Point() : coords{0.0, 0.0}, id(-1) {}
    Point(double lon, double lat, int i) : coords{lon, lat}, id(i) {}
};

class KdNode {
public:
    Point point;
    std::unique_ptr<KdNode> Left;
    std::unique_ptr<KdNode> Right;
    int size;
    int total;
    bool is_deleted;
    
    KdNode() : Left(nullptr), Right(nullptr), size(0), total(0), is_deleted(false) {}
    KdNode(const Point& a) : point(a), Left(nullptr), Right(nullptr), size(1), total(1), is_deleted(false) {}
};

class Dist_Calculateor {
public:
    static const double factor = 0.754;
  
    static inline double dist_sq(const Point& a, const Point& b) {
        double dx = (a.coords[0] - b.coords[0]);
        double dy = (a.coords[1] - b.coords[1]);
        return dx * dx + dy * dy;
    }
  
    static inline double manhattan_dist(const Point& a, const Point& b) {
        return std::abs(a.coords[0] - b.coords[0]) + std::abs(a.coords[1] - b.coords[1]);
    }
};

class KnnEntry {
    double dist;
    Point point;
    bool operator<(const KnnEntry& a) const {return dist < a.dist;}
};
using KnnPQ = std::priority_queue<KnnEntry, std::vector<KnnEntry>, std::less<KnnEntry>>;


class Kd_Tree {
private:
    static constexpr double ALPHA = 0.75;
public: 
    KdNode* build(std::vector<Point>& pts, int l, int r, int depth) {
        if (l > r) return nullptr;
        int d = !(depth & 1);
        int mid = (l + r) >> 1;
        std::nth_element(pts.begin() + l, pts.begin() + mid, pts.begin() + r + 1,
        [d](const Point& a, const Point& b) {
            return a.coords[d] < b.coords[d];
        }
        );
        auto node = std::make_unique<KdNode>(pts[mid]);
        node->Left = build(pts, l, mid - 1, depth + 1);
        node->Right = build(pts, mid + 1, r, depth + 1);
        return node;
    }
    
    void flatten(std::unique_ptr<KdNode> node, std::vector<Point>& pts) {
        if(!node) return;
        if(!node->is_deleted) pts.push_back(node->point);
        
        flatten(std::move(node->Left), pts);
        flatten(std::move(node->Right), pts);
    }

    std::unique_ptr<KdNode> rebuild(std::unique_ptr<KdNode> node, int depth) {
        std::vector<Point> pts;
        flatten(std::move(node), pts);
        return build(pts, 0, (int)pts.size() - 1, depth);
    }

    void knn_search(std::unique_ptr<kdNode> node, const Point& target, int k, int depth, KnnHeap& heap) {
        if(!node) return;
        if(!node->deleted) {
            double d = Dist_Calculateor::dist_sq(node->point, target);
            if(heap.size() < k) heap.push({d, node->point});
            else if(d < heap.top().dist) {
                heap.pop();
                heap.push({d, node->point});
            }
        }
        
        int d = !(depth & 1); 
        double diff = target.coords[d] - node->point.coords[d];
        
        std::unique_ptr near_child = (diff <= 0) ? node->Left : node->Right;
        std::unique_ptr far_child = (diff <= 0) ? node->Right : node->Left;
        
        
    } 
};
