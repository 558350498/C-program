# Timeline Model

This document defines the replay event contract. Cost terminology is defined in
`docs/glossary.md`; module locations are listed in `INDEX.md`.

## Events

The replay uses four core event kinds:

| Event | Effect |
|---|---|
| `request_arrival` | Creates a pending request context |
| `batch_dispatch` | Collects pending requests/free drivers and runs matching |
| `pickup_arrival` | Moves an assigned request into serving |
| `trip_complete` | Completes the request and frees the taxi at dropoff |

If multiple events share a timestamp, process them in this order:

1. `trip_complete`
2. `request_arrival`
3. `batch_dispatch`
4. `pickup_arrival`

This lets just-completed taxis participate in the current batch while keeping
new assignments from immediately re-entering the same batch.

## Request Lifecycle

Successful request:

```text
pending -> dispatched -> serving -> completed
```

Unmatched request:

```text
pending -> pending in later batches
```

The replay does not currently cancel old pending requests by timeout.

## Batch Dispatch

At each batch boundary:

1. Select requests with `request_time <= t` and `status == pending`.
2. Select drivers with `status == free` and `available_time <= t`.
3. Build `BatchDispatchInput`.
4. Generate candidate edges.
5. Run greedy and MCMF on the same candidate set.
6. Apply the selected MCMF assignments through `TaxiSystem::apply_assignment()`.
7. Schedule pickup and completion events from `assignment.pickup_cost`.

The batch log records candidate coverage, greedy/MCMF costs, assignment counts,
applied pickup cost, and timing breakdowns.

## Cost Timing Contract

- `pickup_cost` schedules `pickup_arrival`.
- `trip_duration_seconds` schedules `trip_complete` after pickup.
- `dispatch_cost` chooses the assignment but does not schedule replay time.
- Route-cost CSV and opportunity cost may change `dispatch_cost`; they do not
  alter already-defined replay timing.

## Output Contract

Replay outputs may include:

- summary metrics
- batch logs
- per-request outcomes
- candidate route pairs
- request lifecycle timestamps
- pickup/dropoff coordinates and tile/cell fields used by exporters

Exporters may transform these rows into viewer artifacts, but they must not
modify the replay event order or request outcomes.
