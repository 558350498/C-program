# Map Viewer

Minimal MapLibre MVP for the taxi dispatch project.

This version runs a local Vite + React + TypeScript dev server and renders a
MapLibre canvas. It first tries to load `public/data/tile_stats.geojson`; if
that file does not exist, it falls back to an inline sample GeoJSON layer.
The map also includes an optional online OpenStreetMap raster basemap for local
development, with a panel toggle to turn it off.
The status panel also has separate layer toggles for tile polygons, tile points,
and corner witnesses, so demos can explain each visual layer without leaving a
solid rectangle on the map all the time.

The viewer stays a static frontend. It does not call replay, dispatch, pricing,
Docker, WebSocket, Redis, or a backend API.
The online basemap is a visual reference layer only; it is not used by replay or
dispatch logic.

When `public/data/tile_corner_witnesses.geojson` exists, hovering a tile shows
the pickup orders nearest to that tile's four corners. These points explain why
a coarse rectangle exists even when part of the rectangle covers water or other
non-road areas.

## Generate tile GeoJSON

From the repository root:

```powershell
cd tools\geojson_export
go run . `
  -tile-stats ..\..\build-local\perf-sweeps-grid-sweep-smoke\normalized\grid_200\limit_1000\tile_stats.csv `
  -requests ..\..\build-local\perf-sweeps-grid-sweep-smoke\normalized\grid_200\limit_1000\requests.csv `
  -tile-grid-cols 200 `
  -output-dir ..\..\web\map_viewer\public\data
```

This writes:

```text
web/map_viewer/public/data/tile_stats.geojson
web/map_viewer/public/data/tile_corner_witnesses.geojson
```

Vite serves that file at:

```text
http://localhost:5173/data/tile_stats.geojson
```

## Generate replay artifacts

`tools/replay_visual_export` consumes existing replay CSV outputs. It does not
run dispatch again.

Auto mode uses `1000` requests as the default boundary:

- `<= 1000`: live mode, writes `replay_live_paths.geojson` and
  `replay_live_points.geojson`.
- `> 1000`: batch mode, writes `replay_batches.json` and
  `replay_batch_tiles.json`.

Small live sample:

```powershell
cd tools\replay_visual_export
go run . `
  -requests ..\..\build-local\perf-sweeps-grid-sweep-smoke\normalized\grid_200\limit_1000\requests.csv `
  -drivers ..\..\build-local\perf-sweeps-grid-sweep-smoke\normalized\grid_200\limit_1000\drivers.csv `
  -request-outcomes ..\..\build-local\map-viewer-replay-1000\request_outcomes.csv `
  -batch-logs ..\..\build-local\map-viewer-replay-1000\batch_logs.csv `
  -tile-stats ..\..\build-local\perf-sweeps-grid-sweep-smoke\normalized\grid_200\limit_1000\tile_stats.csv `
  -sample-order-count 12 `
  -sample-seed 20260510 `
  -pickup-hot-weight 0.15 `
  -cold-dropoff-penalty 0.20 `
  -hot-dropoff-discount 0.10 `
  -output-dir ..\..\web\map_viewer\public\data\replay
```

Large batch sample:

```powershell
cd tools\replay_visual_export
go run . `
  -requests ..\..\build-local\perf-sweeps-20k-week-k1\normalized\limit_20000\requests.csv `
  -drivers ..\..\build-local\perf-sweeps-20k-week-k1\normalized\limit_20000\drivers.csv `
  -request-outcomes ..\..\build-local\map-viewer-replay-20k\request_outcomes.csv `
  -batch-logs ..\..\build-local\map-viewer-replay-20k\batch_logs.csv `
  -sample-order-count 12 `
  -output-dir ..\..\web\map_viewer\public\data\replay `
  -batch-window-seconds 600
```

When replay artifacts exist, the viewer loads
`/data/replay/replay_manifest.json`.

- The Replay panel probes live and batch artifacts independently. If both are
  present, the viewer can switch between them; if one is missing, that option is
  disabled rather than treated as a fatal replay error.
