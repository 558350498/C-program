# 调度时间线模型

这份文档定义当前作业阶段的离线仿真时间线。目标不是模拟真实网约车平台的全部细节，而是让 request、taxi、batch matching、MCMF 和状态回写有一个清晰、可验证的推进顺序。

## 1. 当前推荐参数

第一版建议使用固定窗口批量匹配：

- `batch_interval_seconds = 30`
- `pickup_cost` 单位为秒
- `trip_duration_seconds = 600`
- 未匹配 request 暂不取消，留到下一轮继续匹配
- 第一版采用“虚空行走”模型，不模拟真实道路

含义：

- 每 30 秒收集一次当前 pending requests 和 free drivers。
- 候选边的 `pickup_cost` 暂时表示估算接驾耗时。
- 行程服务时长第一版先用固定 600 秒，后续再从 `TripRecord` 或 CSV 数据中读取。
- taxi 完成订单时，位置直接更新到 request 的 `dropoff_location`。

## 2. 核心事件

当前最小事件集合：

- `request_arrival`
  - 新请求到达。
  - 创建 request 上下文，状态为 `pending`。
- `batch_dispatch`
  - 到达固定 batch 时间点。
  - 生成批量输入、候选边和匹配结果。
- `pickup_arrival`
  - taxi 到达乘客上车点。
  - request 从 `dispatched` 进入 `serving`。
- `trip_complete`
  - 订单完成。
  - request 进入 `completed`，taxi 回到 `free`。

后续可以增加：

- `taxi_online`
- `taxi_offline`
- `taxi_position_update`
- `request_timeout`

## 3. 单个订单生命周期

一个成功服务的 request 在时间线中的状态流：

```text
pending
  -> batch_dispatch 匹配成功
dispatched
  -> pickup_arrival
serving
  -> trip_complete
completed
```

对应系统调用：

```text
batch_dispatch:
  TaxiSystem::apply_assignment(request, assignment)

pickup_arrival:
  TaxiSystem::start_trip(request)

trip_complete:
  TaxiSystem::update_taxi_position(taxi_id, request.dropoff_location)
  TaxiSystem::complete_trip(request)
```

如果某一轮没有匹配上：

```text
pending
  -> 留在 pending 队列
  -> 下一轮 batch_dispatch 继续参与匹配
```

## 4. 一轮 batch 的流程

在时间 `t` 到达 batch 边界时：

1. 收集所有 `request_time <= t` 且状态仍为 `pending` 的请求。
2. 收集所有状态为 `free` 且 `available_time <= t` 的司机快照。
3. 构造 `BatchDispatchInput`。
4. 调用 `generate_candidate_edges` 生成候选边。
5. 同一批候选边可以分别跑：
   - `greedy_batch_assign`
   - `McmfBatchStrategy::assign`
6. 选择当前要使用的策略输出 `Assignment`。
7. 对每个 `Assignment` 调用 `TaxiSystem::apply_assignment`。
8. 对回写成功的订单生成后续事件：
   - `pickup_arrival_time = t + assignment.pickup_cost`
   - `trip_complete_time = pickup_arrival_time + trip_duration_seconds`

第一版可以先只使用 MCMF 结果作为正式回写，greedy 结果只用于指标对比。

当前实现里，回放器使用 `generate_candidate_edges_with_stats` 生成候选边和统计数据。每轮 batch 会记录：

- 可用司机数
- pending request 数
- 候选边数量
- 有候选边 / 无候选边的 request 数
- greedy 匹配数量和总 cost
- MCMF 匹配数量和总 cost
- 实际成功回写的 assignment 数量和接驾 cost

## 5. 同一时间点事件顺序

如果多个事件发生在同一秒，建议按以下顺序处理：

1. `trip_complete`
2. `request_arrival`
3. `batch_dispatch`
4. `pickup_arrival`

理由：

- 先释放已经完成订单的 taxi，让它能参与当前 batch。
- 再加入已经到达的新请求。
- 然后做本轮匹配。
- 最后处理刚好到达乘客点的上车事件，避免同一秒内刚派出的订单立刻打乱 batch 输入。

