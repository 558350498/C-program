# 调度系统下一阶段计划

## 当前判断

本阶段最初不直接开写完整 MCMF，而是按更稳的顺序推进：

1. 先补批量匹配的数据入口和候选边生成。
2. 再写一个批量贪心 baseline。
3. 最后把 MCMF 作为独立 batch strategy 实现接进来。

原因很简单：真正容易乱的是输入批次、候选边、请求状态释放和结果回写。

## 目标

在当前 `TaxiSystem + IRequestContext + ISpatialIndex + IDispatchStrategy` 骨架上，继续补齐离线数据回放和批量匹配能力，为后续接入 Kaggle CSV、tile 地图和最小费用最大流策略做准备。

当前优先级仍然是把 C++ 作业版本跑稳，不提前引入复杂服务化和跨语言工程。

## 下一阶段主线

### 1. 数据标准化

- 当前状态：已完成第一版。

- 定义请求、行程、车辆状态、tile 标识等内部统一类型。
- 增加 CSV 预处理层，不让调度层直接依赖原始字段名。
- 统一时间单位、坐标清洗规则和无效数据过滤规则。

建议最小类型：

- `PassengerRequest`
- `TripRecord`
- `DriverSnapshot`
- `TileId`
- `Assignment`

当前代码位置：

- `include/dispatch_batch.h`
- `tests/dispatch_batch_test.cpp`

后续补充：

- CSV 原始字段解析仍未接入，建议继续用 Go 单独实现预处理 CLI。
- 经纬度到 tile 的映射规则仍未实现。
- C++ 已接入标准化 `requests.csv` / `drivers.csv` loader。

Go 预处理边界：

- 输入：Kaggle 原始 taxi trip CSV。
- 输出：标准化 `requests.csv` 和 `drivers.csv`。
- C++ 不直接依赖 Kaggle 字段名。
- 第一版只做抽样、时间排序、坐标清洗和简单 tile 映射。

C++ 标准化 CSV 读取边界：

- `load_passenger_requests_csv` 读取 `requests.csv`。
- `load_driver_snapshots_csv` 读取 `drivers.csv`。
- loader 只依赖标准化字段，不依赖 Kaggle 原始字段名。
- 坏行写入 `errors`，好行保留，便于先跑小样本。

### 2. 批量匹配输入

- 当前状态：已完成第一版。

- 定义一个 batch 输入结构，包含一批可用 drivers 和一批 pending requests。
- batch 输入只使用快照数据，不直接暴露 `TaxiSystem` 内部容器。
- batch 输出统一为 `Assignment` 列表。

建议先不要让 batch strategy 直接修改系统状态。策略只负责给出结果，状态回写由 `TaxiSystem` 或后续的回放器处理。

当前实现：

- `BatchDispatchInput` 保存 `batch_time`、`drivers`、`requests`。
- `DriverSnapshot` 表示司机快照。
- `PassengerRequest` 表示标准化乘客请求。
- `Assignment` 表示策略输出。
- `TaxiSystem::apply_assignment` 负责状态回写。

### 3. 候选边生成

- 当前状态：已完成第一版。

- 先用半径搜索或 top-k 近邻生成候选边。
- 每条边记录 `taxi_id`、`request_id`、`pickup_cost`。
- 第一版 cost 用接驾距离或估算接驾时间。
- 暂时不接真实路网。

这一步是 MCMF 的前置，不建议跳过。

当前实现：

- `CandidateEdge` 记录候选匹配边。
- `CandidateEdgeStats` 记录候选边生成过程中的司机数、请求数、候选边数和无候选请求数。
- `CandidateEdgeGenerationResult` 同时返回候选边和统计信息。
- `CandidateEdgeOptions` 控制半径、cost 单位、每个请求 top-k 和同 tile 粗筛。
- `normalize_candidate_edges` 过滤非法边，并对重复 `taxi_id/request_id` 保留最低 cost。
- `generate_candidate_edges_with_stats` 供 replay 和后续 CSV 数据质量排查使用。
- `generate_candidate_edges` 只处理已到达 batch 时间的请求和当前可用司机。
- `estimate_pickup_cost` 使用欧氏距离乘 `seconds_per_distance_unit`，输出整数 cost。

### 4. 批量贪心 baseline

- 当前状态：已完成第一版。

