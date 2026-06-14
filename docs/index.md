# Documentation Index

This is the repository documentation map. It should point to current paths and
avoid re-explaining the whole project.

## First-Pass Navigation

| Need | Entry |
|---|---|
| Project entrypoint | `../README.md` |
| Agent/task loading map | `../AGENTS.md` |
| Architecture overview | `../ARCHITECTURE.md` |
| Current state and risks | `exec-plans/active/project-status.md` |
| Current executable slices | `exec-plans/active/dispatch-next-steps.md` |
| Stable design docs | `design-docs/index.md` |
| Viewer artifact contract | `../web/map_viewer/README.md` |

## Design Docs

| Need | File |
|---|---|
| Cost and dispatch vocabulary | `design-docs/glossary.md` |
| System layer boundaries | `design-docs/system-modeling.md` |
| Replay event timeline | `design-docs/timeline-model.md` |
| Candidate, pricing, and matching rules | `design-docs/algorithm-and-strategy.md` |
| Tile, region, CellIndex, H3-like direction | `design-docs/region-and-cell-design.md` |

## Execution Plans

| Need | File |
|---|---|
| Plan registry | `exec-plans/index.md` |
| Active status | `exec-plans/active/project-status.md` |
| Active dispatch/spatial slices | `exec-plans/active/dispatch-next-steps.md` |
| External or long-form references | `references/index.md` |

## Core C++ Modules

| Module | Interface | Implementation | Tests |
|---|---|---|---|
| Domain types | `include/taxi_domain.h` | `src/taxi_domain.cpp` | n/a |
| Taxi lifecycle | `include/taxi_system.h` | `src/taxi_system.cpp` | `tests/taxi_system_test.cpp` |
| Request lifecycle | `include/requestcontext.h` | `src/requestcontext.cpp` | `tests/requestcontext_test.cpp` |
| Dispatch strategy | `include/dispatch_strategy.h` | `src/dispatch_strategy.cpp` | `tests/dispatch_strategy_test.cpp` |
| Cell index | `include/cell_index.h` | `src/cell_index.cpp` | `tests/cell_index_test.cpp` |
| Candidate edges | `include/dispatch_batch.h` | header-only / core library | `tests/dispatch_batch_test.cpp` |
| MCMF matching | `include/mcmf_batch_strategy.h` | `src/mcmf_batch_strategy.cpp` | `tests/mcmf_batch_strategy_test.cpp` |
| Replay simulator | `include/dispatch_replay.h` | `src/dispatch_replay.cpp` | `tests/dispatch_replay_test.cpp` |
| Replay CSV IO | `include/dispatch_replay_io.h` | `src/dispatch_replay_io.cpp` | `tests/dispatch_replay_io_test.cpp` |
| Spatial index | `include/spatial_index.h`, `include/kd_tree_spatial_index.h` | `src/kd_tree_spatial_index.cpp` | `tests/kd_tree_spatial_index_test.cpp` |
| Tile / cell stats | `include/tile_grid_stats.h` | `src/tile_grid_stats.cpp` | `tests/tile_grid_stats_test.cpp` |
| Region map | `include/tile_region_map.h` | `src/tile_region_map.cpp` | `tests/tile_region_map_test.cpp` |

## Tools

| Tool | Role |
|---|---|
| `tools/go_csv_preprocess/` | Raw CSV to normalized replay inputs |
| `tools/go_batch_experiments/` | Batch sweeps across limits, modes, radii, k, and grid cols |
| `tools/go_experiment_summary/` | Summarize experiment CSVs and region evidence |
| `tools/geojson_export/` | `tile_stats.csv` / requests to GeoJSON |
| `tools/replay_visual_export/` | Replay CSV outputs to live/batch viewer artifacts |
| `tools/route_visual_export/` | Live paths or candidate route pairs to route geometry and route-cost CSV |

## Commands

```powershell
powershell -ExecutionPolicy Bypass -File scripts\project_doctor.ps1
```

```powershell
powershell -ExecutionPolicy Bypass -File scripts\architecture_lint.ps1
```

```powershell
powershell -ExecutionPolicy Bypass -File scripts\pre_submit_check.ps1
```
