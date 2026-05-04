# 工程导航索引

这份文件只负责快速定位模块。设计解释看 `docs/`，当前计划看 `plan/`。

## 必读顺序

1. `index_.md`
2. `docs/system_modeling.md`
3. `docs/timeline_model.md`
4. `docs/algorithm_and_strategy.md`
5. `plan/dispatch_next_steps.md`

## 模块索引

### TaxiSystem

业务编排入口，负责 taxi 注册、上下线、位置更新、状态迁移、request 生命周期回写。

核心入口：

- `set_logging_enabled`
- `logging_enabled`
- `create_taxi`
- `register_taxi`
- `set_taxi_online`
- `set_taxi_offline`
- `update_taxi_position`
- `update_taxi_status`
- `apply_assignment`
- `dispatch_nearest`
- `start_trip`
- `complete_trip`
- `cancel_request`

位置：

- `include/taxi_system.h`
- `src/taxi_system.cpp`
- `tests/taxi_system_test.cpp`

关键约束：

- `occupy` taxi 不能直接 offline。
- `occupy` taxi 不能通过 `update_taxi_status(..., free)` 绕过 request 生命周期。
- 批量匹配结果统一交给 `apply_assignment` 回写。
- replay / CSV 批量场景默认关闭 `TaxiSystem` 日志。

### RequestContext

单个 request 生命周期和 taxi 绑定关系。

状态：

- `pending`
- `dispatched`
- `serving`
- `completed`
- `canceled`

位置：

- `include/requestcontext.h`
- `src/requestcontext.cpp`
- `tests/requestcontext_test.cpp`

### Batch Dispatch / Candidate Edges

标准化批量匹配输入、候选边生成、贪心 baseline。

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
- `generate_candidate_edges_indexed_with_stats`
- `generate_candidate_edges_indexed`
- `greedy_batch_assign`

位置：

- `include/dispatch_batch.h`
- `tests/dispatch_batch_test.cpp`

### McmfBatchStrategy

在候选边集合上做最小费用最大流：先最大化匹配数量，再最小化接驾总代价。

核心入口：

- `assign(const std::vector<CandidateEdge>&)`
- `assign(const BatchDispatchInput&, const CandidateEdgeOptions&)`

位置：

- `include/mcmf_batch_strategy.h`
- `src/mcmf_batch_strategy.cpp`
- `tests/mcmf_batch_strategy_test.cpp`

### DispatchReplay

离线事件回放器，负责 request arrival、batch dispatch、pickup arrival、trip complete 的推进和指标输出。

核心入口：

- `DispatchReplaySimulator::run`
- `DispatchReplaySimulator::run_report`
- `format_dispatch_replay_report`
- `format_dispatch_replay_batch_logs_csv`
- `format_dispatch_replay_request_outcomes_csv`
- `assignment_rate`
- `completion_rate`
- `average_applied_pickup_cost`

核心输出：

- `DispatchReplayMetrics`：总请求、派单、完成、候选边、耗时等汇总指标。
- `DispatchReplayBatchLog`：每轮 batch 的候选边、匹配和耗时日志。
- `DispatchReplayRequestOutcome`：每个 request 在 replay 中的候选边覆盖、派单、完成、接驾成本和等待时间。

位置：

- `include/dispatch_replay.h`
- `src/dispatch_replay.cpp`
- `tests/dispatch_replay_test.cpp`

### Replay CLI / Experiments

离线回放和参数扫描入口。

核心工具：

- `replay_csv_demo`：读取 normalized CSV，运行一次 replay 并输出 summary。
- `k_sweep`：对候选集规模 `max_edges_per_request` 和候选半径做批量扫描，输出 CSV。
- `go_experiments`：Go 实验编排层，调用 `k_sweep` 并补充供需比、订单里程收入、接驾成本、热点调价和粗略净收入估算。
- `go_batch_experiments`：按样本规模、候选生成模式、半径和 k 值批量跑预处理与实验，输出总表。
- `go_experiment_summary`：读取实验总表，输出紧凑对照和推荐配置。

