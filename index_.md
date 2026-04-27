# 工程导航索引

这份索引不是实现细节文档，它的目标是：忘记函数名或调用关系时，能快速定位应该看哪个模块。

## 当前主分层

### TaxiSystem

负责什么：

1. 作为业务编排入口。
2. 管 taxi 注册、上下线、位置更新、状态迁移。
3. 调用空间索引和派单策略。
4. 编排 request 生命周期入口，例如派单、开始行程、完成行程、取消请求。

不负责什么：

1. 不直接实现空间搜索算法。
2. 不直接实现具体派单策略。
3. 不保存复杂历史订单数据。
4. 不负责 CSV、tile、MCMF 等离线仿真输入。

核心入口：

- `create_taxi`
- `register_taxi`
- `set_taxi_online`
- `set_taxi_offline`
- `update_taxi_position`
- `update_taxi_status`
- `dispatch_nearest(IRequestContext&, double)`
- `start_trip(IRequestContext&)`
- `complete_trip(IRequestContext&)`
- `cancel_request(IRequestContext&)`

代码位置：

- `include/taxi_system.h`
- `src/taxi_system.cpp`

当前关键约束：

- `occupy` taxi 不能直接 offline。
- `occupy` taxi 不能通过 `update_taxi_status(..., free)` 绕过 request 生命周期释放。
- 释放占用车辆应该走 `complete_trip` 或 `cancel_request`。

---

### IRequestContext / RequestContext

负责什么：

1. 管单个 request 的生命周期。
2. 记录 request 和 taxi 的绑定关系。
3. 提供 request 状态查询。
4. 给以后替换 request 存储或上下文实现留虚接口。

不负责什么：

1. 不做派单算法。
2. 不做空间索引维护。
3. 不直接修改 taxi 状态。
4. 不替代 `TaxiSystem` 做业务总编排。

核心状态：

- `pending`
- `dispatched`
- `serving`
- `completed`
- `canceled`

核心入口：

- `assign_taxi`
- `start_trip`
- `complete_request`
- `cancel_request`
- `taxi_id`
- `status`
- `start_location`
- `end_location`

代码位置：

- `include/requestcontext.h`
- `src/requestcontext.cpp`

当前状态流：

1. 新建 request 后是 `pending`。
2. `dispatch_nearest` 成功后调用 `assign_taxi`，状态变为 `dispatched`。
3. `start_trip` 成功后状态变为 `serving`。
4. `complete_trip` 成功后状态变为 `completed`，绑定 taxi 被释放。
5. `cancel_request` 成功后状态变为 `canceled`，绑定 taxi 被释放。

---

### ISpatialIndex / KdTreeSpatialIndex

负责什么：

1. 管空闲 taxi 的空间索引。
2. 提供半径搜索。
3. 提供索引重建能力。

不负责什么：

1. 不管 request 生命周期。
2. 不决定最终派给哪辆车。
3. 不承载业务状态机。

核心入口：

- `upsert`
- `erase`
- `radius_search`
- `rebuild`
- `size`
- `clear`

代码位置：

- `include/spatial_index.h`
- `include/kd_tree_spatial_index.h`
- `src/kd_tree_spatial_index.cpp`

---

### IDispatchStrategy / NearestFreeTaxiStrategy

负责什么：

1. 在给定 taxi 状态和空间索引时选择候选 taxi。
2. 作为最近车、batch matching、MCMF 等策略的扩展点。

不负责什么：

1. 不直接管理 taxi 生命周期。
2. 不记录 request 和 taxi 的绑定事实。
3. 不应该成为业务状态机本体。

核心入口：

- `select_taxi`

代码位置：

- `include/dispatch_strategy.h`
- `include/nearest_free_taxi_strategy.h`
- `src/dispatch_strategy.cpp`

当前注意点：

- 默认策略里仍有少量 stale index 清理逻辑。
- 后续做 batch matching / dry-run / MCMF 时，最好逐步收敛成更纯的决策接口。

---

### Taxi / Point / TaxiStatus

负责什么：

1. 定义系统最基础的领域对象。
2. 在业务层、索引层、策略层之间共享数据表示。

核心状态：

- `TaxiStatus::free`
- `TaxiStatus::occupy`
- `TaxiStatus::offline`

代码位置：

- `include/taxi_domain.h`
- `src/taxi_domain.cpp`

## 忘记函数时怎么找

1. 先判断问题属于哪一类：业务编排、request 生命周期、空间搜索、派单决策、基础领域对象。
2. 进入对应模块看核心入口。
3. 状态变化优先看 `TaxiSystem` 和 `RequestContext`。
4. 算法策略优先看 `IDispatchStrategy`。
5. 空间查询优先看 `ISpatialIndex`。

## 当前设计原则

1. 不过度设计。
2. 频繁变化的地方保留虚接口。
3. 稳定领域对象保持薄接口。
4. 先补业务生命周期闭环，再扩 CSV、tile 和 MCMF。
