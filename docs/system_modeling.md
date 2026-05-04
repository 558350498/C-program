# 出租车调度系统建模笔记

这份文档只记录稳定设计边界和建模原则。函数位置看 `index_.md`，事件推进看 `docs/timeline_model.md`，当前任务看 `plan/dispatch_next_steps.md`。

## 1. 当前架构边界

项目分成四层：

1. 数据预处理层
   - Go 读取 Kaggle raw CSV。
   - Go 负责字段适配、时间解析、坐标过滤、tile 映射、合成司机。
   - 输出标准化 `requests.csv` / `drivers.csv`。

2. 标准化输入层
   - C++ 只读取项目内部标准化 CSV。
   - `dispatch_replay_io` 把 CSV 转成 `PassengerRequest` / `DriverSnapshot`。
   - C++ 不依赖 Kaggle 原始字段名。

3. 调度核心层
   - `TaxiSystem` 维护 taxi 状态、空间索引同步和 request 生命周期回写。
   - `RequestContext` 管单个 request 状态。
   - `dispatch_batch` 生成候选边和贪心 baseline。
   - `McmfBatchStrategy` 只在候选边集合上做匹配。
   - `ISpatialIndex` 只负责空间查询，业务数据通过 side table 挂载。

4. 离线回放层
   - `DispatchReplaySimulator` 负责事件时间线。
   - replay 使用 MCMF 结果正式回写，greedy 用作对比指标。
   - replay 输出总指标、每轮 batch 日志和 per-request outcome。
   - `k_sweep` 基于 replay outcome 做每个实验 row 的 hot/cold dropoff 分组报告。

## 2. 数据流

当前主线数据流：

```text
Kaggle NYC.csv
  -> Go preprocess
  -> data/normalized/requests.csv
  -> data/normalized/drivers.csv
  -> C++ dispatch_replay_io
  -> DispatchReplaySimulator
  -> candidate edges
  -> greedy / MCMF
  -> TaxiSystem::apply_assignment
  -> replay report / request outcomes
  -> k_sweep hot/cold grouped metrics
```

边界原则：

- Go 处理变化频繁的 raw schema 和清洗策略。
- C++ 处理稳定的调度状态机、候选边、匹配算法和回放指标。
- 两边通过文件交互，不用 cgo，不传 C++ 对象或 STL 容器。
- Go 可以继续补充业务估算列，但不同策略下的服务效果以 C++ replay outcome 为事实源。

## 3. 虚空行走模型

当前阶段不模拟真实道路。

规则：

- `Point(x, y)` 直接使用经纬度或二维坐标。
- 候选边 `pickup_cost` 表示接驾耗时。
- taxi 被派单后进入 `occupy`。
- 到达 pickup event 后 request 进入 `serving`。
- trip complete event 时，taxi 位置直接更新到 `dropoff_location`。
- 未匹配 request 留到后续 batch。

这个模型的目的不是还原真实交通，而是验证：

- request 生命周期闭环
- taxi 占用和释放
- 候选边生成
- greedy / MCMF 对比
- replay 指标输出

## 4. 候选边和匹配建模

候选边含义：

```text
taxi_id -> request_id, pickup_cost
```

生成约束：

- 只使用可用司机。
- 只使用已到达 batch 时间的 request。
- 支持半径筛选。
- 支持 `same_tile_only` 粗筛。
- 支持每个 request 的 top-k 限制。
- 候选边会规范化：过滤非法边，并对重复 `taxi_id/request_id` 保留最低 cost。

当前有两条候选边生成路径：

- 全量扫描路径：作为默认 replay 路径和 baseline。
- indexed 对照路径：使用 KD-Tree `radius_query` 查询候选司机 id，再通过 `taxi_id -> DriverSnapshot` side table 取业务数据。

indexed 路径暂不替换默认 replay，先用于验证空间索引抽象和后续性能优化。

replay outcome 记录每个 request 在当前策略下的服务结果：

- 是否进入过 pending batch。
- 候选边覆盖次数和候选边数量。
- 是否成功派单。
- 是否完成。
- 实际接驾成本和派单等待时间。

这些字段用于把全局 replay 指标拆成 per-row 分组指标，例如 hot dropoff / cold dropoff 的完成率、候选边覆盖率和接驾成本。

MCMF 第一版：

```text
source -> taxi -> request -> sink
```

- 每辆 taxi 容量 1。
- 每个 request 容量 1。
- taxi-request 边费用是 `pickup_cost`。
- 先最大化匹配数量，再最小化总代价。

暂不建模：

- 未服务惩罚
- 等待时间修正
- 真实道路 ETA
- 司机收益
- 重定位收益

## 5. Tile 建模

当前 tile 是简单固定网格，用作粗筛。

定位：

- tile 不是为了替代 KD-Tree。
- tile 是第一层候选集合压缩。
- KD-Tree 或距离计算可作为细筛。

推荐方向：

```text
tile bucket -> 候选车辆集合 -> 距离/cost -> 匹配策略
```

空间索引抽象原则：

- KD-Tree、grid index、tile bucket 都应隐藏在 `ISpatialIndex` 或候选生成器之后。
- 空间索引返回轻量 `id + distance_sq` 查询结果。
- taxi、request、heat 等业务字段放在外部 side table。
- 后续热区统计和格点地图不应直接把业务对象塞进 KD-Tree node。

当前热区原型使用 pickup tile 频次作为轻量 heat side table：

```text
dropoff_hotspot = pickup_heat(dropoff_tile) / max_pickup_heat
cold_dropoff = 1 - dropoff_hotspot
```

`k_sweep` 对每个策略 row 输出 hot/cold dropoff 分组效果：

- 请求数
- 候选边覆盖率
- 派单率
- 完成率
- 平均订单距离
- 平均接驾成本

第一版机会成本只做报表估算，不写入 dispatch 权重：

```text
opportunity_adjustment =
  cold_dropoff_penalty * cold_dropoff
  - hot_dropoff_discount * dropoff_hotspot
```

## 6. 当前不建议做

当前阶段不要做：

- 全纽约全量完全二分图。
- 真实道路最短路。
- API / Socket / WebSocket 在线服务。
- 多线程事件处理。
- Redis 或外部数据库。
- cgo / C++ dll。
- 把 Kaggle raw 字段耦合进 C++ 核心。

这些都可以以后做，但不应该阻塞当前离线 replay 主线。

## 7. 设计原则

- 先文件边界，后服务边界。
- 先小样本闭环，后大数据规模。
- 先可解释指标，后复杂优化。
- 变化频繁的 schema 放 Go。
- 稳定的状态机和算法放 C++。
- 策略只输出 `Assignment`，状态回写统一走 `TaxiSystem`。