- 在同一套 batch 输入和候选边上先实现一个简单贪心策略。
- 用它验证数据流、候选边和结果回写是否正确。
- 后续 MCMF 的测试可以和 baseline 做对比。

当前实现：

- `greedy_batch_assign(std::vector<CandidateEdge>)`
- `greedy_batch_assign(const BatchDispatchInput&, double, double)`
- 按 `pickup_cost` 从小到大贪心选择，保证同一辆 taxi 和同一个 request 只匹配一次。

状态回写：

- batch baseline 不直接修改系统状态。
- 回写时按 `Assignment::request_id` 找到对应 request，然后调用 `TaxiSystem::apply_assignment`。
- `apply_assignment` 会处理 request/taxi 校验、占用 taxi、绑定 request，以及绑定失败时释放 taxi。

### 5. MCMF 批量匹配策略

- 当前状态：已完成第一版。

- 在候选边稳定后实现最小费用最大流。
- 第一版目标函数：最小化总接驾 cost。
- 第二版再考虑 request 等待时间。
- 第三版再考虑未服务惩罚。

当前不做：

- 不做全纽约全图完全匹配。
- 不做真实道路最短路。
- 不做收益预测和重定位收益。

当前实现：

- `include/mcmf_batch_strategy.h`
- `src/mcmf_batch_strategy.cpp`
- `tests/mcmf_batch_strategy_test.cpp`
- `McmfBatchStrategy::assign(const std::vector<CandidateEdge>&)`
- `McmfBatchStrategy::assign(const BatchDispatchInput&, const CandidateEdgeOptions&)`
- MCMF 入口会先执行候选边规范化，避免非法边和重复边污染流图。

第一版语义：

- 每辆 taxi 最多匹配一个 request。
- 每个 request 最多被一辆 taxi 服务。
- 在候选边集合内尽量做最大匹配。
- 对所有最大匹配，选择 `pickup_cost` 总和最小的方案。

后续补充：

- 等待时间项：`pickup_cost - lambda * waited_seconds`
- 未服务惩罚：允许部分请求留到下一轮
- 与 greedy baseline 输出做指标对比

### 6. 空间离散化

- 当前状态：未开始。

- 先做固定 tile 网格。
- 建立 `tile -> requests / free drivers` 状态容器。
- 保留 KD-Tree 作为 tile 内或候选集合内的细筛能力。

### 7. 事件驱动回放

- 当前状态：已完成第一版虚空行走事件回放器。

- 做统一事件时间轴。
- 支持请求到达、车辆完成订单、车辆重新空闲等事件。
- 先用小时间窗口数据回放验证流程。

当前文档：

- `docs/timeline_model.md`

当前实现：

- `include/dispatch_replay.h`
- `src/dispatch_replay.cpp`
- `tests/dispatch_replay_test.cpp`

第一版约定：

- 每 30 秒做一次 batch matching。
- 未匹配请求留到下一轮。
- `pickup_cost` 表示接驾耗时。
- 行程时长暂定 600 秒，后续从数据读取。
- 订单完成时 taxi 直接更新到 `dropoff_location`。

### 8. 指标与日志

- 当前状态：已完成第一版。

- 记录匹配率、平均等待时间、平均接驾时间、未服务请求数。
- 对批次构图、未服务决策、状态释放失败补日志。

当前实现：

- `DispatchReplayMetrics` 记录总请求数、派单数、完成数、未服务数、batch 数、候选边总数、greedy/MCMF 匹配数和总 cost、实际接驾 cost。
- `DispatchReplayBatchLog` 记录每轮 batch 的可用司机数、pending 请求数、候选边数、无候选请求数、greedy/MCMF 结果和实际回写结果。
- `DispatchReplayReport` 汇总总指标和 batch 日志。
- `DispatchReplaySimulator::run_report` 返回完整报告。
- `format_dispatch_replay_report` 输出命令行可读摘要。

当前尚未做：

- 平均等待时间。
- CSV 输入后的数据质量报告。
- 失败回写原因的结构化统计。

## 第二轮解耦讨论建议

当前已经可以开始讨论第二轮解耦，但不建议大拆。原因是现在真正阻塞 CSV 接入的不是算法接口，而是输入/输出边界和日志边界。

建议优先考虑的小解耦：

