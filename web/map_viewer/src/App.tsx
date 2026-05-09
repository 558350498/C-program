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

type HoverInfo = {
  title: string;
  details: string;
};

const dataUrl = "/data/tile_stats.geojson";
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
  const [dataMode, setDataMode] = useState<DataMode>("loading");
  const [featureCount, setFeatureCount] = useState(0);
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
        sources: {},
        layers: [
          {
            id: "background",
            type: "background",
            paint: {
              "background-color": "#edf2f4"
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

      map.addSource("sample-dispatch", {
        type: "geojson",
        data: dispatchGeoJson
      });

      map.addLayer({
        id: "sample-dispatch-fill",
        type: "fill",
        source: "sample-dispatch",
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
        source: "sample-dispatch",
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
        source: "sample-dispatch",
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

      map.fitBounds(geoJsonBounds(dispatchGeoJson) ?? sampleBounds, { padding: 72, duration: 0 });
    });

    map.on("mousemove", "sample-dispatch-fill", (event) => {
      const feature = event.features?.[0];
      if (feature) {
        setHoverInfo(describeFeature(feature));
      }
    });

    map.on("mousemove", "sample-dispatch-points", (event) => {
      const feature = event.features?.[0];
      if (feature) {
        setHoverInfo(describeFeature(feature));
      }
    });

    map.on("mouseleave", "sample-dispatch-fill", () => {
      setHoverInfo({
        title: "No feature selected",
        details: "Hover a tile"
      });
    });

    map.on("mouseleave", "sample-dispatch-points", () => {
      setHoverInfo({
        title: "No feature selected",
        details: "Hover a tile"
      });
    });

    return () => {
      map.remove();
      mapRef.current = null;
    };
  }, []);

  return (
    <main className="app-shell">
      <div ref={mapContainerRef} className="map-canvas" aria-label="Taxi dispatch map viewer" />
      <section className="status-panel" aria-label="Map status">
        <h1>Taxi Map Viewer</h1>
        <dl>
          <div>
            <dt>Map</dt>
            <dd>{mapReady ? "loaded" : "loading"}</dd>
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
