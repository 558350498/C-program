# Docs

This directory holds stable design documents. It should explain durable boundaries, not carry the whole task queue.

## Read Order

1. `system_modeling.md`: layer boundaries, data flow, and display/replay separation.
2. `timeline_model.md`: replay event ordering and request/taxi lifecycle.
3. `algorithm_and_strategy.md`: non-ML dispatch, candidate, and pricing rules.
4. `region_design.md`: tile / region / heat semantics and the H3-like `CellIndex` direction.
5. `ppt_prompt.md`: presentation-generation source, useful for provenance and demos.

For current state, read `../PROJECT_STATUS.md`.

For current execution slices, read `../plan/README.md` and `../plan/dispatch_next_steps.md`.

## Active Concepts

| Concept | Current meaning |
|---|---|
| Replay fact | Output from C++ replay, request outcomes, and batch logs |
| Display artifact | Static JSON/GeoJSON derived from replay facts |
| `simpleTile(grid_cols)` | Current baseline spatial bucket implementation |
| `TileGridStats` | Pickup/dropoff heat and free-driver side table |
| `TileRegionMap` | Offline constrained UF audit output |
| `CellIndex` | Proposed abstraction for multi-resolution spatial cells |
| H3 | Future adapter candidate, not current dependency |
| OSRM route polyline | Display-only geometry for live replay visualization |

## Update Rules

- Keep stable boundary decisions here.
- Keep volatile status in `../PROJECT_STATUS.md`.
- Keep task sequencing in `../plan/`.
- Do not duplicate module path tables from `../INDEX.md`.
- Do not let presentation notes become the first-pass architecture map.
