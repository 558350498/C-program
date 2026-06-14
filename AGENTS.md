# Agent Context Map

Use this file as a table of contents, not as the project encyclopedia. Load the
smallest deeper document that matches the task.

## Always Load

| Need | Source |
|---|---|
| Project kernel | `README.md` |
| Architecture sketch | `ARCHITECTURE.md` |
| Full documentation map | `docs/index.md` |
| Current state | `docs/exec-plans/active/project-status.md` |

## Task Routing

| If the task is about... | Load... |
|---|---|
| Terminology, cost names, pricing words | `docs/design-docs/glossary.md` |
| System layers, data flow, viewer boundary | `docs/design-docs/system-modeling.md` |
| Replay event order, request lifecycle, timing | `docs/design-docs/timeline-model.md` |
| Candidate edges, MCMF, matching, Pricing v1 | `docs/design-docs/algorithm-and-strategy.md` |
| Tile stats, CellIndex, region map, H3-like work | `docs/design-docs/region-and-cell-design.md` |
| Active implementation slices | `docs/exec-plans/active/dispatch-next-steps.md` |
| Current risks, latest smoke evidence | `docs/exec-plans/active/project-status.md` |
| Historical or completed plan records | `docs/exec-plans/completed/` |
| External references or long copied notes | `docs/references/` |
| Issue tracker workflow | `docs/agents/issue-tracker.md` |
| Triage label mapping | `docs/agents/triage-labels.md` |
| Agent/domain doc policy | `docs/agents/domain.md` |
| Viewer usage and artifact contract | `web/map_viewer/README.md` |
| Data folders | `data/datasets/nyc-taxi-trip-duration/README.md`, `data/normalized/README.md` |
| Tool-specific usage | The `README.md` in that tool directory |

## Code Routing

| If touching... | Start at... |
|---|---|
| C++ replay and dispatch core | `include/`, `src/`, `tests/` |
| Candidate generation | `include/dispatch_batch.h`, `src/dispatch_replay.cpp` |
| MCMF | `include/mcmf_batch_strategy.h`, `src/mcmf_batch_strategy.cpp` |
| CellIndex | `include/cell_index.h`, `src/cell_index.cpp`, `tests/cell_index_test.cpp` |
| Tile stats | `include/tile_grid_stats.h`, `src/tile_grid_stats.cpp` |
| Region map | `include/tile_region_map.h`, `src/tile_region_map.cpp` |
| Replay CSV IO | `include/dispatch_replay_io.h`, `src/dispatch_replay_io.cpp` |
| Go preprocessing/export tools | `tools/` |
| Map viewer | `web/map_viewer/` |
| Verification scripts | `scripts/` |

## Verification Routing

| Need | Command |
|---|---|
| Docs/path check | `powershell -ExecutionPolicy Bypass -File scripts\project_doctor.ps1` |
| Architecture lint only | `powershell -ExecutionPolicy Bypass -File scripts\architecture_lint.ps1` |
| Homework handoff gate | `powershell -ExecutionPolicy Bypass -File scripts\pre_submit_check.ps1` |
| Report scenario evidence | `powershell -ExecutionPolicy Bypass -File scripts\run_report_scenarios.ps1` |
| Cost parameter sweep | `powershell -ExecutionPolicy Bypass -File scripts\run_cost_grid_search.ps1` |

## Default Rules

- Prefer loading one routed document before reading broad file sets.
- Keep generated files out of docs and source control.
- Put stable terms in `docs/design-docs/glossary.md`.
- Put current execution state in `docs/exec-plans/active/`.
- Put durable architecture boundaries in `docs/design-docs/`.
- Run `scripts/project_doctor.ps1` after changing docs or paths.
- Keep layer boundaries enforceable through `scripts/architecture_lint.ps1`.

## Do Not Load By Default

- Generated outputs under `build-local/` or `build-*`.
- Local datasets under `data/datasets/**`.
- Viewer generated data under `web/map_viewer/public/data/`.
- Old presentation drafts, reports, PDFs, zips, and local homework files.
