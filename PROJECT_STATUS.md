# Project Status

## Objective

Build an offline taxi dispatch replay lab that can compare candidate-generation strategies, explain hot/cold spatial effects, and produce static visualization artifacts without turning the project into an online service.

Current fixed engineering question:

> How can better spatial indexing and regional statistics generate a smaller candidate-edge set without lowering dispatch quality?

The user-facing design direction is H3-like multi-resolution spatial statistics, not naive rectangular tiling as the final idea. The current rectangular `simpleTile(grid_cols)` path remains the baseline and compatibility layer.

## Current State

The project has a stable C++ core, Go data/report/export tools, and a static React/MapLibre viewer.

Current active path:

```text
Go preprocess
-> normalized CSV
-> C++ replay / k_sweep
-> Go batch experiments and summary
-> GeoJSON / replay / route visual exports
-> Map Viewer
```

Current default strategy:

- `scan + finite k` remains the default candidate-generation path.
- `indexed` remains a correctness and performance comparison path.
- `unlimited` remains a theory/stress path only.
- `simpleTile(grid_cols)` remains the spatial baseline.
- H3-like `CellIndex` is the next spatial modeling direction, not yet implemented.

## Latest Project Health Check

Architecture health is good at the core and weaker at the entry-map layer.

Strong signals:

- `taxi_core` keeps stable C++ dispatch/replay logic behind a single library target.
- Tests cover spatial index, request lifecycle, batch dispatch, MCMF, replay, CSV IO, tile stats, and region map.
- Go tools are separated by purpose: preprocess, experiment runner, summary, GeoJSON export, replay visual export, route visual export.
- Display boundaries are explicit: Map Viewer reads static files and does not call back into dispatch.
- Spatial modeling docs already preserve the right direction: `simpleTile` baseline first, H3-like `CellIndex` later.

Main friction:

- The old `index_.md` and `plan/dispatch_next_steps.md` were doing too much at once: status, history, architecture, module map, plan, and presentation notes.
- First-pass navigation mixed active work with provenance from the May presentation phase.
- There is no repo-local doctor script yet to check whether entry docs point to existing paths.

## Current Risks

| Risk | Why it matters | Current handling |
|---|---|---|
| Entry docs drift | Agents may follow stale presentation-era routes | `README.md`, `PROJECT_STATUS.md`, `INDEX.md`, `docs/README.md`, and `plan/README.md` now split roles |
| Rectangular tile overfitting | `simpleTile` is useful but weak for real spatial reasoning | Keep it as baseline; design H3-like `CellIndex` as next abstraction |
| Display facts leaking into dispatch facts | OSRM routes and OSM basemap look more realistic than replay facts | Keep route artifacts display-only |
| Candidate-edge growth | `unlimited` candidates inflate sorting and matching cost | Default to finite k; use `unlimited` only as stress/upper bound |
| Missing executable doc check | Broken doc paths can survive until a human notices | Future slice: add a lightweight project doctor |

## Current Next Actions

Legal next actions without changing project direction:

1. Add a lightweight doc/path checker for entry files, similar in spirit to the novel project doctor.
2. Design `CellIndex` as a narrow abstraction before choosing real H3 bindings.
3. Keep `simpleTile` as the first `CellIndex` adapter so old CSV and experiments stay comparable.
4. Add a small fixture that proves `CellIndex` neighbor/parent/boundary semantics before connecting it to candidate generation.
5. Only after the abstraction is tested, evaluate whether H3 should become a second adapter.

Do not do these as part of the next slice:

- Do not replace the whole replay path with H3 directly.
- Do not put OSRM ETA into `pickup_cost` or MCMF cost.
- Do not convert Map Viewer into an online dispatch service.
- Do not make region map a hard dispatch boundary.

## Active Artifacts

| Artifact | Path |
|---|---|
| Project entrypoint | `README.md` |
| Agent map | `AGENTS.md` |
| Fast index | `INDEX.md` |
| Legacy index shim | `index_.md` |
| Current state | `PROJECT_STATUS.md` |
| Stable docs map | `docs/README.md` |
| Execution plans | `plan/README.md` |
| Next-step plan | `plan/dispatch_next_steps.md` |
| C++ build file | `CMakeLists.txt` |
| Core headers | `include/` |
| Core implementation | `src/` |
| C++ tests | `tests/` |
| Go and export tools | `tools/` |
| Static viewer | `web/map_viewer/` |

## Tool Roles

- `replay_csv_demo`: run one normalized replay and emit summary/CSV artifacts.
- `k_sweep`: scan radius/k/candidate-generation parameters and emit metrics.
- `tools/go_csv_preprocess`: convert Kaggle raw rows into normalized replay input.
- `tools/go_batch_experiments`: orchestrate batch sweeps across limits, modes, radii, k values, and tile resolutions.
- `tools/go_experiment_summary`: summarize experiment CSVs and region-size evidence.
- `tools/geojson_export`: convert tile/request CSV evidence into map-ready GeoJSON.
- `tools/replay_visual_export`: convert replay CSV outputs into live/batch viewer artifacts.
- `tools/route_visual_export`: map live display paths onto OSRM-compatible route polylines.

## Confirmed Boundaries

- Go owns raw CSV drift and experiment/report orchestration.
- C++ owns replay state, candidate edges, MCMF, and lifecycle facts.
- Frontend owns static explanation, not dispatch.
- Pricing reports do not affect completion rate unless a future explicitly designed dispatch-cost integration is added.
- Region and heat layers explain or audit spatial structure; they do not silently become dispatch constraints.
