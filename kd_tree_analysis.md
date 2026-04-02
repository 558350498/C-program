# kd_tree 代码分析与 debug 记录

## 1. 当前 `kd_tree` 已经具备的能力

`Kd_Tree` 现在已经有这些核心能力：

- `insert(const Point&)`：插入点
- `remove(int id)`：按 `id` 懒删除
- `knn(const Point&, int)`：K 近邻查询
- `range_search(const Point&, double)`：圆形范围查询
- 子树失衡时重建、删除过多时重建

从结构上看，这个文件本身已经不是“空壳”，主体框架是完整的，主要问题集中在少数接口没补完，以及重建后的路径一致性。

## 2. 本次确认到的问题

### 已修复的问题

1. `Kd_Tree::build(...)` 之前是空实现

- 位置：`kd_tree.h` 第 263-265 行
- 影响：`rebuild()` 内部会调用 `build()`，原来这里没有返回值，属于未定义行为
- 现在处理：已经改成调用 `build_rec(...)`

2. 重建后，删除路径可能和建树路径不一致

- 位置：`kd_tree.h` 第 126-135、164-166、195-196、213-216 行
- 原因：原先 `build_rec()` 只按当前维度比较；但 `insert/remove` 也是只按当前维度判断左右。只要某一维坐标相等，`nth_element` 可能把点放到任意一边，删除时就有可能沿错误分支走，导致“逻辑上删除成功，树里实际还留着点”
- 现在处理：新增 `less_on_dim(...)`，按“当前维度 -> 另一维 -> id”统一比较，保证建树、插入、删除使用同一套规则

3. 懒删除后没有真正用上 `needs_rebuild(...)`

- 位置：`kd_tree.h` 第 154-156、220-221 行
- 影响：删除很多节点后，树虽然结果还能查，但查询性能会越来越差
- 现在处理：删除递归改成操作 `std::unique_ptr<KdNode>&`，删除后会按删除比例触发子树重建

4. 查询边界参数没有保护

- 位置：`kd_tree.h` 第 283-300 行
- 原因：`knn(..., 0)` 原来会在空堆上取 `top()`，有崩溃风险；空树和负半径查询也没有保护
- 现在处理：补了提前返回

### 仍然存在但不影响正确性的点

1. `range_search` 的私有递归函数里，`range` 参数目前没有实际参与剪枝

- 位置：`kd_tree.h` 第 252-259 行
- 现状：当前范围查询本质上是“圆查找”，真正用来剪枝的是 `node->box.min_dist_sq(center)`
- 结论：这不算 bug，但参数设计有点冗余

## 3. 还需要实现/补清楚的接口

### 明确还没实现的接口

1. `taxi_system::assign_taxi(int customer_id)`

- 位置：`taxi_system.h` 第 56-59 行
- 现状：现在只是 `TODO`
- 说明：这是目前最明确的“还没做完”的业务接口

### 建议补充的接口

1. 批量建树接口

- 当前 `Kd_Tree::build(std::vector<Point>&, int, int, int)` 更像内部 helper，不像对外 API
- 因为它只返回一个 `unique_ptr<KdNode>`，并不会自动更新 `root_` 和 `id_to_point`
- 如果你后面需要“用一批点直接初始化整棵树”，建议再补一个更清晰的接口，例如：

```cpp
void build_from_points(const std::vector<Point>& points);
```

这样可以一次性维护：

- `root_`
- `id_to_point`
- 初始化后的树结构

2. 如果出租车位置会动态变化，建议补位置更新接口

- 例如：

```cpp
bool update_taxi_position(int id, double x, double y);
```

- 现在系统里只有 `add_taxi(...)`，没有“挪动车辆位置”的接口
- 对 `kd_tree` 来说可以通过“`remove + insert`”实现，但业务层最好封装成单独接口

## 4. 我这次做的验证

我新增了一个调试文件：`kd_tree_debug_test.cpp`

覆盖了这些场景：

- 空树查询
- `k = 0` 的 KNN 查询
- 负半径范围查询
- 插入一组共线点，触发重建
- 重建后逐个删除所有点
- 删除后再次查询，确认树中没有残留可见点

本地验证命令：

```bash
g++ -std=c++17 -Wall -Wextra -pedantic main.cpp -o kd_test.exe
g++ -std=c++17 -Wall -Wextra -pedantic kd_tree_debug_test.cpp -o kd_tree_debug_test.exe
./kd_tree_debug_test.exe
```

测试结果已经通过：`kd_tree debug test passed`

## 5. 结论

如果只看 `kd_tree` 这部分，当前最关键的问题已经修掉了，核心数据结构已经能正常工作。

后面你最需要继续实现的是：

1. `assign_taxi(...)`
2. 如果有批量初始化需求，补一个真正对外的批量建树接口
3. 如果出租车会移动，补位置更新接口
