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
- `SimpleTileCellIndex` is now the first `CellIndex` adapter over existing tile semantics.
- `--cell-stats-grid-cols` can re-encode requests and drivers through `SimpleTileCellIndex` so CellIndex-backed heat/cold stats participate in matching.
- CellIndex hotspot scores can now use neighbor-ring smoothing and parent-cell fallback before they become opportunity cost.
- Optional route cost can enter matching through a route-cost CSV side table; `pickup_cost` remains the replay timeline fact.
- Route-cost CSVs now load both `taxi_id/request_id` edge keys and coordinate-based route-pair keys.
- Optional dispatch opportunity cost can enter matching through `dispatch_cost`; `pickup_cost` remains the replay timeline fact.
- `scripts/run_cost_grid_search.ps1` runs ranked parameter calibration sweeps over opportunity scale, cold penalty, and hot discount.
- H3-like `CellIndex` remains the next spatial modeling direction after the seam proves useful.

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
- `scripts/project_doctor.ps1` now checks progressive-disclosure entry docs, the legacy shim, and inline repo paths.

## Current Risks

| Risk | Why it matters | Current handling |
|---|---|---|
| Entry docs drift | Agents may follow stale presentation-era routes | `README.md`, `PROJECT_STATUS.md`, `INDEX.md`, `docs/README.md`, and `plan/README.md` now split roles |
| Rectangular tile overfitting | `simpleTile` is useful but weak for real spatial reasoning | `SimpleTileCellIndex` now isolates the baseline behind a `CellIndex` seam and can feed replay stats via `--cell-stats-grid-cols` |
| Display facts leaking into dispatch facts | OSRM routes and OSM basemap look more realistic than replay facts | Route artifacts stay display-only unless exported as an explicit route-cost CSV side table for `dispatch_cost` |
| Cost semantics blur | Pricing or opportunity penalties can be mistaken for travel time | `pickup_cost` and `dispatch_cost` are now separate |
| Hand-tuned opportunity parameters | One chosen scale/penalty can overfit a tiny smoke | `scripts/run_cost_grid_search.ps1` produces ranked grid-search evidence |
| Candidate-edge growth | `unlimited` candidates inflate sorting and matching cost | Default to finite k; use `unlimited` only as stress/upper bound |
| Missing executable doc check | Broken doc paths can survive until a human notices | `scripts/project_doctor.ps1` checks entry docs, stable docs, legacy shim size, and inline repo paths |

## Current Next Actions

Legal next actions without changing project direction:

1. Keep running `scripts/project_doctor.ps1` before report packaging or handoff.
2. Scale the route-cost and cost-grid comparisons beyond the 120-second smoke window once runtime budget allows.
3. Build a cell-bucket `CandidateEdgeGenerator` if candidate generation itself needs to use CellIndex neighborhoods.
4. Only after the abstraction is tested in one real path, evaluate whether H3 should become a second adapter.

Do not do these as part of the next slice:

- Do not replace the whole replay path with H3 directly.
- Do not put OSRM ETA into `pickup_cost`; route ETA may influence matching only through the explicit route-cost side table.
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
| Report scenario workflow | `scripts/run_report_scenarios.ps1` |
| Cost grid search | `scripts/run_cost_grid_search.ps1` |
| Project doctor | `scripts/project_doctor.ps1` |
| C++ build file | `CMakeLists.txt` |
| Core headers | `include/` |
| Core implementation | `src/` |
| C++ tests | `tests/` |
| Cell index seam | `include/cell_index.h`, `src/cell_index.cpp`, `tests/cell_index_test.cpp` |
| Cell-backed stats path | `include/tile_grid_stats.h`, `src/tile_grid_stats.cpp`, `tests/tile_grid_stats_test.cpp` |
| Go and export tools | `tools/` |
| Static viewer | `web/map_viewer/` |

## Tool Roles

- `replay_csv_demo`: run one normalized replay and emit summary/CSV artifacts, including optional candidate route pairs.
- `k_sweep`: scan radius/k/candidate-generation parameters and emit metrics; can use CellIndex-backed stats.
- `scripts/run_report_scenarios.ps1`: generate report-ready baseline, CellIndex opportunity, and candidate-route artifacts under `build-local/`.
- `scripts/run_cost_grid_search.ps1`: grid-search opportunity-cost parameters and write ranked calibration outputs.
- `scripts/project_doctor.ps1`: check progressive-disclosure docs, legacy shim shape, and inline repo paths.
- `tools/go_csv_preprocess`: convert Kaggle raw rows into normalized replay input.
- `tools/go_batch_experiments`: orchestrate batch sweeps across limits, modes, radii, k values, and tile resolutions.
- `tools/go_experiment_summary`: summarize experiment CSVs and region-size evidence.
- `tools/geojson_export`: convert tile/request CSV evidence into map-ready GeoJSON.
- `tools/replay_visual_export`: convert replay CSV outputs into live/batch viewer artifacts.
- `tools/route_visual_export`: map live display paths or candidate route pairs onto OSRM-compatible routes, deduplicate/cache route pairs, and export route costs as CSV.

