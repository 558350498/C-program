# Region And Cell Design

This document defines the spatial-statistics boundary. Dispatch and cost terms
are defined in `docs/glossary.md`.

## Spatial Layers

| Layer | Role |
|---|---|
| Point | Raw taxi/request coordinates |
| Tile/cell | Lightweight spatial bucket for stats and optional cost side tables |
| Region | Slow-changing audit/explanation grouping above cells |
| Route | Road-network geometry or duration from offline routing tools |

Only tile/cell stats and explicit route-cost CSV may feed matching. Display
geometry and region colors do not define dispatch facts.

## Current Cell Path

`simpleTile(grid_cols)` remains the baseline. `SimpleTileCellIndex` wraps that
baseline behind the `CellIndex` interface:

```text
encode(lon, lat) -> cell_id
neighbors(cell_id) -> nearby cell ids
boundary(cell_id) -> display/audit geometry
parent(cell_id, resolution) -> coarser cell id
```

The current production path:

```text
--cell-stats-grid-cols N
-> SimpleTileCellIndex.encode()
-> TileGridStats
-> optional neighbor/parent smoothing
-> TileDispatchCostModel
-> CandidateEdge.dispatch_cost
```

## Smoothing Contract

Neighbor smoothing and parent fallback only produce hotspot/cold-score side
tables.

They do not:

- move request or driver coordinates
- change `pickup_cost`
- change replay event timing
- convert region map into a dispatch boundary

## Region Map Contract

`TileRegionMap` is an offline constrained-union audit tool. It can help inspect
whether cell grouping is spatially reasonable, but it should not silently alter
candidate generation or matching.

Future region work should stay behind explicit options and evidence scripts.

## H3-Like Direction

H3 is a future adapter candidate, not a current dependency. If introduced, it
should enter as a second `CellIndex` implementation after the tile-backed path
is proven.

Do not replace replay, cost semantics, or viewer artifacts directly with H3.
