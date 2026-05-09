import { useEffect, useRef, useState } from "react";
import maplibregl, { type LngLatBoundsLike, type Map } from "maplibre-gl";

const sampleGeoJson: GeoJSON.FeatureCollection = {
  type: "FeatureCollection",
  features: [
    {
      type: "Feature",
      properties: {
        name: "Sample dispatch tile",
        kind: "polygon"
      },
      geometry: {
        type: "Polygon",
        coordinates: [
          [
            [-73.995, 40.715],
            [-73.93, 40.715],
            [-73.93, 40.765],
            [-73.995, 40.765],
            [-73.995, 40.715]
          ]
        ]
      }
    },
    {
      type: "Feature",
      properties: {
        name: "Sample pickup",
        kind: "point"
      },
      geometry: {
        type: "Point",
        coordinates: [-73.981743, 40.719158]
      }
    },
    {
      type: "Feature",
      properties: {
        name: "Sample dropoff",
        kind: "point"
      },
      geometry: {
        type: "Point",
        coordinates: [-73.938828, 40.829182]
      }
    }
  ]
};

type DataMode = "loading" | "geojson" | "fallback";
type WitnessMode = "loading" | "geojson" | "missing";

type HoverInfo = {
  title: string;
  details: string;
};

const dataUrl = "/data/tile_stats.geojson";
const witnessUrl = "/data/tile_corner_witnesses.geojson";
const basemapLayerId = "osm-basemap";
const dispatchSourceId = "sample-dispatch";
const witnessSourceId = "tile-corner-witnesses";
const selectedTileLayerId = "selected-tile-outline";
const witnessLayerId = "tile-corner-witness-points";
const sampleBounds: LngLatBoundsLike = [
  [-74.02, 40.7],
  [-73.9, 40.84]
];

async function loadTileStats(): Promise<GeoJSON.FeatureCollection> {
  const response = await fetch(dataUrl);
  if (!response.ok) {
    throw new Error(`tile_stats.geojson returned ${response.status}`);
  }
  const data = (await response.json()) as GeoJSON.FeatureCollection;
  if (data.type !== "FeatureCollection" || !Array.isArray(data.features)) {
    throw new Error("tile_stats.geojson is not a FeatureCollection");
  }
  return data;
}

async function loadWitnesses(): Promise<GeoJSON.FeatureCollection | null> {
  const response = await fetch(witnessUrl);
  if (response.status === 404) {
    return null;
  }
  if (!response.ok) {
    throw new Error(`tile_corner_witnesses.geojson returned ${response.status}`);
  }
  const data = (await response.json()) as GeoJSON.FeatureCollection;
  if (data.type !== "FeatureCollection" || !Array.isArray(data.features)) {
    throw new Error("tile_corner_witnesses.geojson is not a FeatureCollection");
  }
  return data;
}

function describeFeature(feature: maplibregl.MapGeoJSONFeature): HoverInfo {
  const properties = feature.properties ?? {};
  const tileId = properties.tile_id;
  if (tileId !== undefined) {
    return {
      title: `Tile ${tileId}`,
      details: [
        `pickup ${properties.pickup_count ?? 0}`,
        `dropoff ${properties.dropoff_count ?? 0}`,
        `drivers ${properties.available_driver_count ?? 0}`,
        `hot ${formatScore(properties.hotspot_score)}`,
        `cold ${formatScore(properties.cold_score)}`
      ].join(" | ")
    };
  }
  return {
    title: String(properties.name ?? "Sample feature"),
    details: String(properties.kind ?? "fallback")
  };
}

function witnessCountsByTile(data: GeoJSON.FeatureCollection | null): globalThis.Map<number, number> {
  const counts = new globalThis.Map<number, number>();
  if (!data) {
    return counts;
  }
  for (const feature of data.features) {
    const tileId = Number(feature.properties?.tile_id);
    if (Number.isFinite(tileId)) {
      counts.set(tileId, (counts.get(tileId) ?? 0) + 1);
    }
  }
  return counts;
}

function formatScore(value: unknown) {
  const numberValue = Number(value);
  if (!Number.isFinite(numberValue)) {
    return "0.00";
  }
  return numberValue.toFixed(2);
}

