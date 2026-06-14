# Dispatch Next Steps

This file records the current executable plan only. For broad project state, read `../PROJECT_STATUS.md`; for module navigation, read `../INDEX.md`.

## Current Health Summary

The dispatch/replay core is in reasonable shape:

- C++ core modules are centralized behind `taxi_core`.
- Tests cover the important replay and matching interfaces.
- Go tools are separated by workflow role.
- Map Viewer remains a static artifact reader.
- Docs now use progressive disclosure instead of one large mixed index.

The main architectural gap is no longer “can the system replay and show a demo?” It is now:

```text
Can the project make spatial candidate generation deeper
without breaking replay comparability?
```

## Completed Slice: CellIndex, Dispatch Cost Split, And Route Cost Side Table

Implemented in this iteration:

- `include/cell_index.h` / `src/cell_index.cpp`: `CellIndex` and `SimpleTileCellIndex`.
- `tests/cell_index_test.cpp`: encode, neighbors, boundary, and parent semantics.
- `CandidateEdge` and `Assignment` now keep both `pickup_cost` and `dispatch_cost`.
- Greedy and MCMF optimize `dispatch_cost`.
- Replay still uses `pickup_cost` for pickup arrival and trip timing.
- `k_sweep` and `replay_csv_demo` now accept `--dispatch-opportunity-cost-scale`.
- `k_sweep` and `replay_csv_demo` now accept `--route-cost-csv` plus `--route-cost-scale`.
- `tools/route_visual_export` can optionally export routed feature costs as CSV.
- `replay_csv_demo` can export pre-matching candidate route pairs with `--candidate-routes-csv`.
- `tools/route_visual_export` can route candidate route pairs with `--input-route-pairs-csv`.
- `tools/route_visual_export` deduplicates candidate route coordinates and can persist route results with `--route-cache-csv`.
- `k_sweep` and `replay_csv_demo` can re-encode replay tiles with `--cell-stats-grid-cols`, letting CellIndex-backed heat/cold stats participate in `dispatch_cost`.
- `k_sweep` and `replay_csv_demo` can smooth CellIndex hotspot scores with neighbor rings and parent-cell fallback.
- `scripts/run_report_scenarios.ps1` generates a report-ready baseline vs CellIndex opportunity packet and candidate route pairs.

This is the first real bridge from regional statistics, pricing logic, and road-network route costs into dispatch. It deliberately does not put road ETA into `pickup_cost`.

## Completed Slice: Candidate Route Cost Precompute Entry

Goal: move from post-dispatch live-route costs toward precomputed candidate-pair route costs.

Current route-cost bridge:

```text
route-cost CSV
-> RouteDispatchCostModel
-> CandidateEdge.dispatch_cost
-> greedy / MCMF
```

Implemented entry path:

- Generate route-cost rows for candidate pairs before matching, not only for already assigned live paths.
- Keep the CSV contract: `taxi_id,request_id,leg_type,route_status,route_duration_s,route_distance_m`.
- Compare baseline `dispatch_cost == pickup_cost` against route-cost matching and record `mcmf_cost` plus `applied_pickup_cost`.

Route-cost cache work completed:

- Candidate route coordinates are deduplicated before OSRM calls.
- `--route-cache-csv` persists route results by start/end coordinate key.
- Cached runs reuse route rows without calling the router again.
- Route-cost CSV loading now supports both `taxi_id/request_id` edge keys and coordinate route-pair keys.

## Completed Slice: Parameter Grid Search

Goal: make opportunity-cost scale/penalty choices auditable instead of hand-waved.

Implemented artifact:

```text
scripts/run_cost_grid_search.ps1
```

Workflow:

```text
scale/cold/hot grid
-> k_sweep rows
-> score service coverage first, then dispatch cost
-> ranked cost_grid_search.csv
-> summary.md best row
```

Smoke evidence:

- No-route grid: `4` ranked rows under `build-local/cost-grid-search-smoke`.
- Route-cost grid: `route_cost_edges=9`, `route_cost_pairs=9`, best smoke row `mcmf_cost=792`.

## Completed Slice: CandidateEdgeGenerator Boundary

Goal: stop replay from owning every candidate-generation detail.

Implemented interface:

```text
CandidateEdgeGenerator
  -> ScanCandidateEdgeGenerator
  -> IndexedCandidateEdgeGenerator
```

Current integration:

- Scan replay path uses the generator interface.
- Indexed replay path still uses the maintained live KD-tree index directly, but now has the same interface shape available.
- Route cost has moved beyond `taxi_id/request_id` by also supporting coordinate route-pair keys.

## Completed Slice: Report Scenario Packet

Goal: make the report claims reproducible without relying on chat notes.

Implemented workflow:

```text
scripts/run_report_scenarios.ps1
-> baseline.csv
-> cell_opportunity.csv
-> candidate_routes.csv
-> summary.md
```

Smoke evidence:

