#include "kd_tree.h"

#include <cassert>
#include <iostream>

int main() {
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

  std::cout << "kd_tree debug test passed\n";
  return 0;
}