1. `TaxiSystem` 日志解耦
   - 当前 `TaxiSystem` 直接写 `std::cerr` / `std::clog`。
   - 接 CSV 批量回放后日志会非常吵。
   - 当前已增加最小日志开关，replay 默认 silent。

2. replay 输入解耦
   - 当前 `DispatchReplaySimulator::run_report` 已经接收标准化内存对象。
   - 当前已新增 CSV loader，把文件解析和 replay 核心分开。
   - 文件放在 `include/dispatch_replay_io.h` / `src/dispatch_replay_io.cpp`，没有塞进 `DispatchReplaySimulator`。

3. 候选边生成器解耦
   - 当前 `generate_candidate_edges` 是 header-only 函数，适合第一版。
   - 后续如果加入 tile bucket、等待时间项、真实 ETA 或更复杂统计，可以拆成 `CandidateEdgeGenerator`。
   - 现在可以先不拆，等 CSV 数据进来后看实际瓶颈和配置复杂度。

4. 成本模型解耦
   - 当前 cost 是欧氏距离乘系数。
   - 等待时间项、未服务惩罚和真实 ETA 不应该直接堆进 MCMF 算法。
   - 建议后续抽出 `PickupCostModel` 或先做纯函数式 cost builder。

结论：

- 现在可以做第二轮解耦的设计讨论。
- `TaxiSystem` 日志开关和 replay CSV loader 已落地。
- 暂时不要重构 `TaxiSystem` 主状态机，也不要提前服务化。

## 当前任务收束

本轮已经完成：

- 批量匹配标准化数据入口。
- 候选边生成。
- top-k 和同 tile 粗筛入口。
- 批量贪心 baseline。
- `Assignment` 到 `TaxiSystem` 状态的统一回写入口。
- MCMF 批量匹配第一版。
- replay 总指标和每轮 batch 日志。
- 候选边生成统计。
- 候选边规范化。

本轮 demo 验证：

- `mcmf_batch_strategy_test`：验证 MCMF 最大匹配 + 最小 cost 行为。
- `smoke`：验证当前所有核心测试和演示程序可以一起构建运行。

下一步建议：

1. 扩展 Go 预处理工具输出 `drivers.csv`。
2. 增加一个小型 replay CLI，读取 `data/normalized/requests.csv` / `drivers.csv` 并输出 report。
3. 用小规模 CSV 样本跑通 `CSV -> replay -> report`。
4. 再根据样本报告决定是否抽 `CandidateEdgeGenerator` 和成本模型。

## 低优先级探索：Go + C++ 跨语言方案

这部分不是当前主线任务，只作为后续工程化方向保留。当前阶段不要因为它打断 C++ 作业版本。

### 方案 A：Go 调 C++ CLI

优先级：低，但如果以后要跨语言，这是最平滑的第一步。

思路：

- 当前优先做更轻的文件式边界：Go 先把 Kaggle CSV 转成标准化 `drivers.csv` / `requests.csv`。
- C++ replay 从标准化文件读取输入并运行调度仿真。
- 后续如果需要 CLI 管线，再让 C++ 编译成 `replay_demo.exe` 或 `matcher.exe`。
- Go 可以继续负责 CSV、tile、抽样、数据清洗和批处理脚本。

适合边界：

- 输入：driver snapshot + request snapshot。
- 输出：assignment list。
- 不跨语言传 C++ 对象、指针、STL 容器或生命周期。

### 方案 B：Go 调 C++ 算法服务

优先级：更低，等主线稳定后再考虑。

思路：

- C++ 单独启动 matcher service。
- Go 通过 HTTP / gRPC 调用 C++ 算法服务。
- 两边通过明确的 DTO 通信。

### 当前不建议：cgo / dll

暂时不建议做 `Go + cgo + C++ dll`。

原因：

- C++ 类不能直接安全暴露给 Go。
- 需要包 C ABI。
- 内存分配和释放边界容易出错。
- Windows 链接配置麻烦。
- 对当前作业主线收益不高。

## 当前阶段原则

- 先保证 C++ 作业版本能独立交付。
- 跨语言只传快照数据，不传对象生命周期。
- Go 如果接入，优先负责非底层工程能力。
- C++ 优先保留算法核心和数据结构实验能力。
- 不因为未来可能服务化而提前污染当前接口。
