# 出租车调度系统建模笔记

## 1. 当前项目骨架

当前项目已经完成了第一轮解耦，核心结构可以按业务编排、索引、策略和批量匹配几个模块理解：

- `TaxiSystem`
  - 负责车辆状态管理、参数校验、索引同步、调用派单策略
- `ISpatialIndex`
  - 抽象空间索引，默认实现是 `KdTreeSpatialIndex`
- `IDispatchStrategy`
  - 抽象派单策略，默认实现是 `NearestFreeTaxiStrategy`
- `dispatch_batch`
  - 定义批量匹配快照、候选边、匹配结果和当前阶段的贪心 baseline
- `McmfBatchStrategy`
  - 在同一套候选边上做最小费用最大流，作为贪心 baseline 的对照策略

当前代码结构：

- `include/`
  - 对外类型、接口、系统入口头文件
- `src/`
  - 领域类型实现、KD-Tree 空间索引实现、派单策略实现、系统实现
- `tests/`
  - 空间索引、单次派单、batch baseline、MCMF 和系统流程测试
- `plan/`
  - 后续阶段计划

当前骨架适合继续往“离线仿真 + 可替换匹配策略”方向演进，不适合再把复杂逻辑继续堆回 `TaxiSystem` 或单个头文件。

当前批量匹配边界已经明确：

- `dispatch_batch.h` 只处理快照数据和候选边，不修改系统状态。
- `greedy_batch_assign` 只输出 `Assignment`，作为 baseline 和后续 MCMF 的对照。
- `McmfBatchStrategy` 同样只输出 `Assignment`，不直接回写系统状态。
- `TaxiSystem::apply_assignment` 负责把已选中的 `Assignment` 回写到 request/taxi 状态。
- `dispatch_nearest` 也复用 `apply_assignment`，避免单次派单和批量回写出现两套状态提交逻辑。

## 2. 数据集接入思路

Kaggle 的 CSV 数据不应直接喂给调度器，应该先做一层标准化。

建议先抽象出内部统一对象：

- `PassengerRequest`
  - `request_id`
  - `request_time`
  - `pickup_lon`
  - `pickup_lat`
  - `dropoff_lon`
  - `dropoff_lat`
  - `pickup_tile`
  - `dropoff_tile`
- `TripRecord`
  - `trip_id`
  - `taxi_id`
  - `pickup_time`
  - `dropoff_time`
  - `pickup_tile`
  - `dropoff_tile`
- `DriverSnapshot`
  - `taxi_id`
  - `current_tile`
  - `status`
  - `available_time`

建议的数据流：

1. CSV 解析器负责读取原始字段
2. 预处理层负责清洗时间、坐标、缺失值
3. 地图层负责经纬度转 tile
4. 调度层只接收标准化后的请求、车辆和事件

这样以后即使换数据集，也只需要改解析和预处理，不需要改调度器。

当前建议把 CSV 解析器放在 Go 侧实现：

- Go 负责读取 Kaggle 原始 CSV。
- Go 负责字段名适配、时间解析、坐标过滤和抽样。
- Go 输出项目内部标准化 CSV。
- C++ 只读取标准化后的 `drivers.csv` / `requests.csv`，不直接依赖 Kaggle 原始字段名。

第一版标准化文件建议：

```csv
request_id,customer_id,request_time,pickup_x,pickup_y,dropoff_x,dropoff_y,pickup_tile,dropoff_tile
101,1001,10,3,4,10,0,1,2
```

```csv
taxi_id,x,y,tile,available_time
1,0,0,1,0
```

跨语言边界：

- Go 和 C++ 通过文件交互。
- 不用 cgo，不传 C++ 对象、指针或 STL 容器。
- Go 输出快照数据，C++ replay 负责调度状态机和算法。

## 3. 纽约地图与 tile 建模

项目下一阶段不建议直接接真实路网，先做 tile 离散化更稳。

当前回放器第一版更进一步简化为“虚空行走”模型：

- 只使用二维坐标 `Point(x, y)`。
- 接驾耗时用 `pickup_cost` 表示。
- 不模拟车辆沿道路连续移动。
- 订单完成时，taxi 位置直接更新到 `dropoff_location`。

这个模型只用于验证调度流程、状态机和匹配算法，不用于表达真实道路行驶。

推荐做法：

- 先用固定网格或简单 tile 编号
- 每个乘客请求落到一个 `pickup_tile`
- 每辆空闲车落到一个 `current_tile`
- 匹配时先按 tile 做候选筛选，再做距离或流网络优化

tile 的作用不是替代 KD-Tree，而是做第一层粗筛：

`tile bucket -> 候选车辆集合 -> 细粒度距离计算 / 匹配策略`

这样可以显著减少候选边数量，避免把全纽约所有车和单直接连成稠密大图。

## 4. 事件驱动仿真

下一阶段系统更自然的形态是事件驱动仿真，而不是静态查询。

核心事件：

- 新请求到达
- 车辆上线
- 车辆下线
- 车辆完成订单重新空闲
- 车辆位置更新

建议做统一时间轴，按事件时间顺序推进系统状态。

如果用 Kaggle trip 数据做离线回放，可以先把：

- `pickup_time` 视为请求到达事件
- `dropoff_time` 视为订单完成事件

第一版不强求真实历史车流，只要先让系统能基于订单流回放即可。

## 5. 二分图匹配与最小费用建模

### 5.1 基本图结构

对于某个匹配批次，构造流网络：

- 超级源点 `S`
- 超级汇点 `T`
- 空闲车节点集合 `D`
- 请求节点集合 `R`