function geoJsonBounds(data: GeoJSON.FeatureCollection): LngLatBoundsLike | null {
  let west = Infinity;
  let south = Infinity;
  let east = -Infinity;
  let north = -Infinity;

  const visit = (coordinates: GeoJSON.Position) => {
    const [lon, lat] = coordinates;
    if (typeof lon !== "number" || typeof lat !== "number") {
      return;
    }
    west = Math.min(west, lon);
    south = Math.min(south, lat);
    east = Math.max(east, lon);
    north = Math.max(north, lat);
  };

  const walk = (coordinates: unknown): void => {
    if (!Array.isArray(coordinates)) {
      return;
    }
    if (typeof coordinates[0] === "number") {
      visit(coordinates as GeoJSON.Position);
      return;
    }
    for (const child of coordinates) {
      walk(child);
    }
  };

  const walkGeometry = (geometry: GeoJSON.Geometry): void => {
    if (geometry.type === "GeometryCollection") {
      for (const child of geometry.geometries) {
        walkGeometry(child);
      }
      return;
    }
    walk(geometry.coordinates);
  };

  for (const feature of data.features) {
    if (feature.geometry) {
      walkGeometry(feature.geometry);
    }
  }

  if (![west, south, east, north].every(Number.isFinite)) {
    return null;
  }
  return [
    [west, south],
    [east, north]
  ];
}

