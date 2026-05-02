# Go CSV Preprocess

This tool reads the raw Kaggle taxi trip CSV and writes normalized replay
inputs:

- `requests.csv`
- `drivers.csv`

The current driver generation is intentionally synthetic. For every N valid
requests, it creates one free driver near the request pickup point. The default
is `--driver-every 2`, which means roughly 0.5 driver per request and creates a
supply-short replay scenario.

The raw Kaggle rows are not assumed to be time-sorted. The tool first collects
the valid sampled requests, chooses the earliest pickup time as the default
base time, and writes `requests.csv` sorted by relative `request_time`.

## Usage

```powershell
go run . `
  -input ..\..\data\datasets\nyc-taxi-trip-duration\raw\NYC.csv `
  -output ..\..\data\normalized\requests.csv `
  -drivers-output ..\..\data\normalized\drivers.csv `
  -limit 1000
```

## Important Options

- `-limit`: maximum valid requests to write.
- `-driver-every`: synthesize one driver for every N valid requests.
- `-driver-radius`: random offset radius around pickup point, in lon/lat
  degrees.
- `-seed`: random seed for reproducible synthesized drivers.
- `-base-time`: optional base time. If omitted, the first valid pickup time is
  used as `request_time = 0`.

The C++ replay layer reads only the normalized files and does not depend on
Kaggle raw column names.