连边方式：

- `S -> driver_i`
  - 容量 `1`
  - 费用 `0`
- `driver_i -> request_j`
  - 容量 `1`
  - 费用 `cost(i, j)`
- `request_j -> T`
  - 容量 `1`
  - 费用 `0`

如果只保留这些边，那么模型的含义是：

- 尽量在当前候选边集合内做最大匹配
- 在所有最大匹配中最小化总代价

### 5.2 不要全图连边

不建议把所有空闲车和所有请求直接连边。

正确做法：

- 每个请求只连接本 tile 及周边若干 tile 中的车辆
- 或每个请求只连接 top-k 最近车辆

这样构造出的图是稀疏二分图，计算和业务解释都更合理。

当前实现对应为：

- `CandidateEdgeOptions::radius` 控制半径筛选。
- `CandidateEdgeOptions::max_edges_per_request` 控制每个请求最多保留多少条候选边。
- `CandidateEdgeOptions::same_tile_only` 提供第一版 tile 粗筛入口。
- `generate_candidate_edges` 只为已经到达 batch 时间的请求和可用司机生成边。
- `pickup_cost` 当前由欧氏距离乘 `seconds_per_distance_unit` 估算，统一成整数费用。

这一层稳定后，MCMF 可以直接复用同一批 `CandidateEdge`，不用重新定义输入。

### 5.7 当前 MCMF 第一版

当前 MCMF 实现已经接入：

- 输入：`std::vector<CandidateEdge>`
- 输出：`std::vector<Assignment>`
- 目标：先最大化匹配数量，再在最大匹配中最小化总代价
- 费用：直接使用 `CandidateEdge::pickup_cost`

第一版没有做：

- 未服务请求惩罚
- 等待时间修正
- 真实道路 ETA
- 收益或重定位收益

这使得 MCMF 和贪心 baseline 可以在完全相同的候选边集合上对比。测试里保留了一个贪心会提前占用低成本边、导致最终匹配数量变少的案例，用来验证 MCMF 会通过残量网络重新调整匹配。

当前 demo 验证口径：

- `mcmf_batch_strategy_test` 验证 MCMF 能在同一候选边集合上优于贪心陷阱案例，并验证 batch 输入入口可用。
- `smoke` 验证 MCMF、batch baseline、单次派单、request 生命周期和 taxi 状态流可以一起构建并运行。

### 5.3 第一版费用定义

第一版最小费用建议直接定义为接驾代价：

`cost(i, j) = pickup_eta_seconds(i, j)`

如果暂时没有真实 ETA，可以退化为：

- 欧氏距离
- 曼哈顿距离
- tile 距离乘平均耗时

其中最推荐统一成“秒”作为费用单位，因为后面最好直接用整数费用跑最小费用流。

### 5.4 允许“不匹配”的建模

如果系统允许本轮不服务某些请求，就不能只做“最大匹配”。

建议把“不服务请求”也看成一种选择，并赋予惩罚。

常见处理方式：

- 为每个请求引入一个“未服务”选择
- 代价为 `penalty_unserved(j)`

等价理解：

- 给请求分配某辆车，代价是 `cost(i, j)`
- 本轮不匹配该请求，代价是 `penalty_unserved(j)`

这样算法自然会比较：

- 接单是否值得
- 如果接驾代价太大，是否应该把这单留到下一轮

第一版建议先用固定常数：

`penalty_unserved(j) = C`

例如：

- `C = 600`
- `C = 900`

单位按秒理解即可，表示如果接驾时间过大，则不如暂缓该请求。

### 5.5 考虑请求等待时间

如果系统按 batch 处理请求，那么老请求应当逐渐获得更高优先级。

建议第二版费用定义：

`cost(i, j) = pickup_eta_seconds(i, j) - lambda * waited_seconds(j)`

含义：

- 接驾越快越好
- 等得越久的请求，越应该被优先服务

如果不希望出现负费用，可以整体平移为非负：

`cost(i, j) = pickup_eta_seconds(i, j) + M - lambda * waited_seconds(j)`

其中 `M` 取足够大的常数。

### 5.6 推荐的阶段化成本模型

建议分阶段推进：

第一版：

- `cost(i, j) = pickup_eta_seconds(i, j)`
- 目标是先跑通、先可解释

第二版：

- `cost(i, j) = pickup_eta_seconds(i, j) - lambda * waited_seconds(j)`
- 让老请求不会长期饿死

第三版：

- 保留等待时间项
- 再加入“不匹配惩罚”
- 允许部分请求留待下一轮处理

进一步版本才考虑：

- 订单收益
- 热点区域回流价值
- 司机重定位收益

这些都不建议在当前阶段提前引入。

## 6. 推荐的系统推进顺序

建议按这个顺序推进：

1. 定义标准化数据对象
2. 实现 CSV 预处理和 tile 映射
3. 做事件驱动回放器
4. 做 tile 级别状态容器
5. 先跑最近车或局部贪心 baseline
6. 再加最小费用最大流策略做对比

这样可以保证每一层都能独立验证，而不是把数据、地图、仿真、优化算法同时揉在一起。

## 7. 当前阶段不建议做的事

- 不要先做全纽约全局完全二分图
- 不要先接真实道路最短路
- 不要先引入 Redis / WebSocket 参与主调度流程
- 不要先追求并发和线程安全
- 不要先把历史数据所有字段都耦合到核心模型里

当前阶段最重要的是让系统的：

- 数据流
- 状态流
- 候选边生成
- 匹配策略接口
- 结果日志和指标

都足够清晰、可解释、可替换。
