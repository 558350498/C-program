# Docs

This directory holds stable design documents. It should explain durable boundaries, not carry the whole task queue.

## Read Order

1. `system_modeling.md`: layer boundaries, data flow, and display/replay separation.
2. `timeline_model.md`: replay event ordering and request/taxi lifecycle.
3. `glossary.md`: dispatch, pricing, cost, and spatial vocabulary.
4. `algorithm_and_strategy.md`: non-ML dispatch, candidate, and pricing rules.
5. `region_design.md`: tile / region / heat semantics and the H3-like `CellIndex` direction.

For current state, read `../PROJECT_STATUS.md`.

For current execution slices, read `../plan/README.md` and `../plan/dispatch_next_steps.md`.

## Active Concepts

| Concept | Current meaning |
|---|---|
| Replay fact | Output from C++ replay, request outcomes, and batch logs |
| Display artifact | Static JSON/GeoJSON derived from replay facts |
| `pickup_cost` | Replay timeline fact for pickup arrival and applied pickup totals |
| `dispatch_cost` | Matching objective used by greedy and MCMF |
| Route cost | Explicit route-cost CSV side table; never a silent rewrite of `pickup_cost` |
| Opportunity cost | Rule-based hot/cold adjustment to `dispatch_cost` |
| `simpleTile(grid_cols)` | Current baseline spatial bucket implementation |
| `TileGridStats` | Pickup/dropoff heat and free-driver side table |
| `TileRegionMap` | Offline constrained UF audit output |
| `CellIndex` | Abstraction for multi-resolution spatial cells; `SimpleTileCellIndex` can feed replay stats via `--cell-stats-grid-cols` and optional neighbor/parent smoothing |
| H3 | Future adapter candidate, not current dependency |
| OSRM route polyline | Display geometry by default; optional route-cost CSV source for `dispatch_cost` |

## Update Rules

- Keep stable boundary decisions here.
- Keep volatile status in `../PROJECT_STATUS.md`.
- Keep task sequencing in `../plan/`.
- Keep terminology updates synchronized with `glossary.md`.
- Do not duplicate module path tables from `../INDEX.md`.
- Keep presentation prompts and generated report notes out of stable docs.
