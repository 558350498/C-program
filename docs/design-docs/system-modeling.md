# System Modeling

This document keeps the stable architecture boundaries. File/module lookup
lives in `../index.md`; current status lives in
`../exec-plans/active/project-status.md`; terminology lives in `glossary.md`.

## Layer Boundaries

| Layer | Owns | Must not own |
|---|---|---|
| Go preprocess | Raw CSV drift, field cleanup, normalized request/driver CSVs | C++ replay state |
| C++ core | Request lifecycle, taxi state, candidate edges, greedy/MCMF matching, replay metrics | Kaggle raw schema, viewer state |
| Go experiment/export tools | Batch sweeps, summaries, GeoJSON, replay artifacts, route-cost CSV generation | Online dispatch behavior |
| Viewer | Static explanation of replay artifacts | Replay execution, assignment writes, cost calculation |
| Docs/scripts | Reproducible evidence and handoff guidance | Generated build/report outputs |

Main data flow:

```text
raw CSV
-> tools/go_csv_preprocess
-> data/normalized/requests.csv + drivers.csv
-> replay_csv_demo / k_sweep
-> batch logs + request outcomes + summaries
-> export tools
-> web/map_viewer static artifacts
```

## Dispatch Facts

C++ replay is the source of truth for:

- request arrival, dispatch, pickup, completion, and unserved outcomes
- taxi occupied/free transitions
- `pickup_cost`, wait time, and completion timing
- greedy and MCMF comparison metrics
- per-request outcome rows used by reports and viewer exports

Route artifacts, raster maps, sampled-order explanations, and front-end
highlighting explain these facts. They do not redefine them.

## Candidate And Matching Boundary

Candidate generation builds taxi/request edges from available drivers and
pending requests. The default path is still `scan + finite k`; indexed paths are
comparison and future optimization paths.

MCMF optimizes `dispatch_cost`. Replay timing applies `pickup_cost`. See
`glossary.md` for the cost split.

Do not silently add:

- all-pairs city-scale bipartite graphs
- live routing calls inside replay
- unbounded candidates as a default experiment
- region map as a hard dispatch boundary

## Spatial Boundary

`simpleTile(grid_cols)` is the current baseline spatial bucket. `CellIndex` is
the abstraction for future multi-resolution cells.

Allowed spatial inputs to matching:

- `--cell-stats-grid-cols` to re-encode stats through `SimpleTileCellIndex`
- neighbor smoothing and parent fallback to adjust hotspot scores
- explicit `--route-cost-csv` to feed route duration into `dispatch_cost`

Not allowed:

- route ETA rewriting `pickup_cost`
- OSM/MapLibre display layers defining region facts
- H3 replacing replay directly without an adapter path

## Viewer Boundary

`web/map_viewer` is a static explanation surface. It can load JSON/GeoJSON,
animate replay artifacts, display route polylines, and show sampled-order
explanations.

It must not:

- run replay
- dispatch orders
- write request outcome or batch log files
- call routing services to mutate matching inputs
- present display paths as dispatch facts