## Confirmed Boundaries

- Go owns raw CSV drift and experiment/report orchestration.
- C++ owns replay state, candidate edges, MCMF, and lifecycle facts.
- Frontend owns static explanation, not dispatch.
- Pricing and route reports do not affect completion rate unless explicit dispatch-cost options are provided.
- Region and heat layers explain or audit spatial structure; they do not silently become dispatch constraints.

## Latest Evidence

Report scenario smoke without router:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_report_scenarios.ps1 -BuildDir build-codex-check -OutputDir build-local\report-scenarios-smoke -EndTime 120 -Radius 0.03 -K 1
```

Observed output:

- `baseline`: `assigned=9`, `completed=9`, `mcmf_cost=1321`, `applied_pickup_cost=1321`.
- `cell_opportunity`: `assigned=9`, `completed=9`, `mcmf_cost=1437`, `applied_pickup_cost=1321`.
- `candidate_routes.csv`: `9` pre-matching taxi-to-pickup route pairs.

Report scenario smoke with local OSRM:

```powershell
docker run -d --rm --name osrm-codex-report -p 5000:5000 -v "${PWD}\data\osm\osrm-ny:/data" ghcr.io/project-osrm/osrm-backend osrm-routed --algorithm mld /data/new-york-latest.osrm
powershell -ExecutionPolicy Bypass -File scripts\run_report_scenarios.ps1 -BuildDir build-codex-check -OutputDir build-local\report-scenarios-route-smoke -EndTime 120 -Radius 0.03 -K 1 -RunRouter -RouteMaxFeatures 9
docker stop osrm-codex-report
```

Observed output from `build-local/report-scenarios-route-smoke/summary.md`:

- `baseline`: `assigned=9`, `completed=9`, `mcmf_cost=1321`, `applied_pickup_cost=1321`.
- `cell_opportunity`: `assigned=9`, `completed=9`, `mcmf_cost=1437`, `applied_pickup_cost=1321`.
- `route_cost_cell_opportunity`: `assigned=9`, `completed=9`, `mcmf_cost=772`, `applied_pickup_cost=1321`.
- `route_costs.csv`: `9` `dispatch_to_pickup` rows with `route_status=routed`.

Interpretation: CellIndex-backed opportunity cost and offline road-network route seconds now participate in `dispatch_cost` while `pickup_cost` remains the replay timeline fact. The replay loop still does not call OSRM directly; route cost enters through a cached CSV side table.

Cost grid-search smoke:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_cost_grid_search.ps1 -BuildDir build-codex-check -OutputDir build-local\cost-grid-search-smoke -EndTime 120 -Radius 0.03 -K 1 -OpportunityScales 0,25 -ColdPenalties 1 -HotDiscounts 0,1 -CellNeighborRings 1 -CellNeighborWeight 0.5 -CellParentGridCols 10 -CellParentWeight 0.25
```

Observed output:

- `build-local/cost-grid-search-smoke/cost_grid_search.csv`: `4` ranked parameter rows.
- Best row in the smoke: `scale=0`, `cold_penalty=1`, `hot_discount=1`, `assigned=9`, `completed=9`, `mcmf_cost=1321`.

Route-cost grid-search smoke:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_cost_grid_search.ps1 -BuildDir build-codex-check -OutputDir build-local\cost-grid-search-route-smoke -EndTime 120 -Radius 0.03 -K 1 -RouteCostCsv build-local\report-scenarios-route-smoke\route_costs.csv -OpportunityScales 25 -ColdPenalties 1 -HotDiscounts 1 -CellNeighborRings 1 -CellNeighborWeight 0.5 -CellParentGridCols 10 -CellParentWeight 0.25
```

Observed output:

- `route_cost_edges=9`, `route_cost_pairs=9`.
- Best row in the smoke: `scale=25`, `cold_penalty=1`, `hot_discount=1`, `assigned=9`, `completed=9`, `mcmf_cost=792`.
