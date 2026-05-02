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

- `set_logging_enabled`
- `logging_enabled`
- `create_taxi`
- `register_taxi`
- `set_taxi_online`
- `set_taxi_offline`
- `update_taxi_position`
- `update_taxi_status`
- `apply_assignment(IRequestContext&, const Assignment&)`
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
- 批量匹配结果不能在策略层直接改状态，应该统一交给 `apply_assignment` 回写。
- replay / CSV 批量场景可以关闭 `TaxiSystem` 日志，避免状态日志污染指标输出。

---

### Batch dispatch / Candidate edges

负责什么：

1. 定义离线批量匹配使用的标准化快照数据。
2. 表示一批可用司机、一批待处理请求和候选匹配边。
3. 根据半径、top-k、tile 粗筛条件生成候选边。
4. 提供当前阶段的批量贪心 baseline。
5. 给 MCMF 等批量策略提供统一输入。

不负责什么：

1. 不直接修改 `TaxiSystem` 内部 taxi 状态。
2. 不直接修改 request 生命周期。
3. 不实现完整 MCMF。
4. 不负责 CSV 原始字段解析和真实路网 ETA。

核心类型：

- `PassengerRequest`
- `TripRecord`
- `DriverSnapshot`
- `Assignment`
- `CandidateEdge`
- `CandidateEdgeStats`
- `CandidateEdgeGenerationResult`
- `CandidateEdgeOptions`
- `BatchDispatchInput`

核心入口：

- `is_available_for_batch`
- `is_request_ready_for_batch`
- `estimate_pickup_cost`
- `normalize_candidate_edges`
- `generate_candidate_edges_with_stats`
- `generate_candidate_edges`
- `greedy_batch_assign`

代码位置：

- `include/dispatch_batch.h`
- `tests/dispatch_batch_test.cpp`

当前状态流：

1. batch 模块只接收快照数据。
2. `generate_candidate_edges_with_stats` 生成 `taxi_id -> request_id` 候选边、`pickup_cost` 和候选边统计。
3. `normalize_candidate_edges` 过滤非法候选边，并对同一 `taxi_id/request_id` 保留最低 cost。
4. `generate_candidate_edges` 保持原便捷入口，只返回候选边列表。
5. `greedy_batch_assign` 产出 `Assignment` 列表。
6. 状态回写由 `TaxiSystem::apply_assignment` 逐条提交。

---

### Timeline replay model

负责什么：

1. 约定离线仿真的时间推进方式。
2. 说明 batch 多久跑一次。
3. 定义 request arrival、batch dispatch、pickup arrival、trip complete 的处理顺序。
4. 驱动第一版事件回放器完成虚空行走流程测试。

核心约定：

- 第一版 `batch_interval_seconds = 30`。
- `pickup_cost` 暂时按秒理解。
- 第一版 `trip_duration_seconds = 600`。
- 第一版采用“虚空行走”：不模拟真实道路，订单完成时 taxi 直接出现在终点。
- 未匹配 request 留到下一轮继续参与匹配。
- replay 会记录总指标和每轮 batch 日志，用于展示和后续 CSV 数据质量排查。

文档位置：

- `docs/timeline_model.md`

代码位置：

- `include/dispatch_replay.h`
- `src/dispatch_replay.cpp`
- `tests/dispatch_replay_test.cpp`

核心入口：

- `DispatchReplaySimulator::run`
- `DispatchReplaySimulator::run_report`
- `format_dispatch_replay_report`
- `assignment_rate`
- `completion_rate`
- `average_applied_pickup_cost`

---

### Go CSV preprocessing

负责什么：

1. 读取 Kaggle 原始 taxi trip CSV。
2. 清洗时间、坐标、缺失值和异常行。
3. 做抽样和简单 tile 映射。
4. 输出 C++ replay 可直接读取的标准化文件。

不负责什么：

1. 不直接调用 C++ 对象。
2. 不修改 `TaxiSystem` 状态。
3. 不实现 MCMF。

建议输出：

- `requests.csv`
- `drivers.csv`

边界原则：

- Go 负责数据预处理。
- C++ 负责调度核心、状态机、MCMF 和事件回放。
- 两边第一版通过文件交互。

### Replay CSV IO

负责什么：

1. 读取标准化 `requests.csv`。
2. 读取标准化 `drivers.csv`。
3. 把文件数据转换成 `PassengerRequest` 和 `DriverSnapshot`。
4. 记录行级解析错误，跳过坏行，保留可用行。

不负责什么：

1. 不解析 Kaggle 原始字段。
2. 不做坐标清洗和 tile 映射。
3. 不运行调度仿真。
4. 不修改 `TaxiSystem` 状态。

核心入口：

- `load_passenger_requests_csv`
- `load_driver_snapshots_csv`

代码位置：

- `include/dispatch_replay_io.h`
- `src/dispatch_replay_io.cpp`
- `tests/dispatch_replay_io_test.cpp`

---

### McmfBatchStrategy

负责什么：

1. 在候选边集合上做最大匹配。
2. 在最大匹配数量相同的解里最小化总代价。
3. 作为后续等待时间、未服务惩罚等成本模型的扩展点。

不负责什么：

1. 核心算法不拥有候选边生成逻辑；batch 便捷入口只复用统一候选边生成器。
2. 不直接读取 `TaxiSystem` 内部状态。
3. 不直接修改 request/taxi 状态。
4. 当前第一版不建模未服务惩罚。

核心入口：

- `assign(const std::vector<CandidateEdge>&)`
- `assign(const BatchDispatchInput&, const CandidateEdgeOptions&)`

代码位置：

- `include/mcmf_batch_strategy.h`
- `src/mcmf_batch_strategy.cpp`
- `tests/mcmf_batch_strategy_test.cpp`

当前算法含义：

1. 源点连接 taxi，容量为 1。
2. taxi 通过 `CandidateEdge` 连接 request，容量为 1，费用为 `pickup_cost`。
3. request 连接汇点，容量为 1。
4. 使用最小费用最大流求解，先保证匹配数量最大，再最小化总代价。
5. 进入 MCMF 前会先规范化候选边，避免非法边和重复边污染流图。

当前验证入口：

- `mcmf_batch_strategy_test`
- `smoke`

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

1. 先判断问题属于哪一类：业务编排、batch 输入、request 生命周期、空间搜索、派单决策、基础领域对象。
2. 进入对应模块看核心入口。
3. 状态变化优先看 `TaxiSystem` 和 `RequestContext`。
4. 批量匹配输入和候选边优先看 `dispatch_batch.h`。
5. 离线回放指标和 batch 日志优先看 `dispatch_replay.h`。
6. 算法策略优先看 `IDispatchStrategy` / `McmfBatchStrategy`。
7. 空间查询优先看 `ISpatialIndex`。

## 当前设计原则

1. 不过度设计。
2. 频繁变化的地方保留虚接口。
3. 稳定领域对象保持薄接口。
4. 先补业务生命周期闭环，再扩 CSV、tile 和 MCMF。