这个顺序是第一版约定，后续如有更真实的接驾建模，可以再调整。

## 6. 当前不做的复杂度

当前阶段不做：

- 多线程事件处理。
- 真实道路最短路。
- taxi 在接驾途中的连续位置更新。
- request 超时取消。
- 未服务惩罚。
- 司机收益、重定位收益和区域热度建模。

这些都可以作为后续工程化或算法增强，但不应该阻塞当前 C++ 作业版本。

## 7. 虚空行走模型

第一版回放器先不模拟真实地图和道路行驶，只使用二维坐标验证调度流程。

规则：

1. taxi 初始位置来自司机快照。
2. request 使用 `pickup_location` 和 `dropoff_location`。
3. 接驾耗时由候选边的 `pickup_cost` 表示。
4. 到达 `pickup_arrival_time` 后调用 `TaxiSystem::start_trip`。
5. 到达 `trip_complete_time` 时，先把 taxi 位置更新到 `dropoff_location`，再调用 `TaxiSystem::complete_trip`。
6. 完成后 taxi 回到 `free`，下一轮可以继续参与匹配。

这个模型的目的：

- 验证 request 生命周期闭环。
- 验证 taxi 被占用后不会重复派单。
- 验证未匹配 request 会留到下一轮。
- 验证 greedy 和 MCMF 可以在同一批候选边上比较。

它不表达真实道路路径，只表达“接驾耗时”和“订单完成后车辆出现在终点”这两个调度必要事实。

## 8. 当前实现

第一版事件驱动仿真已经接入：

- `include/dispatch_replay.h`
- `src/dispatch_replay.cpp`
- `tests/dispatch_replay_test.cpp`

当前 `DispatchReplaySimulator` 会：

1. 注册并上线初始 free drivers。
2. 按 `request_time` 生成 `request_arrival` 事件。
3. 按 `batch_interval_seconds` 生成 `batch_dispatch` 事件。
4. 在 batch 中生成候选边，同时计算 greedy 和 MCMF 结果。
5. 使用 MCMF 结果调用 `TaxiSystem::apply_assignment`。
6. 按 `pickup_cost` 生成 `pickup_arrival` 事件。
7. 按固定 `trip_duration_seconds` 生成 `trip_complete` 事件。
8. 订单完成时把 taxi 位置更新到 `dropoff_location`，再释放为 `free`。
9. 通过 `run_report` 返回 `DispatchReplayReport`，包含总指标和每轮 batch 日志。
10. 通过 `format_dispatch_replay_report` 输出可展示的回放摘要。

当前测试覆盖：

- request arrival -> batch matching -> apply assignment -> start trip -> complete trip。
- 第一单完成后，taxi 虚空移动到终点，并继续接第二单。
- 无候选边时 request 保持未服务。
- 每轮 batch 日志、候选边统计、greedy/MCMF 对比指标和平均接驾 cost。

## 9. 当前输出指标

当前回放器已经输出：

- 总请求数
- 已派单请求数
- 完成请求数
- 未服务请求数
- 派单率
- 完成率
- 总 batch 数
- 候选边总数
- 有候选边 / 无候选边的 request 累计数
- greedy 和 MCMF 的匹配数量 / 总 cost 对比
- 实际回写的接驾总 cost 和平均 cost

第一版回放器已经证明流程闭环：

```text
request arrival -> batch matching -> apply assignment -> start trip -> complete trip
```

## 10. 下一步实现建议

后续可以继续增强回放模块：

- `TaxiSystem` 已支持日志开关，replay 默认关闭系统状态日志，避免污染 summary。
- 已增加标准化 `drivers.csv` / `requests.csv` 读取入口。
- 允许从标准化输入读取真实或近似的 `trip_duration_seconds`。
- 为 CSV 接入后的数据质量检查保留 batch 明细输出。

标准化 CSV 读取代码：

- `include/dispatch_replay_io.h`
- `src/dispatch_replay_io.cpp`
- `tests/dispatch_replay_io_test.cpp`

当前读取边界：

- C++ 只读取项目内部标准化字段。
- CSV loader 不解析 Kaggle 原始字段。
- 坏行会被记录到 `errors`，好行仍然保留。
