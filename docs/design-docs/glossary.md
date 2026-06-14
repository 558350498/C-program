# Glossary

This glossary keeps dispatch, pricing, cost, and spatial terms consistent
across code, docs, reports, and agent handoffs.

## Cost And Dispatch Terms

| Term | Meaning | Current source |
|---|---|---|
| `pickup_cost` | Replay timeline fact for taxi-to-pickup travel time. It schedules pickup arrival, wait time, completion timing, and applied pickup totals. It is still based on the replay cost model, not live road ETA. | `include/dispatch_batch.h`, `src/dispatch_replay.cpp` |
| `dispatch_cost` | Matching objective used by greedy and MCMF. It starts as `pickup_cost`, then may include explicit route-cost and opportunity-cost inputs. | `include/dispatch_batch.h`, `src/mcmf_batch_strategy.cpp` |
| `mcmf_cost` | Sum of selected assignment `dispatch_cost` values in the MCMF matcher. It is not necessarily the same as applied pickup time. | `src/dispatch_replay.cpp` |
| `applied_pickup_cost` | Sum of selected assignment `pickup_cost` values after replay applies assignments. It remains tied to replay timing. | `src/dispatch_replay.cpp` |
| Route cost | Offline road-network duration/distance from route-cost CSV. It can become a `dispatch_cost` side table when `--route-cost-csv` is provided. | `tools/route_visual_export/`, `src/dispatch_replay_io.cpp` |
| Opportunity cost | Hot/cold-zone adjustment that nudges matching away from worse future-positioning outcomes. In the current model it is a rule-based adjustment, not a learned future supply-demand forecast. | `include/dispatch_batch.h`, `scripts/run_cost_grid_search.ps1` |
| Pricing v1 | Explanatory fare/revenue estimate for reports and sample orders. It does not affect matching unless an explicit dispatch-cost option is enabled. | `algorithm-and-strategy.md` |

## Spatial Terms

| Term | Meaning | Current source |
|---|---|---|
| `simpleTile(grid_cols)` | Rectangular grid baseline used for current spatial buckets and comparable sweeps. | `include/tile_grid_stats.h` |
| `CellIndex` | Spatial abstraction with encode, neighbor, boundary, and parent operations. It exists so tile semantics can later be replaced or extended by H3-like cells. | `include/cell_index.h` |
| `SimpleTileCellIndex` | First `CellIndex` adapter that wraps existing rectangular tile semantics. | `src/cell_index.cpp` |
| Neighbor smoothing | Optional hotspot-score smoothing over nearby cells. It affects opportunity adjustment, not coordinates or replay timing. | `include/tile_grid_stats.h` |
| Parent fallback | Optional coarse-cell fallback for sparse cells. It affects opportunity adjustment, not candidate geometry or replay timing. | `include/tile_grid_stats.h` |
| H3-like adapter | Future multi-resolution cell implementation candidate. It is not the current dependency and should not replace replay directly. | `region-and-cell-design.md` |

## Boundary Rules

- Route geometry and OSM basemaps explain replay artifacts; they do not define
  replay facts.
- Real-road route duration can enter matching only through explicit route-cost
  CSV input.
- `pickup_cost` must stay separate from `dispatch_cost`.
- Region, heat, and cell statistics are side-table evidence unless an explicit
  option feeds them into dispatch matching.