- Baseline: `mcmf_cost=1321`, `applied_pickup_cost=1321`.
- CellIndex opportunity: `mcmf_cost=1437`, `applied_pickup_cost=1321`.
- Candidate route pairs: `9`.

Route evidence with local OSRM:

- Route-cost CellIndex opportunity: `mcmf_cost=772`, `applied_pickup_cost=1321`.
- Route-cost rows: `9` `dispatch_to_pickup` rows, all `route_status=routed`.

## Completed Slice: Lightweight Project Doctor

Goal: catch stale entry-map paths early.

Implemented artifact:

```text
scripts/project_doctor.ps1
```

Current checks:

- `README.md`, `PROJECT_STATUS.md`, `INDEX.md`, `docs/README.md`, and `plan/README.md` exist.
- Paths listed in the main navigation tables exist.
- `index_.md` remains only a legacy shim.
- `docs/README.md` points to stable docs only.
- `plan/dispatch_next_steps.md` stays under the configured line budget.

This mirrors the useful part of the novel project approach: make entry drift machine-visible before it becomes architecture debt.

## Completed Slice: CellIndex Statistics Integration

Goal achieved for the first production path: move the tested `CellIndex` seam from standalone module into replay statistics and dispatch-cost inputs without directly replacing the current CSV/replay baseline.

Implemented interface shape:

```text
CellIndex
  encode(lon, lat) -> cell_id
  neighbors(cell_id) -> cell_id list
  boundary(cell_id) -> polygon / bbox
  parent(cell_id, resolution) -> cell_id
```

First adapter is implemented:

```text
SimpleTileCellIndex
```

It wraps the current `simpleTile(grid_cols)` semantics so existing normalized CSV and multi-resolution experiments remain comparable.

Production entry path:

```text
--cell-stats-grid-cols N
-> SimpleTileCellIndex.encode(lon, lat)
-> replay request/driver tile ids
-> TileGridStats
-> optional neighbor-ring smoothing / parent-cell fallback
-> TileDispatchCostModel
-> CandidateEdge.dispatch_cost
```

Neighbor / parent design:

- Neighbor smoothing mirrors the H3 `grid_disk` idea: immediate or multi-ring neighbors can contribute to a cell's hotspot score with exponential decay.
- Parent fallback mirrors hierarchical H3 use: a sparse cell can borrow a coarse parent-cell average without changing the replay tile id.

Second adapter, only after the seam is tested:

```text
H3CellIndex
```

## Completed Slice: Route Cost Evidence Run

Goal: produce the first report packet that includes `route_cost_cell_opportunity.csv`.

Completed work:

- Start local OSRM-compatible router.
- Run `scripts/run_report_scenarios.ps1 -RunRouter`.
- Compare baseline, CellIndex opportunity, and route-cost CellIndex opportunity rows.

Observed smoke comparison:

| scenario | assigned | completed | mcmf_cost | applied_pickup_cost |
|---|---:|---:|---:|---:|
| baseline | 9 | 9 | 1321 | 1321 |
| cell_opportunity | 9 | 9 | 1437 | 1321 |
| route_cost_cell_opportunity | 9 | 9 | 772 | 1321 |

## Active Slice 1: Cell-Bucket Candidate Generation

Goal: avoid spreading spatial-grid decisions through dispatch logic.

Current rule:

- `scan + finite k` stays default.
- `indexed` stays comparison path.
- `unlimited` stays stress path.

Next useful design step:

```text
CandidateEdgeGenerator
  uses spatial query / cell bucket / side table
  emits normalized CandidateEdge list
```

`CellIndex` now feeds statistics, smoothing, and tile ids through explicit CLI paths. The next decision is whether a new generator should consume cell buckets directly instead of only influencing cost.

## Active Slice 2: Region Audit Viewer

Goal: expose `region_stats.csv` and `region_map.csv` as an audit layer without changing dispatch.

Preferred route:

```text
region_map.csv + region_stats.csv
-> region GeoJSON export
-> Map Viewer layer toggle
```

Boundaries:

- Region is an explanation/audit layer.
- Region is not a hard dispatch boundary.
- Region is not MCMF cost.
- Region is not dynamically redrawn every batch.

## Hold

Do not spend the next slice on:

- Full H3 replacement before the current `SimpleTileCellIndex` path is compared.
- Real-road ETA in `pickup_cost` or live HTTP routing inside the replay loop.
- Redis / WebSocket / online location services.
- Order CRUD or admin UI.
- More presentation-first material.
- Region as hard dispatch boundary.
- Pricing v1 inside MCMF cost without an explicit cost-scale experiment flag.

## Verification Commands

C++ core:

```powershell
cmake -S . -B build-mingw -G "MinGW Makefiles"
cmake --build build-mingw
ctest --test-dir build-mingw --output-on-failure
```

Go tool examples:

```powershell
cd tools\replay_visual_export
go test ./...

cd ..\route_visual_export
go test ./...
```

Viewer:

```powershell
cd web\map_viewer
npm run dev
```
