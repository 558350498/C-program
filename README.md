# Taxi Dispatch Replay Lab

这是一个离线出租车调度 replay 与空间统计实验项目。

它不是在线打车平台、不是订单后台，也不是机器学习定价系统。项目目标是把 NYC taxi 原始数据转成可复盘的离线事实流：预处理订单和司机快照，运行 C++ replay，比较候选边和匹配策略，导出静态可视化 artifact，再用 Map Viewer 解释调度结果、热区/冷区和计价实验。

当前主线已经从“做展示 MVP”进入“继续深化空间建模”的阶段。展示层可以解释 replay 事实，但不能反向定义 replay、dispatch、MCMF cost 或订单生命周期。

## Fixed Kernel

v1 固定服务这一条工程内核：

```text
Kaggle CSV
-> normalized requests/drivers CSV
-> C++ replay / candidate edges / MCMF
-> grouped metrics and per-request outcomes
-> static JSON/GeoJSON artifacts
-> MapLibre viewer explanation
```

硬规则：

- C++ replay outcome 是完成率、等待、接驾成本和订单生命周期的事实源。
- Go 可以清洗 raw CSV、编排实验、汇总报表和导出展示文件。
- 前端只读取静态 artifact，不触发 replay，不派单，不写回订单。
- OSRM / OSM / route polyline 只属于展示层，不能写回 `pickup_cost` 或 MCMF cost。
- Pricing v1 只进报表和抽样订单解释，不能改变 dispatch 结果。

## Active Path

当前入口只看这一条路线：

```text
tools/go_csv_preprocess/
-> data/normalized/requests.csv + data/normalized/drivers.csv
-> replay_csv_demo / k_sweep
-> tools/go_batch_experiments/
-> tools/geojson_export/ + tools/replay_visual_export/ + tools/route_visual_export/
-> web/map_viewer/
```

当前默认实验口径：

| Role | Current rule |
|---|---|
| Candidate generation | `scan + finite k` |
| Indexed path | correctness/performance comparison only |
| Stress path | `unlimited`, theory upper bound only |
| Spatial baseline | `simpleTile(grid_cols)` |
| Resolution sweep | `100 / 200 / 400` grid cols |
| Region map | constrained offline UF audit output |
| Future spatial direction | H3-like multi-resolution `CellIndex` |

## Working Loop

```text
raw CSV
-> preprocess window
-> replay or k sweep
-> grouped metrics
-> export static artifacts
-> inspect viewer
-> record next engineering move
```

When changing the algorithmic path, keep the loop evidence-backed:

1. Add or update a focused test for the interface being changed.
2. Run the relevant C++ or Go check.
3. Update `PROJECT_STATUS.md` if the active path changes.
4. Update `plan/dispatch_next_steps.md` only when the next executable slice changes.

## Hard Artifacts

| Artifact | Purpose | Current home |
|---|---|---|
| System model | Stable boundaries between Go, C++, replay, tools, and viewer | `docs/system_modeling.md` |
| Timeline model | Event order and request/taxi lifecycle | `docs/timeline_model.md` |
| Strategy model | Non-ML candidate, pricing, and opportunity-cost rules | `docs/algorithm_and_strategy.md` |
| Region model | Tile / region / heat / H3-like upgrade boundary | `docs/region_design.md` |
| Current state | Active path, risks, and legal next actions | `PROJECT_STATUS.md` |
| Fast index | File/module navigation only | `INDEX.md` |
| Execution plans | Durable repo-level plan slices | `plan/` |
| Map viewer | Static replay explanation surface | `web/map_viewer/` |

## Commands

Build and run C++ tests:

```powershell
cmake -S . -B build-mingw -G "MinGW Makefiles"
cmake --build build-mingw
ctest --test-dir build-mingw --output-on-failure
```

Run the Vite map viewer:

```powershell
cd web\map_viewer
npm install
npm run dev
```

Run Go tool tests:

```powershell
cd tools\replay_visual_export
go test ./...

cd ..\route_visual_export
go test ./...
```

Generate a small normalized sample:

```powershell
cd tools\go_csv_preprocess
go run . `
  -input ..\..\data\datasets\nyc-taxi-trip-duration\raw\NYC.csv `
  -output ..\..\data\normalized\requests.csv `
  -drivers-output ..\..\data\normalized\drivers.csv `
  -window-seconds 86400 `
  -limit 1000
```

## Progressive Disclosure

Use this order when entering the project:

1. `AGENTS.md`: agent workflow and repo setup notes.
2. `README.md`: current kernel, hard rules, and active route.
3. `PROJECT_STATUS.md`: current state, risk, and legal next actions.
4. `INDEX.md`: fast file/module map.
5. `docs/README.md`: stable domain and architecture docs.
6. `docs/system_modeling.md`: layer boundaries and data flow.
7. `docs/region_design.md`: spatial modeling and H3-like `CellIndex` direction.
8. `plan/README.md`: durable execution plan registry.
9. `plan/dispatch_next_steps.md`: next actionable implementation slices.
10. `web/map_viewer/README.md`: viewer-specific artifact contract.

## Active And Provenance Split

Active docs should point to runnable paths, current evidence, and the next small engineering move.

Provenance docs can explain how the project reached this route, but they should not dominate first-pass navigation. Old presentation-oriented notes belong behind `docs/ppt_prompt.md` or commit history, not in the primary entry path.
