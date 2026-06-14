# Architecture

This is the top-level architecture summary. Load deeper docs from
`docs/design-docs/` only when the task needs them.

## System Kernel

```text
raw taxi CSV
-> Go preprocess
-> normalized requests/drivers CSV
-> C++ replay + candidate generation + MCMF
-> metrics, batch logs, request outcomes
-> static export tools
-> MapLibre viewer
```

## Layer Ownership

| Layer | Owns |
|---|---|
| Go preprocess | Raw dataset drift, CSV cleanup, normalized input generation |
| C++ core | Replay timing, taxi/request state, candidate edges, greedy/MCMF, metrics |
| Go export tools | Experiment sweeps, summaries, GeoJSON, replay artifacts, route-cost CSV |
| Viewer | Static explanation of replay artifacts |
| Docs/scripts | Reproducible evidence, handoff maps, pre-submit checks |

## Non-Negotiable Boundaries

- `pickup_cost` is replay timing, not road ETA.
- `dispatch_cost` is the matching objective.
- Route costs enter matching only through explicit route-cost CSV input.
- The viewer never dispatches orders or writes replay facts.
- `CellIndex` is the spatial abstraction boundary; H3-like work must enter
  through an adapter, not by replacing replay.

## Deep Dives

| Topic | File |
|---|---|
| Full documentation map | `docs/index.md` |
| System boundaries | `docs/design-docs/system-modeling.md` |
| Replay timeline | `docs/design-docs/timeline-model.md` |
| Dispatch/pricing strategy | `docs/design-docs/algorithm-and-strategy.md` |
| Region and CellIndex design | `docs/design-docs/region-and-cell-design.md` |
| Current state | `docs/exec-plans/active/project-status.md` |

## Enforcement

Architecture drift is checked by:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\architecture_lint.ps1
```

The lint is intentionally lightweight: it enforces the UI/static boundary, C++
dependency boundary, route-cost boundary, cost semantics, and docs structure.
