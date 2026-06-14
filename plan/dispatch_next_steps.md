# Dispatch Next Steps

This file records the current executable plan only. For broad project state, read `../PROJECT_STATUS.md`; for module navigation, read `../INDEX.md`.

## Current Health Summary

The dispatch/replay core is in reasonable shape:

- C++ core modules are centralized behind `taxi_core`.
- Tests cover the important replay and matching interfaces.
- Go tools are separated by workflow role.
- Map Viewer remains a static artifact reader.
- Docs now use progressive disclosure instead of one large mixed index.

The main architectural gap is no longer “can the system replay and show a demo?” It is now:

```text
Can the project make spatial candidate generation deeper
without breaking replay comparability?
```

## Active Slice 1: Lightweight Project Doctor

Goal: catch stale entry-map paths early.

Suggested artifact:

```text
tools/project_doctor.*
```

Minimum checks:

- `README.md`, `PROJECT_STATUS.md`, `INDEX.md`, `docs/README.md`, and `plan/README.md` exist.
- Paths listed in the main navigation tables exist.
- `index_.md` remains only a legacy shim.
- `docs/README.md` points to stable docs only.
- `plan/dispatch_next_steps.md` stays short enough to be a plan, not a history dump.

This mirrors the useful part of the novel project approach: make entry drift machine-visible before it becomes architecture debt.

## Active Slice 2: `CellIndex` Design

Goal: prepare the H3-like path without directly replacing the current CSV/replay baseline.

Proposed interface shape:

```text
CellIndex
  encode(lon, lat) -> cell_id
  neighbors(cell_id) -> cell_id list
  boundary(cell_id) -> polygon / bbox
  parent(cell_id, resolution) -> cell_id
```

First adapter:

```text
SimpleTileCellIndex
```

It should wrap the current `simpleTile(grid_cols)` semantics so existing normalized CSV and multi-resolution experiments remain comparable.

Second adapter, only after the seam is tested:

```text
H3CellIndex
```

## Active Slice 3: Candidate Generation Boundary

Goal: avoid spreading spatial-grid decisions through dispatch logic.

Current rule:

- `scan + finite k` stays default.
- `indexed` stays comparison path.
- `unlimited` stays stress path.

Next useful design step:

```text
CandidateEdgeGenerator
  uses spatial query / cell bucket / side table
  emits normalized CandidateEdge list
```

Do not implement this until `CellIndex` has a small tested adapter. Otherwise the project will couple candidate generation to another temporary grid shape.

## Active Slice 4: Region Audit Viewer

Goal: expose `region_stats.csv` and `region_map.csv` as an audit layer without changing dispatch.

Preferred route:

```text
region_map.csv + region_stats.csv
-> region GeoJSON export
-> Map Viewer layer toggle
```

Boundaries:

- Region is an explanation/audit layer.
- Region is not a hard dispatch boundary.
- Region is not MCMF cost.
- Region is not dynamically redrawn every batch.

## Hold

Do not spend the next slice on:

- Full H3 replacement before `CellIndex` exists.
- Real-road ETA inside dispatch.
- Redis / WebSocket / online location services.
- Order CRUD or admin UI.
- More presentation-first material.
- Region as hard dispatch boundary.
- Pricing v1 inside MCMF cost.

## Verification Commands

C++ core:

```powershell
cmake -S . -B build-mingw -G "MinGW Makefiles"
cmake --build build-mingw
ctest --test-dir build-mingw --output-on-failure
```

Go tool examples:

```powershell
cd tools\replay_visual_export
go test ./...

cd ..\route_visual_export
go test ./...
```

Viewer:

```powershell
cd web\map_viewer
npm run dev
```