- Batch mode also loads `/data/replay/replay_batches.json` and
  `/data/replay/replay_batch_tiles.json`. It shows a batch tick slider, play
  cursor, current pending requests, available drivers, candidate edges, applied
  assignments, cumulative assigned/completed counts, and a current-window tile
  activity overlay on the map.
- Live mode loads `/data/replay/replay_live_paths.geojson` and
  `/data/replay/replay_live_points.geojson`. It shows a time slider, play
  cursor, active virtual-walk paths, interpolated taxi dots, and recent
  pickup/dropoff event points.
- When `/data/replay/replay_live_routes.geojson` exists, live mode uses it
  instead of `replay_live_paths.geojson` for path drawing and taxi interpolation.
  Missing or failed routes fall back to the original virtual-walk line.
- When `/data/replay/sampled_order_explanations.json` exists, the Replay panel
  exposes an Orders drawer. Selecting one highlights its route when live
  route/path artifacts are available, plus pickup/dropoff points and related
  tiles. The sample file is display-only and does not change dispatch results.
  When generated with `-tile-stats`, each sample also includes an explanatory
  pricing estimate with base fare, price factor, pickup cost, and estimated net.
  The drawer groups the explanation into Lifecycle, Dispatch, and Pricing.

If you want live and batch available at the same time, keep both file families
under `public/data/replay`. `tools/replay_visual_export -mode live` and
`-mode batch` are still single-mode exports, so exporting one mode directly into
the viewer directory can remove files from the other mode. A safe workflow is to
export the second mode into a temporary build directory and copy only the needed
`replay_batches.json` / `replay_batch_tiles.json` files into
`public/data/replay`.

## Generate live route GeoJSON

`tools/route_visual_export` consumes `replay_live_paths.geojson` and writes
`replay_live_routes.geojson`. It expects a local OSRM-compatible router by
default:

```text
http://127.0.0.1:5000
```

Current local validation:

- Docker container: `osrm-nyc`
- Router URL: `http://127.0.0.1:5000`
- Sample live paths: `1998`
- Routed features: `1998`
- Fallback features: `0`

Start an existing local OSRM container:

```powershell
docker start osrm-nyc
```

Stop it when not needed:

```powershell
docker stop osrm-nyc
```

Generate route artifacts:

```powershell
cd tools\route_visual_export
go run . `
  -input-live-paths ..\..\web\map_viewer\public\data\replay\replay_live_paths.geojson `
  -output ..\..\web\map_viewer\public\data\replay\replay_live_routes.geojson `
  -router-url http://127.0.0.1:5000
```

If the router is unavailable, the tool still writes an output file. Each failed
route keeps the original LineString and gets `route_status=fallback`.

The route artifact is display-only. It does not change replay times, dispatch
decisions, MCMF cost, pickup cost, completion rate, or request outcomes.

## Local development

Use `npm.cmd` on Windows PowerShell so execution policy does not try to run
`npm.ps1`.

```powershell
npm.cmd install
npm.cmd run dev -- --host 127.0.0.1 --port 5173
```

Open:

```text
http://localhost:5173
```

## Demo on another machine

The repository can come from GitHub. Demo data is intentionally separate because
`web/map_viewer/public/data/` and generated replay artifacts are ignored build
outputs.

Recommended workflow for a notebook:

1. `git pull` the repository.
2. Put demo artifacts in a separate folder, for example `D:\taxi-demo-artifacts`.
   The folder can either contain `tile_stats.geojson` plus a `replay\` subfolder,
   or all replay files directly at the root.
3. Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\prepare_map_viewer_demo.ps1 -ArtifactSource D:\taxi-demo-artifacts -Serve
```

If the notebook already has the artifacts under `web/map_viewer/public/data`,
run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\prepare_map_viewer_demo.ps1 -Serve
```

The script does not package source code. It only syncs optional demo artifacts,
checks missing layers, runs the frontend build, and starts the local viewer.

## Build

```powershell
npm.cmd run build
```
