# Project Index

This index is for quick navigation only. It should point to current paths and avoid re-explaining the whole project.

## Active Navigation

| Use | Entry |
|---|---|
| Project entrypoint | [README.md](./README.md) |
| Current state and next legal moves | [PROJECT_STATUS.md](./PROJECT_STATUS.md) |
| Stable docs map | [docs/README.md](./docs/README.md) |
| Execution plan map | [plan/README.md](./plan/README.md) |
| Current next-step plan | [plan/dispatch_next_steps.md](./plan/dispatch_next_steps.md) |
| Agent workflow map | [AGENTS.md](./AGENTS.md) |
| Map viewer docs | [web/map_viewer/README.md](./web/map_viewer/README.md) |

## Stable Design Docs

| Use | File |
|---|---|
| System layer boundaries | [docs/system_modeling.md](./docs/system_modeling.md) |
| Replay event timeline | [docs/timeline_model.md](./docs/timeline_model.md) |
| Candidate, pricing, and rule strategy | [docs/algorithm_and_strategy.md](./docs/algorithm_and_strategy.md) |
| Tile / region / heat / H3-like route | [docs/region_design.md](./docs/region_design.md) |
| Presentation-generation input | [docs/ppt_prompt.md](./docs/ppt_prompt.md) |

## Core C++ Modules

| Module | Interface | Implementation | Tests |
|---|---|---|---|
| Domain types | `include/taxi_domain.h` | `src/taxi_domain.cpp` | n/a |
| Taxi lifecycle | `include/taxi_system.h` | `src/taxi_system.cpp` | `tests/taxi_system_test.cpp` |
| Request lifecycle | `include/requestcontext.h` | `src/requestcontext.cpp` | `tests/requestcontext_test.cpp` |
| Dispatch strategy | `include/dispatch_strategy.h` | `src/dispatch_strategy.cpp` | `tests/dispatch_strategy_test.cpp` |
| Candidate edges | `include/dispatch_batch.h` | header-only / core library | `tests/dispatch_batch_test.cpp` |
| MCMF matching | `include/mcmf_batch_strategy.h` | `src/mcmf_batch_strategy.cpp` | `tests/mcmf_batch_strategy_test.cpp` |
| Replay simulator | `include/dispatch_replay.h` | `src/dispatch_replay.cpp` | `tests/dispatch_replay_test.cpp` |
| Replay CSV IO | `include/dispatch_replay_io.h` | `src/dispatch_replay_io.cpp` | `tests/dispatch_replay_io_test.cpp` |
| Spatial index | `include/spatial_index.h`, `include/kd_tree_spatial_index.h` | `src/kd_tree_spatial_index.cpp` | `tests/kd_tree_spatial_index_test.cpp` |
| Tile stats | `include/tile_grid_stats.h` | `src/tile_grid_stats.cpp` | `tests/tile_grid_stats_test.cpp` |
| Region map | `include/tile_region_map.h` | `src/tile_region_map.cpp` | `tests/tile_region_map_test.cpp` |

## C++ Executables

| Executable | Source | Role |
|---|---|---|
| `taxi_demo` | `src/main.cpp` | Small lifecycle smoke demo |
| `replay_csv_demo` | `src/replay_csv_demo.cpp` | Run one replay from normalized CSV |
| `k_sweep` | `src/k_sweep.cpp` | Sweep radius/k/candidate modes and emit metrics |

## Tooling

| Tool | Role |
|---|---|
| `tools/go_csv_preprocess/` | Kaggle raw CSV -> normalized requests/drivers |
| `tools/go_experiments/` | Single experiment runner with pricing/report columns |
| `tools/go_batch_experiments/` | Batch sweeps across limits, modes, radii, k, and grid cols |
| `tools/go_experiment_summary/` | Summarize experiment CSVs and region evidence |
| `tools/geojson_export/` | `tile_stats.csv` / requests -> GeoJSON |
| `tools/replay_visual_export/` | replay CSV outputs -> live/batch viewer artifacts |
| `tools/route_visual_export/` | live paths -> OSRM-compatible route polylines |
| `scripts/prepare_map_viewer_demo.ps1` | Prepare local viewer demo artifacts |

## Viewer

| Path | Role |
|---|---|
| `web/map_viewer/src/App.tsx` | Main React/MapLibre viewer |
| `web/map_viewer/src/styles.css` | Viewer styling |
| `web/map_viewer/public/data/` | Static artifact input directory |
| `web/map_viewer/README.md` | Viewer artifact contract and local usage |

## Data

| Path | Role |
|---|---|
| `data/datasets/nyc-taxi-trip-duration/raw/` | Raw dataset location |
| `data/normalized/requests.csv` | Normalized replay requests |
| `data/normalized/drivers.csv` | Synthetic/normalized driver snapshots |
| `build-local/` | Local generated build and experiment artifacts; keep out of source control |

## Common Commands

```powershell
cmake -S . -B build-mingw -G "MinGW Makefiles"
cmake --build build-mingw
ctest --test-dir build-mingw --output-on-failure
```

```powershell
cd tools\go_batch_experiments
go run . -limits 1000 -modes scan -radii 0.03 -k-values 1,2,5 -tile-grid-cols 100,200,400 -output-dir ..\..\build-local\perf-sweeps-grid-sweep-smoke
```

```powershell
cd web\map_viewer
npm run dev
```
