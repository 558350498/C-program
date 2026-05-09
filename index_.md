# 工程导航索引

这份文件只负责快速定位模块。设计解释看 `docs/`，当前计划看 `plan/`。

## 必读顺序

1. `index_.md`
2. `docs/system_modeling.md`
3. `docs/timeline_model.md`
4. `docs/algorithm_and_strategy.md`
5. `docs/region_design.md`
6. `plan/dispatch_next_steps.md`

## 当前架构快照

当前项目已经形成稳定的离线 replay 实验架构：

```text
Go raw CSV preprocess
  -> normalized requests/drivers CSV
  -> C++ replay / dispatch / MCMF
  -> k_sweep grouped metrics
  -> Go experiment summary
```

当前默认实验口径：

- 候选生成：`scan + finite k`。
- `indexed`：保留为空间索引正确性和性能对照路径。
- `unlimited`：只作为理论上界和压力测试。
- `TileGridStats`：当前事实统计层，负责 tile heat/cold 和初始 free driver count。
- `TileRegionMap`：受约束离线 UF 审计产物，只解释 region 尺度，不接派单主线。
- 多分辨率对照：支持 `100 / 200 / 400` grid cols，用于判断 region 是否因为 tile 太粗而过大。

空间建模当前结论：

- 现在继续保留 `simpleTile(grid_cols)` 作为最小实现和兼容 baseline。
- H3 是更专业的后续候选路线，适合真实地理网格、层级分辨率、邻居查询和供需热区分析。
- 暂不把真实地图瓦片、OSM 路网、OSRM/GraphHopper/Valhalla 等作为派单实验依赖；这些属于地图展示或真实路由层。当前前端可使用在线 OSM raster 底图作为开发展示参考，但不反向影响 replay / dispatch。
- 下一步讨论重点是：是否抽象 `CellIndex`，先让 `simpleTile` 实现，再决定是否替换为 H3。

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
- `go_batch_experiments`：按样本规模、tile grid 分辨率、候选生成模式、半径和 k 值批量跑预处理与实验，输出总表。
- `go_experiment_summary`：读取实验总表，输出紧凑对照、推荐配置和可选 region 尺度汇总。

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

### Tile / Grid Stats

轻量 tile/grid side table，用于区域热度、冷区分数、初始可用司机数统计，以及 `k_sweep` 的 hot/cold dropoff 分组报告。

核心类型：

- `TileGridStats`
- `TileGridStatsEntry`
- `RequestTileFeatures`

核心入口：

- `build_tile_grid_stats`
- `hotspot_score`
- `cold_score`
- `request_tile_features`
- `format_tile_grid_stats_csv`

位置：

- `include/tile_grid_stats.h`
- `src/tile_grid_stats.cpp`
- `tests/tile_grid_stats_test.cpp`

### Tile Region Map

受约束离线 UF region map 原型。输入 `TileGridStats`，输出稳定的 `tile_id -> region_id` 和 region 聚合明细；只用于统计和审计，不影响 dispatch、MCMF cost 或候选生成。

`region_stats.csv` 会额外输出 bbox 粗略公里尺度：`approx_width_km`、`approx_height_km`、`approx_diagonal_km`、`approx_area_km2`。这些是按 Go `simpleTile()` 的当前 grid cols 估算的几何距离，不是真实路网距离。

多分辨率实验可通过 `-tile-grid-cols` / `--tile-grid-cols` 对照 `100 / 200 / 400` 三档网格。不同 grid cols 的 normalized CSV 必须分目录保存，避免 tile id 语义混用。

核心类型：

- `TileRegionMap`
- `TileRegionMapOptions`
- `TileRegionMapEntry`
- `TileRegionStatsEntry`

核心入口：

- `build_tile_region_map`
- `format_tile_region_map_csv`
- `format_tile_region_stats_csv`

位置：

- `include/tile_region_map.h`
- `src/tile_region_map.cpp`
- `tests/tile_region_map_test.cpp`

### Region Design

区域设计文档，记录 tile / region / heat 的边界：tile 是事实层，region 是慢变解释层，heat/cold 是快变状态。当前已有受约束离线 UF 原型，但不接派单主线。

位置：

- `docs/region_design.md`

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

读取 Kaggle raw CSV，输出标准化 replay 输入，并合成供不应求司机快照。支持 `-window-seconds` 选取连续 pickup-time 窗口，避免前 N 条 raw row 横跨过长时间；支持 `-tile-grid-cols` 调整 `simpleTile()` 网格分辨率。

