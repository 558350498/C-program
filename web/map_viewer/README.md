# Map Viewer

Static MapLibre viewer placeholder.

This folder is reserved for the future web map layer:

- load GeoJSON exported from replay artifacts
- render tile heat and region audit layers
- keep dispatch, pricing, replay, and data conversion outside the frontend

First planned data flow:

```text
tools/geojson_export
  -> build-local/.../geojson/*.geojson
  -> web/map_viewer
```
