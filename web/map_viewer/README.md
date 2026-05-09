# Map Viewer

Minimal MapLibre MVP for the taxi dispatch project.

This version runs a local Vite + React + TypeScript dev server and renders a
MapLibre canvas. It first tries to load `public/data/tile_stats.geojson`; if
that file does not exist, it falls back to an inline sample GeoJSON layer.

The viewer stays a static frontend. It does not call replay, dispatch, pricing,
Docker, WebSocket, Redis, or a backend API.

## Generate tile GeoJSON

From the repository root:

```powershell
cd tools\geojson_export
go run . `
  -tile-stats ..\..\build-local\perf-sweeps-grid-sweep-smoke\normalized\grid_200\limit_1000\tile_stats.csv `
  -tile-grid-cols 200 `
  -output-dir ..\..\web\map_viewer\public\data
```

This writes:

```text
web/map_viewer/public/data/tile_stats.geojson
```

Vite serves that file at:

```text
http://localhost:5173/data/tile_stats.geojson
```

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
