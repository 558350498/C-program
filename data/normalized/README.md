# Normalized Replay Data

Go preprocessing tools should write normalized files here:

```text
data/normalized/requests.csv
data/normalized/drivers.csv
```

Generated normalized CSV files are ignored by Git. The C++ replay layer should
read from this directory after preprocessing.

## Schema

`requests.csv`:

```csv
request_id,customer_id,request_time,pickup_x,pickup_y,dropoff_x,dropoff_y,pickup_tile,dropoff_tile
101,1001,10,3.0,4.0,10.0,0.0,1,2
```

`drivers.csv`:

```csv
taxi_id,x,y,tile,available_time,status
1,0.0,0.0,1,0,free
```

Notes:

- `status` is optional for drivers and defaults to `free`.
- Supported driver statuses are `free`, `occupy` / `occupied`, and `offline`.
- CSV loading code lives in `include/dispatch_replay_io.h` and
  `src/dispatch_replay_io.cpp`.