位置：

- `src/replay_csv_demo.cpp`
- `src/k_sweep.cpp`
- `tools/go_experiments/main.go`
- `tools/go_experiments/go.mod`
- `tools/go_batch_experiments/main.go`
- `tools/go_experiment_summary/main.go`

常用输出字段：

- `assignment_rate`
- `completion_rate`
- `candidate_edges`
- `avg_pickup_cost`
- `candidate_generation_ms`
- `matching_ms`
- `replay_ms`
- `hot_dropoff_completion_rate`
- `cold_dropoff_completion_rate`
- `hot_dropoff_candidate_coverage_rate`
- `cold_dropoff_candidate_coverage_rate`
- `opportunity_adjustment_avg`
- `supply_demand_ratio`
- `estimated_net_revenue`
- `pricing_mode`
- `avg_price_factor`
- `max_price_factor`
- `hotspot_net_delta`

### Replay CSV IO

读取标准化 `requests.csv` / `drivers.csv`，转换成 C++ replay 内存对象。

核心入口：

- `load_passenger_requests_csv`
- `load_driver_snapshots_csv`

位置：

- `include/dispatch_replay_io.h`
- `src/dispatch_replay_io.cpp`
- `tests/dispatch_replay_io_test.cpp`

### Spatial Index

空闲 taxi 的空间索引抽象和 KD-Tree 实现。当前支持旧的 `radius_search -> Point`，也支持侧表挂载友好的 `radius_query / nearest_k -> SpatialQueryResult{id, distance_sq}`。

位置：

- `include/spatial_index.h`
- `include/kd_tree_spatial_index.h`
- `src/kd_tree_spatial_index.cpp`
- `tests/kd_tree_spatial_index_test.cpp`

### Dispatch Strategy

单次派单策略抽象和最近空闲车策略。

位置：

- `include/dispatch_strategy.h`
- `include/nearest_free_taxi_strategy.h`
- `src/dispatch_strategy.cpp`
- `tests/dispatch_strategy_test.cpp`

### Domain

基础领域对象。

核心类型：

- `Taxi`
- `Point`
- `TaxiStatus`

位置：

- `include/taxi_domain.h`
- `src/taxi_domain.cpp`

### Go CSV Preprocess

读取 Kaggle raw CSV，输出标准化 replay 输入，并合成供不应求司机快照。支持 `-window-seconds` 选取连续 pickup-time 窗口，避免前 N 条 raw row 横跨过长时间。

位置：

- `tools/go_csv_preprocess/main.go`
- `tools/go_csv_preprocess/README.md`

输出：

- `data/normalized/requests.csv`
- `data/normalized/drivers.csv`

## 常用命令

```powershell
cmake -S . -B build-mingw -G "MinGW Makefiles"
cmake --build build-mingw
ctest --test-dir build-mingw --output-on-failure
```

Go 小样本预处理：

```powershell
cd tools\go_csv_preprocess
go run . `
  -input ..\..\data\datasets\nyc-taxi-trip-duration\raw\NYC.csv `
  -output ..\..\data\normalized\requests.csv `
  -drivers-output ..\..\data\normalized\drivers.csv `
  -window-seconds 86400 `
  -limit 1000
```

Replay CLI：

```powershell
build-mingw\replay_csv_demo.exe --indexed-candidates --batch-log-csv build-mingw\batch_logs.csv --request-outcome-csv build-mingw\request_outcomes.csv
```

候选集规模扫描：

```powershell
build-mingw\k_sweep.exe --radii 0.01,0.03,0.05 --k-values 1,2,5,unlimited --indexed-candidates
```

Go 实验 runner：

```powershell
cd tools\go_experiments
go run .
```

批量性能 / 分组实验：

```powershell
cd tools\go_batch_experiments
go run . -limits 1000,5000,20000 -modes scan,indexed -radii 0.01,0.03,0.05 -k-values 1,2,5,unlimited

cd ..\go_experiment_summary
go run . -input ..\..\build-local\perf-sweeps\summary.csv
```
