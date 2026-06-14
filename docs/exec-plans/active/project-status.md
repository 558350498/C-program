# Project Status

## Objective

Build an offline taxi dispatch replay lab that can compare candidate-generation
strategies, explain hot/cold spatial effects, and produce static visualization
artifacts without turning the project into an online service.

Current fixed engineering question:

> How can better spatial indexing and regional statistics generate a smaller
> candidate-edge set without lowering dispatch quality?

The user-facing direction is H3-like multi-resolution spatial statistics. The
current rectangular `simpleTile(grid_cols)` path remains the baseline and
compatibility layer.

## Current Truth

- The project has a stable C++ replay/dispatch core, Go data/report/export
  tools, and a static React/MapLibre viewer.
- `scan + finite k` remains the default candidate-generation path.
- `indexed` is a correctness/performance comparison path.
- `unlimited` is a theory/stress path only.
- `SimpleTileCellIndex` is the first `CellIndex` adapter over existing tile
  semantics.
- `--cell-stats-grid-cols` can re-encode requests and drivers through
  `SimpleTileCellIndex` so CellIndex-backed heat/cold stats participate in
  matching.
- Neighbor-ring smoothing and parent-cell fallback can adjust hotspot scores
  before they become opportunity cost.
- Route-cost CSVs can feed `dispatch_cost` through taxi/request keys or
  coordinate route-pair keys.
- `pickup_cost` remains the replay timeline fact.
- `scripts/run_cost_grid_search.ps1` ranks opportunity-cost parameter choices.

## Current Risks

| Risk | Current handling |
|---|---|
| Entry docs drift | `scripts/project_doctor.ps1` checks required docs, line budgets, and inline paths |
| Cost semantics blur | `docs/design-docs/glossary.md` defines pricing, `pickup_cost`, `dispatch_cost`, route cost, and opportunity cost |
| Rectangular tile overfitting | `CellIndex` isolates the current tile baseline behind an adapter seam |
| Route/display facts leaking into dispatch facts | Route evidence affects matching only through explicit route-cost CSV side tables |
| Hand-tuned opportunity parameters | `scripts/run_cost_grid_search.ps1` produces ranked smoke evidence |
| Candidate-edge growth | Default remains finite k; `unlimited` stays a stress/upper-bound path |

## Latest Evidence

Report scenario smoke without router:

- Command: `scripts/run_report_scenarios.ps1 -BuildDir build-codex-check -OutputDir build-local\report-scenarios-smoke -EndTime 120 -Radius 0.03 -K 1`
- `baseline`: `assigned=9`, `completed=9`, `mcmf_cost=1321`, `applied_pickup_cost=1321`.
- `cell_opportunity`: `assigned=9`, `completed=9`, `mcmf_cost=1437`, `applied_pickup_cost=1321`.
- `candidate_routes.csv`: `9` pre-matching taxi-to-pickup route pairs.

Report scenario smoke with local OSRM:

- `route_cost_cell_opportunity`: `assigned=9`, `completed=9`, `mcmf_cost=772`, `applied_pickup_cost=1321`.
- `route_costs.csv`: `9` `dispatch_to_pickup` rows with `route_status=routed`.
- Interpretation: route seconds and CellIndex opportunity adjustments participate
  in `dispatch_cost`; they do not change `pickup_cost` or replay timing.

Cost grid-search smoke:

- No-route grid: `4` ranked rows, best smoke row `scale=0`, `cold_penalty=1`,
  `hot_discount=1`, `assigned=9`, `completed=9`, `mcmf_cost=1321`.
- Route-cost grid: `route_cost_edges=9`, `route_cost_pairs=9`, best smoke row
  `scale=25`, `cold_penalty=1`, `hot_discount=1`, `assigned=9`,
  `completed=9`, `mcmf_cost=792`.

## Legal Next Actions

1. Run `scripts/pre_submit_check.ps1` before homework packaging or handoff.
2. Scale the route-cost and cost-grid comparisons beyond the 120-second smoke
   window when runtime budget allows.
3. Build a cell-bucket `CandidateEdgeGenerator` if candidate generation itself
   needs to use CellIndex neighborhoods.
4. Evaluate an H3 adapter only after the current `CellIndex` seam is proven in
   one real path.

Do not replace the replay path with H3 directly, put OSRM ETA into
`pickup_cost`, convert the viewer into an online dispatch service, or make
region map a hard dispatch boundary.

## Where Details Live

| Detail | File |
|---|---|
| Agent entry and done criteria | `AGENTS.md` |
| Architecture overview | `ARCHITECTURE.md` |
| Fast path/module navigation | `docs/index.md` |
| Stable design docs | `docs/design-docs/index.md` |
| Cost and dispatch terms | `docs/design-docs/glossary.md` |
| Current executable slices | `docs/exec-plans/active/dispatch-next-steps.md` |
| Report evidence workflow | `scripts/run_report_scenarios.ps1` |
| Cost parameter workflow | `scripts/run_cost_grid_search.ps1` |
| Pre-submit gate | `scripts/pre_submit_check.ps1` |
