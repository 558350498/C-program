# Algorithm And Strategy

This document records stable non-ML dispatch and pricing rules. Current smoke
numbers belong in `PROJECT_STATUS.md`; terms belong in `docs/glossary.md`.

## Candidate Strategy

Default candidate generation is `scan + finite k`.

| Mode | Role |
|---|---|
| `scan` | Default baseline; simple and stable for the homework dataset |
| `indexed` | Correctness/performance comparison path for spatial-index work |
| `unlimited` | Stress/theory upper bound only |

Candidate edges are valid only when:

- the taxi is available
- the request is pending in the current batch
- the taxi/request pair passes radius and optional same-tile filters
- duplicate taxi/request edges are normalized to the lowest cost
- each request respects the configured top-k limit unless explicitly using the
  stress path

## Matching Strategy

Greedy and MCMF consume the same candidate edge set.

MCMF graph:

```text
source -> taxi -> request -> sink
```

The strategy is to maximize assignment count, then minimize total
`dispatch_cost`. The chosen assignments are applied through `TaxiSystem`, so
state mutation stays centralized.

## Cost Split

The project keeps two costs separate:

```text
pickup_cost   = replay timing fact
dispatch_cost = matching objective
```

Default:

```text
dispatch_cost == pickup_cost
```

Optional extension:

```text
base = route_cost_csv duration if present
       otherwise pickup_cost

opportunity_adjustment =
  cold_dropoff_penalty * cold_dropoff_score
  - hot_dropoff_discount * dropoff_hotspot_score

dispatch_cost =
  base + dispatch_opportunity_cost_scale * opportunity_adjustment
```

This affects greedy/MCMF matching only. It does not rewrite pickup arrival,
completion time, or `applied_pickup_cost`.

## Pricing V1

Pricing v1 is an explanation/reporting model. It can estimate fare, pickup
burden, hot/cold factors, and net value for sampled orders.

It should not affect matching unless a command explicitly feeds a pricing or
opportunity-cost factor into `dispatch_cost`.

## Calibration

Use `scripts/run_cost_grid_search.ps1` to rank opportunity-cost parameters.
Treat small 120-second smoke results as evidence that the path works, not as a
globally calibrated dispatch policy.

## Not In Scope

- learned demand forecasting
- driver acceptance or cancellation modeling
- dynamic surge pricing as a live service
- real-road ETA inside `pickup_cost`
- live HTTP routing inside replay
- region map as an implicit hard dispatch constraint
