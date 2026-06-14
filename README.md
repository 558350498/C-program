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
- OSRM / OSM / route polyline 默认只属于展示层；只有显式提供 route-cost CSV side table 时才可进入 `dispatch_cost`，永远不写回 `pickup_cost`。
- Pricing v1 默认只进报表和抽样订单解释；只有显式 cost-scale 选项才可改变 dispatch matching。

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
| Cell abstraction | `SimpleTileCellIndex` over current tile semantics |
| Cell stats path | optional `--cell-stats-grid-cols N` replay/k-sweep re-encoding |
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

Current cost split:

- `pickup_cost` remains the replay timeline fact.
- `dispatch_cost` is the optional matching cost used by greedy/MCMF.
- Without explicit route-cost or opportunity-cost options, `dispatch_cost == pickup_cost`.
- `--route-cost-csv` can load road-network route seconds as a side table for matching.
- `--cell-stats-grid-cols` can make heat/cold stats come from `SimpleTileCellIndex` cells.
- `--dispatch-opportunity-cost-scale` can add hot/cold opportunity adjustment to that matching cost.
- `--cell-neighbor-rings` / `--cell-neighbor-weight` smooth hotspot scores over nearby cells.
- `--cell-parent-grid-cols` / `--cell-parent-weight` add coarse parent-cell fallback.
- `scripts/run_cost_grid_search.ps1` can grid-search scale/penalty/discount choices and rank candidate parameter sets.

Current report smoke evidence:

- Baseline: `assigned=9`, `completed=9`, `mcmf_cost=1321`, `applied_pickup_cost=1321`.
- CellIndex opportunity: `assigned=9`, `completed=9`, `mcmf_cost=1437`, `applied_pickup_cost=1321`.
- Route-cost CellIndex opportunity with local OSRM: `assigned=9`, `completed=9`, `mcmf_cost=772`, `applied_pickup_cost=1321`.

## Hard Artifacts

| Artifact | Purpose | Current home |
|---|---|---|
| System model | Stable boundaries between Go, C++, replay, tools, and viewer | `docs/system_modeling.md` |
| Timeline model | Event order and request/taxi lifecycle | `docs/timeline_model.md` |
| Strategy model | Non-ML candidate, pricing, and opportunity-cost rules | `docs/algorithm_and_strategy.md` |
| Region model | Tile / region / heat / H3-like upgrade boundary | `docs/region_design.md` |
| Cell index seam | H3-ready spatial cell abstraction | `include/cell_index.h`, `src/cell_index.cpp` |
| Cell-backed stats | Optional CellIndex replay/statistics bridge | `include/tile_grid_stats.h`, `src/tile_grid_stats.cpp` |
| Route cost side table | Optional road-network cost bridge into `dispatch_cost` | `tools/route_visual_export/`, `include/dispatch_batch.h` |
| Cost grid search | Parameter calibration smoke and ranked CSV output | `scripts/run_cost_grid_search.ps1` |
| Project doctor | Progressive-disclosure doc/path check | `scripts/project_doctor.ps1` |
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
powershell -ExecutionPolicy Bypass -File scripts\project_doctor.ps1
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

Generate the report scenario packet:

```powershell
powershell -ExecutionPolicy Bypass `
  -File scripts\run_report_scenarios.ps1 `
  -OutputDir build-local\report-scenarios `
  -EndTime 120 `
  -Radius 0.03 `
  -K 1
```

This writes `summary.md`, baseline/cell-opportunity CSVs, and candidate route pairs under `build-local\report-scenarios`. Add `-RunRouter` when a local OSRM-compatible router is available; route requests are deduplicated and cached in `route_cache.csv`.

Run a small cost-parameter grid search:

```powershell
powershell -ExecutionPolicy Bypass `
  -File scripts\run_cost_grid_search.ps1 `
  -BuildDir build-codex-check `
  -OutputDir build-local\cost-grid-search `
  -EndTime 120 `
  -Radius 0.03 `
  -K 1 `
  -OpportunityScales 0,25,50 `
  -ColdPenalties 1,2 `
  -HotDiscounts 0,1 `
  -CellNeighborRings 1 `
  -CellNeighborWeight 0.5 `
  -CellParentGridCols 10 `
  -CellParentWeight 0.25
```

Run a small CellIndex + opportunity-cost replay and export candidate route pairs:

```powershell
.\build-mingw\replay_csv_demo.exe `
  --requests data\normalized\requests.csv `
  --drivers data\normalized\drivers.csv `
  --end-time 120 `
  --radius 0.03 `
  --max-edges-per-request 1 `
  --cell-stats-grid-cols 100 `
  --dispatch-opportunity-cost-scale 25 `
  --candidate-routes-csv build-local\candidate_routes.csv
```

Route candidate pairs into a cost table, then feed the table back into dispatch:

```powershell
cd tools\route_visual_export
go run . `
  -input-route-pairs-csv ..\..\build-local\candidate_routes.csv `
  -route-cost-csv ..\..\build-local\route_costs.csv `
  -route-cache-csv ..\..\build-local\route_cache.csv

cd ..\..
.\build-mingw\replay_csv_demo.exe `
  --requests data\normalized\requests.csv `
  --drivers data\normalized\drivers.csv `
  --end-time 120 `
  --radius 0.03 `
  --max-edges-per-request 1 `
  --cell-stats-grid-cols 100 `
  --route-cost-csv build-local\route_costs.csv `
  --dispatch-opportunity-cost-scale 25
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
