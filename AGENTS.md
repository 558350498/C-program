# Agent Entry

This repo is an offline taxi dispatch replay lab. Treat it as a reproducible
algorithm and evidence project, not as an online ride-hailing service.

## Read Order

Start with these files before changing code:

1. `README.md`: project kernel, hard boundaries, and navigation.
2. `PROJECT_STATUS.md`: current truth, risks, and latest verified evidence.
3. `INDEX.md`: fast file/module map.
4. `docs/README.md`: stable design-doc map.
5. `docs/glossary.md`: cost, pricing, dispatch, and spatial terms.
6. `plan/README.md`: durable plan registry.
7. `plan/dispatch_next_steps.md`: current executable slices.

Use `docs/system_modeling.md`, `docs/timeline_model.md`,
`docs/algorithm_and_strategy.md`, and `docs/region_design.md` when the task
touches architecture, replay timing, dispatch cost, pricing, or spatial cells.

## Repo Layout

| Path | Role |
|---|---|
| `include/`, `src/` | C++ replay, candidate generation, dispatch, MCMF, and domain logic |
| `tests/` | C++ unit and integration tests |
| `tools/go_csv_preprocess/` | Raw taxi CSV to normalized replay inputs |
| `tools/go_batch_experiments/` | Batch experiment orchestration |
| `tools/go_experiment_summary/` | Experiment result summaries |
| `tools/geojson_export/` | Map-ready GeoJSON export |
| `tools/replay_visual_export/` | Replay artifacts for the viewer |
| `tools/route_visual_export/` | OSRM-compatible route and route-cost CSV export |
| `web/map_viewer/` | Static React/MapLibre explanation surface |
| `scripts/` | Reproducible project checks and report/evidence workflows |
| `docs/` | Stable architecture and domain documentation |
| `plan/` | Current and durable execution plans |
| `build-local/`, `build-*` | Generated local artifacts; do not commit |

## Common Commands

Run the pre-submit gate:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\pre_submit_check.ps1
```

Run only progressive-disclosure and path checks:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\project_doctor.ps1
```

Build and test manually:

```powershell
cmake -S . -B build-local -G "MinGW Makefiles"
cmake --build build-local
ctest --test-dir build-local --output-on-failure
```

Generate the no-router report evidence packet:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_report_scenarios.ps1 -BuildDir build-local -OutputDir build-local\report-scenarios -EndTime 120 -Radius 0.03 -K 1
```

## Engineering Boundaries

- C++ replay is the source of truth for request lifecycle, completion, wait
  time, `pickup_cost`, and per-request outcomes.
- Go tools may preprocess data, orchestrate experiments, summarize results,
  and export static artifacts.
- The viewer reads static artifacts only. It must not dispatch orders, call
  replay, or write request state.
- OSRM/OSM routes are display or offline side-table evidence by default.
  They can affect matching only through an explicit route-cost CSV feeding
  `dispatch_cost`; they must not rewrite `pickup_cost`.
- Pricing v1 is explanatory/reporting by default. It may affect matching only
  through explicit cost-scale options.
- `simpleTile(grid_cols)` is the baseline spatial bucket. `CellIndex` is the
  abstraction boundary for multi-resolution or H3-like future work.
- Keep generated outputs under `build-local/` or other ignored build dirs.

## Done Criteria

For non-trivial code or docs work, finish with evidence:

- The relevant tests or scripts ran, or the reason they could not run is stated.
- `scripts/project_doctor.ps1` passes when entry docs or paths changed.
- `scripts/pre_submit_check.ps1` passes before packaging or homework handoff,
  unless a heavier check was intentionally skipped and documented.
- Updated docs preserve the progressive-disclosure split: README for kernel,
  PROJECT_STATUS for current truth, INDEX for navigation, docs for stable
  decisions, and plan for executable slices.
- Cost terminology matches `docs/glossary.md`.

## Do Not

- Do not put live routing HTTP calls inside the replay loop.
- Do not put real-road ETA into `pickup_cost`.
- Do not let front-end artifacts define replay or dispatch facts.
- Do not replace the replay path with H3 directly; add adapters behind
  `CellIndex` only after the seam is proven.
- Do not commit generated build outputs, CSV evidence packets, PDFs, zips, or
  local viewer data.

## Issue Tracker Notes

Issues and PRDs for this repo are tracked in GitHub Issues for
`558350498/C-program`. See `docs/agents/issue-tracker.md`,
`docs/agents/triage-labels.md`, and `docs/agents/domain.md` when using the
issue-tracker workflow.