位置：

- `tools/go_csv_preprocess/main.go`
- `tools/go_csv_preprocess/README.md`

输出：

- `data/normalized/requests.csv`
- `data/normalized/drivers.csv`

### GeoJSON Export

文件式前端数据桥。第一版把 `tile_stats.csv` 转成 `tile_stats.geojson`，供 MapLibre 静态 viewer 通过 Vite 静态文件服务加载；传入 `requests.csv` 时还会输出 `tile_corner_witnesses.geojson`，用于 hover tile 时显示四角最近 pickup witness。它不提供 HTTP API，也不触发 replay。

位置：

- `tools/geojson_export/main.go`
- `tools/geojson_export/go.mod`

常用命令：

```powershell
cd tools\geojson_export
go run . `
  -tile-stats ..\..\build-local\perf-sweeps-grid-sweep-smoke\normalized\grid_200\limit_1000\tile_stats.csv `
  -requests ..\..\build-local\perf-sweeps-grid-sweep-smoke\normalized\grid_200\limit_1000\requests.csv `
  -tile-grid-cols 200 `
  -output-dir ..\..\web\map_viewer\public\data
```

### Replay Visual Export

离线 replay 可视化产物导出器。它读取 `requests.csv`、`drivers.csv`、`request_outcomes.csv` 和 `batch_logs.csv`，只生成给前端展示用的静态文件，不重新运行 dispatch，也不改变 C++ replay 模型。

默认 `-mode auto`：`request_count <= 1000` 输出 live mode 的逐单虚空行走 artifact，`request_count > 1000` 输出 batch mode 的聚合时间线。

位置：

- `tools/replay_visual_export/main.go`
- `tools/replay_visual_export/go.mod`

输出：

- live mode：`replay_manifest.json`、`replay_live_paths.geojson`、`replay_live_points.geojson`
- batch mode：`replay_manifest.json`、`replay_batches.json`、`replay_batch_tiles.json`

### Map Viewer

本地 MapLibre 前端展示层。当前使用 Vite + React + TypeScript，在 `localhost:5173` 加载 `/data/tile_stats.geojson` 渲染真实 tile 方格；如果 GeoJSON 不存在，会回退到内置 sample 图层。前端还提供可开关的在线 OSM raster 底图，仅用于开发展示；如果存在 `/data/tile_corner_witnesses.geojson`，hover tile 时会显示该 tile 四角最近 pickup witness。Replay 面板会读取 `/data/replay/replay_manifest.json`，按 `live` / `batch` 模式展示对应 artifact 状态；batch mode 已支持 tick 滑块、播放游标和当前窗口 tile activity overlay。

位置：

- `web/map_viewer`

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
build-local\replay_csv_demo.exe --batch-log-csv build-local\batch_logs.csv --request-outcome-csv build-local\request_outcomes.csv
```

候选集规模扫描：

```powershell
build-local\k_sweep.exe --radii 0.01,0.03,0.05 --k-values 1,2,5,10
```

Tile/grid 明细导出：

```powershell
build-local\k_sweep.exe --radii 0.03 --k-values 1,2,5 --tile-stats-csv build-local\tile_stats.csv
```

Region map / stats 明细导出：

```powershell
build-local\k_sweep.exe --radii 0.03 --k-values 1,2,5 --region-map-csv build-local\region_map.csv --region-stats-csv build-local\region_stats.csv
```

多分辨率 tile / region sweep：

```powershell
cd tools\go_batch_experiments
go run . -limits 1000 -modes scan -radii 0.03 -k-values 1,2,5 -tile-grid-cols 100,200,400 -output-dir ..\..\build-local\perf-sweeps-grid-sweep-smoke
```

批量输出会包含总表 `summary.csv`，以及每档自己的 `grid_100\summary.csv`、`grid_200\summary.csv`、`grid_400\summary.csv`。

`--indexed-candidates` 和 `k=unlimited` 主要用于对照和压力测试，不作为默认实验口径。

Go 实验 runner：

```powershell
cd tools\go_experiments
go run .
```

批量性能 / 分组实验：

```powershell
cd tools\go_batch_experiments
go run . -limits 1000,5000 -modes scan -radii 0.01,0.03,0.05 -k-values 1,2,5,10

cd ..\go_experiment_summary
go run . -input ..\..\build-local\perf-sweeps\summary.csv
```
