# Go Batch Experiments

Runs repeated preprocessing and replay experiments across several request
limits, then writes one combined CSV.

Default workflow:

```text
raw NYC.csv
  -> go_csv_preprocess per limit
  -> go_experiments scan/indexed
  -> build-local/perf-sweeps/summary.csv
```

Example:

```powershell
go run . `
  -limits 1000,5000,20000 `
  -radii 0.01,0.03,0.05 `
  -k-values 1,2,5,unlimited
```

For a quick smoke run:

```powershell
go run . -limits 1000 -radii 0.03 -k-values unlimited
```

The output directory defaults to `../../build-local/perf-sweeps`, so generated
CSV files and normalized intermediate inputs stay out of version control.

After a run, summarize the combined CSV with:

```powershell
cd ..\go_experiment_summary
go run . -input ..\..\build-local\perf-sweeps\summary.csv
```

The combined CSV inherits the `go_experiments` output columns, including the
lightweight tile hotspot statistics:

- `avg_pickup_hotspot_score`
- `avg_dropoff_hotspot_score`
- `avg_cold_dropoff_score`
- `hot_pickup_requests`
- `hot_dropoff_requests`
- `cold_dropoff_requests`
- `hot_dropoff_rate`
- `cold_dropoff_rate`