function App() {
  const mapContainerRef = useRef<HTMLDivElement | null>(null);
  const mapRef = useRef<Map | null>(null);
  const [mapReady, setMapReady] = useState(false);
  const [basemapEnabled, setBasemapEnabled] = useState(true);
  const [dataMode, setDataMode] = useState<DataMode>("loading");
  const [witnessMode, setWitnessMode] = useState<WitnessMode>("loading");
  const [featureCount, setFeatureCount] = useState(0);
  const [witnessCount, setWitnessCount] = useState(0);
  const [hoverInfo, setHoverInfo] = useState<HoverInfo>({
    title: "No feature selected",
    details: "Hover a tile"
  });

  useEffect(() => {
    if (!mapContainerRef.current || mapRef.current) {
      return;
    }

    const map = new maplibregl.Map({
      container: mapContainerRef.current,
      center: [-73.965, 40.745],
      zoom: 11,
      attributionControl: false,
      style: {
        version: 8,
        sources: {
          osm: {
            type: "raster",
            tiles: ["https://tile.openstreetmap.org/{z}/{x}/{y}.png"],
            tileSize: 256,
            attribution: "© OpenStreetMap contributors"
          }
        },
        layers: [
          {
            id: "background",
            type: "background",
            paint: {
              "background-color": "#edf2f4"
            }
          },
          {
            id: basemapLayerId,
            type: "raster",
            source: "osm",
            paint: {
              "raster-opacity": 0.72,
              "raster-saturation": -0.25
            }
          }
        ]
      }
    });

    mapRef.current = map;
    map.addControl(new maplibregl.NavigationControl({ visualizePitch: true }), "top-right");
    map.addControl(new maplibregl.AttributionControl({ compact: true }), "bottom-right");

    map.on("load", async () => {
      setMapReady(true);
      let dispatchGeoJson = sampleGeoJson;
      let sourceMode: DataMode = "fallback";

      try {
        dispatchGeoJson = await loadTileStats();
        sourceMode = "geojson";
      } catch (error) {
        console.warn("Falling back to inline sample GeoJSON.", error);
      }

      setDataMode(sourceMode);
      setFeatureCount(dispatchGeoJson.features.length);

      let witnessGeoJson: GeoJSON.FeatureCollection | null = null;
      try {
        witnessGeoJson = await loadWitnesses();
        setWitnessMode(witnessGeoJson ? "geojson" : "missing");
      } catch (error) {
        console.warn("Corner witness GeoJSON is unavailable.", error);
        setWitnessMode("missing");
      }
      const witnessCounts = witnessCountsByTile(witnessGeoJson);

      map.addSource(dispatchSourceId, {
        type: "geojson",
        data: dispatchGeoJson
      });
      map.addSource(witnessSourceId, {
        type: "geojson",
        data: witnessGeoJson ?? {
          type: "FeatureCollection",
          features: []
        }
      });

      map.addLayer({
        id: "sample-dispatch-fill",
        type: "fill",
        source: dispatchSourceId,
        filter: ["==", ["geometry-type"], "Polygon"],
        paint: {
          "fill-color": [
            "interpolate",
            ["linear"],
            ["coalesce", ["get", "hotspot_score"], 0],
            0,
            "#d6e6f2",
            0.4,
            "#84c5d4",
            0.7,
            "#f2b84b",
            1,
            "#d1495b"
          ],
          "fill-opacity": 0.64
        }
      });

      map.addLayer({
        id: "sample-dispatch-outline",
        type: "line",
        source: dispatchSourceId,
        filter: ["==", ["geometry-type"], "Polygon"],
        paint: {
          "line-color": "#355070",
          "line-opacity": 0.5,
          "line-width": 1
        }
      });

      map.addLayer({
        id: "sample-dispatch-points",
        type: "circle",
        source: dispatchSourceId,
        filter: ["==", ["geometry-type"], "Point"],
        paint: {
          "circle-color": [
            "match",
            ["get", "kind"],
            "point",
            "#d1495b",
            "#d1495b"
          ],
          "circle-radius": 7,
          "circle-stroke-color": "#ffffff",
          "circle-stroke-width": 2
        }
      });

      map.addLayer({
        id: selectedTileLayerId,
        type: "line",
        source: dispatchSourceId,
        filter: ["==", ["get", "tile_id"], -1],
        paint: {
          "line-color": "#0b1f33",
          "line-width": 3,
          "line-opacity": 0.95
        }
      });

      map.addLayer({
        id: witnessLayerId,
        type: "circle",
        source: witnessSourceId,
        filter: ["==", ["get", "tile_id"], -1],
        paint: {
          "circle-color": [
            "match",
            ["get", "corner"],
            "southwest",
            "#2f80ed",
            "southeast",
            "#00a676",
            "northeast",
            "#d1495b",
            "northwest",
            "#f2b84b",
            "#4f5d75"
          ],
          "circle-radius": 5.5,
          "circle-stroke-color": "#ffffff",
          "circle-stroke-width": 2
        }
      });

      map.fitBounds(geoJsonBounds(dispatchGeoJson) ?? sampleBounds, { padding: 72, duration: 0 });

      const showTileWitnesses = (feature: maplibregl.MapGeoJSONFeature) => {
        const tileId = Number(feature.properties?.tile_id);
        setHoverInfo(describeFeature(feature));
        if (!Number.isFinite(tileId)) {
          setWitnessCount(0);
          return;
        }
        const tileFilter: maplibregl.FilterSpecification = ["==", ["get", "tile_id"], tileId];
        map.setFilter(selectedTileLayerId, tileFilter);
        map.setFilter(witnessLayerId, tileFilter);
        setWitnessCount(witnessCounts.get(tileId) ?? 0);
      };

      const hideTileWitnesses = () => {
        const emptyFilter: maplibregl.FilterSpecification = ["==", ["get", "tile_id"], -1];
        map.setFilter(selectedTileLayerId, emptyFilter);
        map.setFilter(witnessLayerId, emptyFilter);
        setWitnessCount(0);
        setHoverInfo({
          title: "No feature selected",
          details: "Hover a tile"
        });
      };

      map.on("mousemove", "sample-dispatch-fill", (event) => {
        const feature = event.features?.[0];
        if (feature) {
          showTileWitnesses(feature);
        }
      });

      map.on("mousemove", "sample-dispatch-points", (event) => {
        const feature = event.features?.[0];
        if (feature) {
          setHoverInfo(describeFeature(feature));
        }
      });

      map.on("mouseleave", "sample-dispatch-fill", hideTileWitnesses);
      map.on("mouseleave", "sample-dispatch-points", () => {
        setHoverInfo({
          title: "No feature selected",
          details: "Hover a tile"
        });
      });
    });

    return () => {
      map.remove();
      mapRef.current = null;
    };
  }, []);

  useEffect(() => {
    const map = mapRef.current;
    if (!mapReady || !map?.getLayer(basemapLayerId)) {
      return;
    }
    map.setLayoutProperty(basemapLayerId, "visibility", basemapEnabled ? "visible" : "none");
  }, [basemapEnabled, mapReady]);

  return (
    <main className="app-shell">
      <div ref={mapContainerRef} className="map-canvas" aria-label="Taxi dispatch map viewer" />
      <section className="status-panel" aria-label="Map status">
        <h1>Taxi Map Viewer</h1>
        <label className="toggle-row">
          <input
            type="checkbox"
            checked={basemapEnabled}
            onChange={(event) => setBasemapEnabled(event.target.checked)}
          />
          <span>Online basemap</span>
        </label>
        <dl>
          <div>
            <dt>Map</dt>
            <dd>{mapReady ? "loaded" : "loading"}</dd>
          </div>
          <div>
            <dt>Basemap</dt>
            <dd>{basemapEnabled ? "online" : "off"}</dd>
          </div>
          <div>
            <dt>Data</dt>
            <dd>{dataMode}</dd>
          </div>
          <div>
            <dt>Features</dt>
            <dd>{featureCount}</dd>
          </div>
          <div>
            <dt>Witness</dt>
            <dd>
              <span>{witnessMode}</span>
              <small>{witnessCount} shown for hovered tile</small>
            </dd>
          </div>
          <div>
            <dt>Focus</dt>
            <dd>
              <span>{hoverInfo.title}</span>
              <small>{hoverInfo.details}</small>
            </dd>
          </div>
        </dl>
      </section>
    </main>
  );
}

export default App;
