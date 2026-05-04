# Go Experiment Summary

Reads a combined experiment `summary.csv` from `go_batch_experiments` and prints
a compact comparison plus one recommended row per sample size.

Example:

```powershell
go run . -input ..\..\build-local\perf-sweeps\summary.csv
```

Recommendation rule:

1. Keep rows with `completion_rate >= -min-completion-rate`.
2. Prefer fewer `candidate_edges`.
3. Then prefer lower `replay_ms`.
4. Then prefer lower `avg_pickup_cost`.

The compact table includes completion, candidate edge count, replay time, and
hot/cold dropoff rates.

