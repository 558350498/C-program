# Map Viewer

Minimal MapLibre MVP for the taxi dispatch project.

This version runs a local Vite + React + TypeScript dev server and renders a
MapLibre canvas. It first tries to load `public/data/tile_stats.geojson`; if
that file does not exist, it falls back to an inline sample GeoJSON layer.
The map also includes an optional online OpenStreetMap raster basemap for local
development, with a panel toggle to turn it off.

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
  -output-dir ..\..\web\map_viewer\public\data\replay `
  -batch-window-seconds 600
```

When replay artifacts exist, the viewer loads
`/data/replay/replay_manifest.json`.

- Batch mode also loads `/data/replay/replay_batches.json` and
  `/data/replay/replay_batch_tiles.json`. It shows a batch tick slider, play
  cursor, current pending requests, available drivers, candidate edges, applied
  assignments, cumulative assigned/completed counts, and a current-window tile
  activity overlay on the map.
- Live mode currently reports live path and point feature counts. The actual
  per-vehicle animation layer is intentionally left for the next UI step.

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

## Build

```powershell
npm.cmd run build
```
