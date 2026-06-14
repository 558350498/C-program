# Taxi Dispatch Replay Lab

这是一个离线出租车调度 replay 与空间统计实验项目。

项目目标是把 NYC taxi 原始数据转成可复盘的离线事实流：预处理订单和司机快照，运行 C++ replay，比较候选边和匹配策略，导出静态可视化 artifact，再用 Map Viewer 解释调度结果、热区/冷区、计价估算和路线证据。

它不是在线打车平台、不是订单后台，也不是机器学习定价系统。展示层可以解释 replay 事实，但不能反向定义 replay、dispatch、MCMF cost 或订单生命周期。

## Fixed Kernel

v1 固定工程内核：

```text
Kaggle CSV
-> normalized requests/drivers CSV
-> C++ replay / candidate edges / MCMF
-> grouped metrics and per-request outcomes
-> static JSON/GeoJSON artifacts
-> MapLibre viewer explanation
```

## Hard Boundaries

- C++ replay outcome 是完成率、等待、接驾成本和订单生命周期的事实源。
- Go 可以清洗 raw CSV、编排实验、汇总报表和导出展示文件。
- 前端只读取静态 artifact，不触发 replay，不派单，不写回订单。
- OSRM / OSM / route polyline 默认属于展示层或离线 side table；只有显式 route-cost CSV 才可进入 `dispatch_cost`。
- `pickup_cost` 永远是 replay 时间线事实，不写入真实路网 ETA。
- Pricing v1 默认只进入报表和抽样订单解释；只有显式 cost-scale 选项才可改变 dispatch matching。
- `simpleTile(grid_cols)` 是当前空间基线，`CellIndex` 是未来 H3-like 多分辨率网格的抽象边界。

术语以 `docs/glossary.md` 为准。

## Active Path

当前主线：

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

## Progressive Disclosure

Use this order when entering the project:

1. `AGENTS.md`: agent workflow, repo layout, commands, boundaries, done criteria.
2. `PROJECT_STATUS.md`: current state, risks, latest evidence, legal next actions.
3. `INDEX.md`: fast file/module map.
4. `docs/README.md`: stable domain and architecture docs.
5. `docs/glossary.md`: dispatch, pricing, cost, and spatial terms.
6. `plan/README.md`: durable execution plan registry.
7. `plan/dispatch_next_steps.md`: next actionable implementation slices.
8. `web/map_viewer/README.md`: viewer-specific artifact contract.

## Verification

Run the normal pre-submit gate before packaging or handoff:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\pre_submit_check.ps1
```

For a lighter documentation-only check:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\project_doctor.ps1
```

Generated reports, CSV evidence packets, viewer data, and local builds belong under ignored build/output directories such as `build-local/` and `build-*`.
