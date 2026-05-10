import { useEffect, useMemo, useRef, useState } from "react";
import maplibregl, { type GeoJSONSource, type LngLatBoundsLike, type Map } from "maplibre-gl";

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
type ReplayDataMode = "loading" | "missing" | "live" | "batch" | "error";
type ReplayArtifactMode = "loading" | "geojson" | "missing" | "error";

type HoverInfo = {
  title: string;
  details: string;
};

type ReplayManifest = {
  schema_version: number;
  mode: "live" | "batch";
  requested_mode: string;
  live_threshold: number;
  request_count: number;
  driver_count: number;
  outcome_count: number;
  batch_count: number;
  assigned_count: number;
  completed_count: number;
  generated_files: string[];
};

type ReplayBatch = {
  batch_time: number;
  available_drivers: number;
  pending_requests: number;
  candidate_edges: number;
  applied_assignments: number;
  assigned_cumulative: number;
  completed_cumulative: number;
};

type ReplayLiveSummary = {
  pathCount: number;
  pointCount: number;
  routeSource: "routes" | "paths";
  routeCount: number;
  routedCount: number;
  fallbackCount: number;
  startTime: number;
  endTime: number;
};

type ReplayTileMode = "loading" | "geojson" | "missing" | "error";
type SampleOrdersMode = "loading" | "geojson" | "missing" | "error";

type ReplayBatchTileActivity = {
  tile_id: number;
  pickup_count: number;
  assigned_count: number;
  completed_count: number;
  activity_score: number;
};

type ReplayBatchTileTotals = {
  active_tiles: number;
  pickup_count: number;
  assigned_count: number;
  completed_count: number;
  activity_score: number;
};

type ReplayBatchTileFrame = {
  batch_time: number;
  window_start_time: number;
  window_seconds: number;
  tiles: ReplayBatchTileActivity[];
  totals: ReplayBatchTileTotals;
};

type SampleOrderPoint = {
  x: number;
  y: number;
  tile: string;
};

type SampleOrderPricing = {
  mode: string;
  base_revenue: number;
  price_factor: number;
  pickup_hotspot_component: number | null;
  cold_dropoff_component: number | null;
  hot_dropoff_component: number | null;
  estimated_revenue: number;
  estimated_pickup_km: number;
  estimated_pickup_cost: number;
  estimated_net: number;
};

type SampleOrderExplanation = {
  request_id: string;
  taxi_id: string;
  status: "completed" | "assigned_incomplete" | "unserved";
  reason_tags: string[];
  request_time: number;
  assignment_time: number;
  pickup_time: number;
  completion_time: number;
  wait_time: number;
  pickup_cost: number;
  pending_batch_count: number;
  candidate_batch_count: number;
  candidate_edge_count: number;
  has_candidate_edge: boolean;
  pickup: SampleOrderPoint;
  dropoff: SampleOrderPoint;
  trip_distance: number;
  pickup_hotspot_score: number | null;
  pickup_cold_score: number | null;
  dropoff_hotspot_score: number | null;
  dropoff_cold_score: number | null;
  opportunity_adjustment: number | null;
  pricing?: SampleOrderPricing;
};

const dataUrl = "/data/tile_stats.geojson";
const witnessUrl = "/data/tile_corner_witnesses.geojson";
const replayManifestUrl = "/data/replay/replay_manifest.json";
const replayBatchesUrl = "/data/replay/replay_batches.json";
const replayBatchTilesUrl = "/data/replay/replay_batch_tiles.json";
const replayLivePathsUrl = "/data/replay/replay_live_paths.geojson";
const replayLiveRoutesUrl = "/data/replay/replay_live_routes.geojson";
const replayLivePointsUrl = "/data/replay/replay_live_points.geojson";
const sampleOrdersUrl = "/data/replay/sampled_order_explanations.json";
const basemapLayerId = "osm-basemap";
const dispatchSourceId = "sample-dispatch";
const witnessSourceId = "tile-corner-witnesses";
const dispatchFillLayerId = "sample-dispatch-fill";
const dispatchOutlineLayerId = "sample-dispatch-outline";
const dispatchPointLayerId = "sample-dispatch-points";
const replayActivitySourceId = "replay-batch-activity";
const replayActivityFillLayerId = "replay-batch-activity-fill";
const replayActivityOutlineLayerId = "replay-batch-activity-outline";
const livePathSourceId = "replay-live-paths";
const liveTaxiSourceId = "replay-live-taxis";
const liveEventSourceId = "replay-live-events";
const selectedSamplePathSourceId = "selected-sample-paths";
const selectedSamplePointSourceId = "selected-sample-points";
const livePathLayerId = "replay-live-active-paths";
const liveTaxiLayerId = "replay-live-active-taxis";
const liveEventLayerId = "replay-live-recent-events";
const selectedSamplePathLayerId = "selected-sample-paths";
const selectedSamplePointLayerId = "selected-sample-points";
const selectedSampleTileLayerId = "selected-sample-tile-outline";
const selectedTileLayerId = "selected-tile-outline";
const witnessLayerId = "tile-corner-witness-points";
const sampleBounds: LngLatBoundsLike = [
  [-74.02, 40.7],
  [-73.9, 40.84]
];
const emptyFeatureCollection: GeoJSON.FeatureCollection = {
  type: "FeatureCollection",
  features: []
};
const emptyReplayTotals: ReplayBatchTileTotals = {
  active_tiles: 0,
  pickup_count: 0,
  assigned_count: 0,
  completed_count: 0,
  activity_score: 0
};

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

async function loadReplayManifest(): Promise<ReplayManifest | null> {
  const response = await fetch(replayManifestUrl);
  if (response.status === 404) {
    return null;
  }
  if (!response.ok) {
    throw new Error(`replay_manifest.json returned ${response.status}`);
  }
  const data = (await response.json()) as ReplayManifest;
  if (data.mode !== "live" && data.mode !== "batch") {
    throw new Error("replay_manifest.json has an invalid mode");
  }
  return data;
}

async function loadReplayBatches(): Promise<ReplayBatch[]> {
  const response = await fetch(replayBatchesUrl);
  if (response.status === 404) {
    return [];
  }
  if (!response.ok) {
    throw new Error(`replay_batches.json returned ${response.status}`);
  }
  const data = (await response.json()) as ReplayBatch[];
  if (!Array.isArray(data)) {
    throw new Error("replay_batches.json is not an array");
  }
  return data;
}

type LiveReplayData = {
  paths: GeoJSON.FeatureCollection;
  points: GeoJSON.FeatureCollection;
  summary: ReplayLiveSummary;
};

async function loadLiveReplay(): Promise<LiveReplayData | null> {
  const [pathsResponse, pointsResponse] = await Promise.all([
    fetch(replayLivePathsUrl),
    fetch(replayLivePointsUrl)
  ]);
  if (pathsResponse.status === 404 || pointsResponse.status === 404) {
    return null;
  }
  if (!pathsResponse.ok) {
    throw new Error(`replay_live_paths.geojson returned ${pathsResponse.status}`);
  }
  if (!pointsResponse.ok) {
    throw new Error(`replay_live_points.geojson returned ${pointsResponse.status}`);
  }
  const paths = (await pathsResponse.json()) as GeoJSON.FeatureCollection;
  const points = (await pointsResponse.json()) as GeoJSON.FeatureCollection;
  if (paths.type !== "FeatureCollection" || points.type !== "FeatureCollection") {
    throw new Error("live replay GeoJSON is not a FeatureCollection");
  }
  let displayPaths = paths;
  let routeSource: "routes" | "paths" = "paths";
  try {
    const routes = await loadLiveRoutes();
    if (routes) {
      displayPaths = routes;
      routeSource = "routes";
    }
  } catch (error) {
    console.warn("Live route GeoJSON is unavailable; using virtual-walk paths.", error);
  }
  return {
    paths: displayPaths,
    points,
    summary: summarizeLiveReplay(displayPaths, points, routeSource)
  };
}

async function loadReplayBatchTiles(): Promise<ReplayBatchTileFrame[] | null> {
  const response = await fetch(replayBatchTilesUrl);
  if (response.status === 404) {
    return null;
  }
  if (!response.ok) {
    throw new Error(`replay_batch_tiles.json returned ${response.status}`);
  }
  const data = (await response.json()) as ReplayBatchTileFrame[];
  if (!Array.isArray(data)) {
    throw new Error("replay_batch_tiles.json is not an array");
  }
  return data;
}

async function loadLiveRoutes(): Promise<GeoJSON.FeatureCollection | null> {
  const response = await fetch(replayLiveRoutesUrl);
  if (response.status === 404) {
    return null;
  }
  if (!response.ok) {
    throw new Error(`replay_live_routes.geojson returned ${response.status}`);
  }
  const data = (await response.json()) as GeoJSON.FeatureCollection;
  if (data.type !== "FeatureCollection" || !Array.isArray(data.features)) {
    throw new Error("replay_live_routes.geojson is not a FeatureCollection");
  }
  return data;
}

async function loadSampleOrders(): Promise<SampleOrderExplanation[] | null> {
  const response = await fetch(sampleOrdersUrl);
  if (response.status === 404) {
    return null;
  }
  if (!response.ok) {
    throw new Error(`sampled_order_explanations.json returned ${response.status}`);
  }
  const contentType = response.headers.get("content-type") ?? "";
  if (!contentType.includes("application/json")) {
    return null;
  }
  const data = (await response.json()) as SampleOrderExplanation[];
  if (!Array.isArray(data)) {
    throw new Error("sampled_order_explanations.json is not an array");
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

function tileFeatureLookup(data: GeoJSON.FeatureCollection): globalThis.Map<number, GeoJSON.Feature> {
  const features = new globalThis.Map<number, GeoJSON.Feature>();
  for (const feature of data.features) {
    const tileId = Number(feature.properties?.tile_id);
    if (!Number.isFinite(tileId) || !feature.geometry) {
      continue;
    }
    if (feature.geometry.type !== "Polygon" && feature.geometry.type !== "MultiPolygon") {
      continue;
    }
    features.set(tileId, feature);
  }
  return features;
}

function activityFrameToGeoJson(
  frame: ReplayBatchTileFrame | null,
  featuresByTile: globalThis.Map<number, GeoJSON.Feature>
): GeoJSON.FeatureCollection {
  if (!frame) {
    return emptyFeatureCollection;
  }

  const features: GeoJSON.Feature[] = [];
  for (const tileActivity of frame.tiles) {
    const tileFeature = featuresByTile.get(tileActivity.tile_id);
    if (!tileFeature?.geometry) {
      continue;
    }
    features.push({
      type: "Feature",
      geometry: tileFeature.geometry,
      properties: {
        tile_id: tileActivity.tile_id,
        pickup_count: tileActivity.pickup_count,
        assigned_count: tileActivity.assigned_count,
        completed_count: tileActivity.completed_count,
        activity_score: tileActivity.activity_score,
        batch_time: frame.batch_time,
        window_start_time: frame.window_start_time,
        window_seconds: frame.window_seconds
      }
    });
  }
  return {
    type: "FeatureCollection",
    features
  };
}

function summarizeLiveReplay(
  paths: GeoJSON.FeatureCollection,
  points: GeoJSON.FeatureCollection,
  routeSource: "routes" | "paths"
): ReplayLiveSummary {
  let startTime = Infinity;
  let endTime = -Infinity;
  for (const feature of paths.features) {
    const start = Number(feature.properties?.start_time);
    const end = Number(feature.properties?.end_time);
    if (Number.isFinite(start)) {
      startTime = Math.min(startTime, start);
    }
    if (Number.isFinite(end)) {
      endTime = Math.max(endTime, end);
    }
  }
  for (const feature of points.features) {
    const time = Number(feature.properties?.event_time);
    if (Number.isFinite(time)) {
      startTime = Math.min(startTime, time);
      endTime = Math.max(endTime, time);
    }
  }
  if (!Number.isFinite(startTime) || !Number.isFinite(endTime)) {
    startTime = 0;
    endTime = 0;
  }
  const routeStats = summarizeRouteStatuses(paths);
  return {
    pathCount: paths.features.length,
    pointCount: points.features.length,
    routeSource,
    routeCount: routeSource === "routes" ? paths.features.length : 0,
    routedCount: routeStats.routedCount,
    fallbackCount: routeStats.fallbackCount,
    startTime,
    endTime
  };
}

function summarizeRouteStatuses(paths: GeoJSON.FeatureCollection) {
  let routedCount = 0;
  let fallbackCount = 0;
  for (const feature of paths.features) {
    const routeStatus = feature.properties?.route_status;
    if (routeStatus === "routed") {
      routedCount += 1;
    } else if (routeStatus === "fallback") {
      fallbackCount += 1;
    }
  }
  return { routedCount, fallbackCount };
}

function liveReplayFrame(
  paths: GeoJSON.FeatureCollection | null,
  points: GeoJSON.FeatureCollection | null,
  replayTime: number
) {
  const activePaths: GeoJSON.Feature[] = [];
  const taxiPoints: GeoJSON.Feature[] = [];
  const recentEvents: GeoJSON.Feature[] = [];

  if (paths) {
    for (const feature of paths.features) {
      const startTime = Number(feature.properties?.start_time);
      const endTime = Number(feature.properties?.end_time);
      if (!Number.isFinite(startTime) || !Number.isFinite(endTime)) {
        continue;
      }
      if (replayTime < startTime || replayTime > endTime) {
        continue;
      }
      activePaths.push(feature);
      const point = interpolateLineString(feature.geometry, startTime, endTime, replayTime);
      if (point) {
        taxiPoints.push({
          type: "Feature",
          geometry: {
            type: "Point",
            coordinates: point
          },
          properties: {
            taxi_id: feature.properties?.taxi_id,
            request_id: feature.properties?.request_id,
            leg_type: feature.properties?.leg_type,
            route_status: feature.properties?.route_status,
            replay_time: replayTime
          }
        });
      }
    }
  }

  if (points) {
    const eventWindowSeconds = 45;
    for (const feature of points.features) {
      const eventTime = Number(feature.properties?.event_time);
      if (!Number.isFinite(eventTime)) {
        continue;
      }
      if (eventTime <= replayTime && eventTime >= replayTime - eventWindowSeconds) {
        recentEvents.push(feature);
      }
    }
  }

  return {
    paths: {
      type: "FeatureCollection",
      features: activePaths
    } satisfies GeoJSON.FeatureCollection,
    taxis: {
      type: "FeatureCollection",
      features: taxiPoints
    } satisfies GeoJSON.FeatureCollection,
    events: {
      type: "FeatureCollection",
      features: recentEvents
    } satisfies GeoJSON.FeatureCollection,
    activePathCount: activePaths.length,
    activeTaxiCount: taxiPoints.length,
    recentEventCount: recentEvents.length
  };
}

function selectedSamplePaths(
  paths: GeoJSON.FeatureCollection | null,
  requestId: string | null
): GeoJSON.FeatureCollection {
  if (!paths || !requestId) {
    return emptyFeatureCollection;
  }
  return {
    type: "FeatureCollection",
    features: paths.features.filter((feature) => String(feature.properties?.request_id) === requestId)
  };
}

function selectedSamplePoints(sample: SampleOrderExplanation | null): GeoJSON.FeatureCollection {
  if (!sample) {
    return emptyFeatureCollection;
  }
  return {
    type: "FeatureCollection",
    features: [
      samplePointFeature(sample, "pickup"),
      samplePointFeature(sample, "dropoff")
    ]
  };
}

function samplePointFeature(sample: SampleOrderExplanation, pointType: "pickup" | "dropoff"): GeoJSON.Feature {
  const point = pointType === "pickup" ? sample.pickup : sample.dropoff;
  return {
    type: "Feature",
    geometry: {
      type: "Point",
      coordinates: [point.x, point.y]
    },
    properties: {
      point_type: pointType,
      request_id: sample.request_id,
      taxi_id: sample.taxi_id,
      tile: point.tile
    }
  };
}

function sampleTileFilter(sample: SampleOrderExplanation | null): maplibregl.FilterSpecification {
  if (!sample) {
    return ["==", ["get", "tile_id"], -1];
  }
  const pickupTile = Number(sample.pickup.tile);
  const dropoffTile = Number(sample.dropoff.tile);
  const tiles = [pickupTile, dropoffTile].filter(Number.isFinite);
  if (tiles.length === 0) {
    return ["==", ["get", "tile_id"], -1];
  }
  return ["in", ["get", "tile_id"], ["literal", tiles]];
}

function formatMaybeScore(value: number | null | undefined) {
  if (value === null || value === undefined) {
    return "n/a";
  }
  return formatScore(value);
}

function formatSigned(value: number | null | undefined) {
  if (value === null || value === undefined || !Number.isFinite(value)) {
    return "n/a";
  }
  return value >= 0 ? `+${value.toFixed(2)}` : value.toFixed(2);
}

function formatMoney(value: number | null | undefined) {
  if (value === null || value === undefined || !Number.isFinite(value)) {
    return "n/a";
  }
  return value.toFixed(2);
}

function formatDistance(value: number | null | undefined) {
  if (value === null || value === undefined || !Number.isFinite(value)) {
    return "n/a";
  }
  return value.toFixed(3);
}

function netToneClass(value: number | null | undefined) {
  if (value === null || value === undefined || !Number.isFinite(value)) {
    return "net-neutral";
  }
  return value >= 0 ? "net-positive" : "net-negative";
}

function primaryReasonTag(sample: SampleOrderExplanation) {
  return sample.reason_tags[0] ?? sample.status;
}

function orderStatusText(mode: SampleOrdersMode, count: number, selectedSample: SampleOrderExplanation | null) {
  if (mode === "missing") {
    return "unavailable";
  }
  if (mode === "error") {
    return "schema error";
  }
  if (mode === "loading") {
    return "loading";
  }
  if (!selectedSample) {
    return `${count} samples`;
  }
  return `${count} samples · ${selectedSample.status}`;
}

function interpolateLineString(
  geometry: GeoJSON.Geometry | null,
  startTime: number,
  endTime: number,
  replayTime: number
): GeoJSON.Position | null {
  if (!geometry || geometry.type !== "LineString" || geometry.coordinates.length === 0) {
    return null;
  }
  if (geometry.coordinates.length === 1 || endTime <= startTime) {
    return geometry.coordinates[0];
  }
  const ratio = Math.min(1, Math.max(0, (replayTime - startTime) / (endTime - startTime)));
  const segmentLengths: number[] = [];
  let totalLength = 0;
  for (let index = 1; index < geometry.coordinates.length; index += 1) {
    const previous = geometry.coordinates[index - 1];
    const current = geometry.coordinates[index];
    const length = coordinateDistance(previous, current);
    segmentLengths.push(length);
    totalLength += length;
  }
  if (totalLength <= 0) {
    return geometry.coordinates[0];
  }
  const targetLength = totalLength * ratio;
  let walkedLength = 0;
  for (let index = 1; index < geometry.coordinates.length; index += 1) {
    const segmentLength = segmentLengths[index - 1];
    if (walkedLength + segmentLength >= targetLength) {
      const previous = geometry.coordinates[index - 1];
      const current = geometry.coordinates[index];
      const segmentRatio = segmentLength > 0 ? (targetLength - walkedLength) / segmentLength : 0;
      return [
        Number(previous[0]) + (Number(current[0]) - Number(previous[0])) * segmentRatio,
        Number(previous[1]) + (Number(current[1]) - Number(previous[1])) * segmentRatio
      ];
    }
    walkedLength += segmentLength;
  }
  return geometry.coordinates[geometry.coordinates.length - 1];
}

function coordinateDistance(start: GeoJSON.Position, end: GeoJSON.Position) {
  const averageLat = ((Number(start[1]) + Number(end[1])) / 2) * (Math.PI / 180);
  const lonScale = Math.cos(averageLat);
  const deltaLon = (Number(end[0]) - Number(start[0])) * lonScale;
  const deltaLat = Number(end[1]) - Number(start[1]);
  return Math.hypot(deltaLon, deltaLat);
}

function formatScore(value: unknown) {
  const numberValue = Number(value);
  if (!Number.isFinite(numberValue)) {
    return "0.00";
  }
  return numberValue.toFixed(2);
}

function formatInteger(value: number | undefined) {
  if (!Number.isFinite(value)) {
    return "0";
  }
  return Math.round(value ?? 0).toLocaleString("en-US");
}

function formatTime(seconds: number | undefined) {
  if (!Number.isFinite(seconds)) {
    return "00:00:00";
  }
  const safeSeconds = Math.max(0, Math.floor(seconds ?? 0));
  const hours = Math.floor(safeSeconds / 3600);
  const minutes = Math.floor((safeSeconds % 3600) / 60);
  const secs = safeSeconds % 60;
  return [hours, minutes, secs].map((part) => String(part).padStart(2, "0")).join(":");
}

function formatEventTime(seconds: number | undefined) {
  if (!Number.isFinite(seconds) || (seconds ?? 0) < 0) {
    return "n/a";
  }
  return formatTime(seconds);
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
  const tileFeaturesByIdRef = useRef<globalThis.Map<number, GeoJSON.Feature>>(new globalThis.Map());
  const [mapReady, setMapReady] = useState(false);
  const [tileLookupVersion, setTileLookupVersion] = useState(0);
  const [basemapEnabled, setBasemapEnabled] = useState(true);
  const [tilesEnabled, setTilesEnabled] = useState(true);
  const [tilePointsEnabled, setTilePointsEnabled] = useState(true);
  const [witnessesEnabled, setWitnessesEnabled] = useState(true);
  const [dataMode, setDataMode] = useState<DataMode>("loading");
  const [witnessMode, setWitnessMode] = useState<WitnessMode>("loading");
  const [featureCount, setFeatureCount] = useState(0);
  const [witnessCount, setWitnessCount] = useState(0);
  const [replayMode, setReplayMode] = useState<ReplayDataMode>("loading");
  const [liveArtifactMode, setLiveArtifactMode] = useState<ReplayArtifactMode>("loading");
  const [batchArtifactMode, setBatchArtifactMode] = useState<ReplayArtifactMode>("loading");
  const [replayManifest, setReplayManifest] = useState<ReplayManifest | null>(null);
  const [replayBatches, setReplayBatches] = useState<ReplayBatch[]>([]);
  const [replayBatchTiles, setReplayBatchTiles] = useState<ReplayBatchTileFrame[]>([]);
  const [replayTileMode, setReplayTileMode] = useState<ReplayTileMode>("loading");
  const [liveSummary, setLiveSummary] = useState<ReplayLiveSummary | null>(null);
  const [livePaths, setLivePaths] = useState<GeoJSON.FeatureCollection | null>(null);
  const [livePoints, setLivePoints] = useState<GeoJSON.FeatureCollection | null>(null);
  const [sampleOrdersMode, setSampleOrdersMode] = useState<SampleOrdersMode>("loading");
  const [sampleOrders, setSampleOrders] = useState<SampleOrderExplanation[]>([]);
  const [selectedSampleRequestId, setSelectedSampleRequestId] = useState<string | null>(null);
  const [liveReplayTime, setLiveReplayTime] = useState(0);
  const [liveFrameCounts, setLiveFrameCounts] = useState({
    activePathCount: 0,
    activeTaxiCount: 0,
    recentEventCount: 0
  });
  const [replayCursor, setReplayCursor] = useState(0);
  const [isReplayPlaying, setIsReplayPlaying] = useState(false);
  const [ordersOpen, setOrdersOpen] = useState(false);
  const [hoverInfo, setHoverInfo] = useState<HoverInfo>({
    title: "No feature selected",
    details: "Hover a tile"
  });

  const selectedBatch = replayBatches[replayCursor] ?? null;
  const selectedBatchTiles = replayBatchTiles[replayCursor] ?? null;
  const selectedActivityTotals = selectedBatchTiles?.totals ?? emptyReplayTotals;
  const selectedSample =
    sampleOrders.find((sample) => sample.request_id === selectedSampleRequestId) ?? sampleOrders[0] ?? null;
  const replayProgress = useMemo(() => {
    if (replayBatches.length <= 1) {
      return 0;
    }
    return Math.round((replayCursor / (replayBatches.length - 1)) * 100);
  }, [replayBatches.length, replayCursor]);
  const liveProgress = useMemo(() => {
    if (!liveSummary || liveSummary.endTime <= liveSummary.startTime) {
      return 0;
    }
    return Math.round(((liveReplayTime - liveSummary.startTime) / (liveSummary.endTime - liveSummary.startTime)) * 100);
  }, [liveReplayTime, liveSummary]);
  const canShowLive = liveArtifactMode === "geojson";
  const canShowBatch = batchArtifactMode === "geojson";
  const switchReplayMode = (mode: "live" | "batch") => {
    if ((mode === "live" && !canShowLive) || (mode === "batch" && !canShowBatch)) {
      return;
    }
    setIsReplayPlaying(false);
    setReplayMode(mode);
  };

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
      tileFeaturesByIdRef.current = tileFeatureLookup(dispatchGeoJson);
      setTileLookupVersion((version) => version + 1);

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
      map.addSource(replayActivitySourceId, {
        type: "geojson",
        data: emptyFeatureCollection
      });
      map.addSource(livePathSourceId, {
        type: "geojson",
        data: emptyFeatureCollection
      });
      map.addSource(liveTaxiSourceId, {
        type: "geojson",
        data: emptyFeatureCollection
      });
      map.addSource(liveEventSourceId, {
        type: "geojson",
        data: emptyFeatureCollection
      });
      map.addSource(selectedSamplePathSourceId, {
        type: "geojson",
        data: emptyFeatureCollection
      });
      map.addSource(selectedSamplePointSourceId, {
        type: "geojson",
        data: emptyFeatureCollection
      });

      map.addLayer({
        id: dispatchFillLayerId,
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
        id: dispatchOutlineLayerId,
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
        id: replayActivityFillLayerId,
        type: "fill",
        source: replayActivitySourceId,
        paint: {
          "fill-color": [
            "interpolate",
            ["linear"],
            ["coalesce", ["get", "activity_score"], 0],
            0,
            "#e6f3f5",
            4,
            "#5bc0be",
            10,
            "#f4a261",
            18,
            "#e76f51"
          ],
          "fill-opacity": 0.74
        }
      });

      map.addLayer({
        id: replayActivityOutlineLayerId,
        type: "line",
        source: replayActivitySourceId,
        paint: {
          "line-color": "#0b1f33",
          "line-opacity": 0.7,
          "line-width": 1.6
        }
      });

      map.addLayer({
        id: livePathLayerId,
        type: "line",
        source: livePathSourceId,
        paint: {
          "line-color": [
            "match",
            ["get", "leg_type"],
            "dispatch_to_pickup",
            "#2f80ed",
            "pickup_to_dropoff",
            "#00a676",
            "#4f5d75"
          ],
          "line-opacity": 0.78,
          "line-width": 2.2
        }
      });

      map.addLayer({
        id: liveEventLayerId,
        type: "circle",
        source: liveEventSourceId,
        paint: {
          "circle-color": [
            "match",
            ["get", "point_type"],
            "pickup",
            "#2f80ed",
            "dropoff",
            "#00a676",
            "#4f5d75"
          ],
          "circle-opacity": 0.86,
          "circle-radius": 5,
          "circle-stroke-color": "#ffffff",
          "circle-stroke-width": 1.5
        }
      });

      map.addLayer({
        id: liveTaxiLayerId,
        type: "circle",
        source: liveTaxiSourceId,
        paint: {
          "circle-color": [
            "match",
            ["get", "leg_type"],
            "dispatch_to_pickup",
            "#1f6feb",
            "pickup_to_dropoff",
            "#00875a",
            "#0b1f33"
          ],
          "circle-radius": 6,
          "circle-stroke-color": "#ffffff",
          "circle-stroke-width": 2
        }
      });

      map.addLayer({
        id: selectedSamplePathLayerId,
        type: "line",
        source: selectedSamplePathSourceId,
        paint: {
          "line-color": "#111827",
          "line-opacity": 0.9,
          "line-width": 5
        }
      });

      map.addLayer({
        id: selectedSamplePointLayerId,
        type: "circle",
        source: selectedSamplePointSourceId,
        paint: {
          "circle-color": [
            "match",
            ["get", "point_type"],
            "pickup",
            "#1f6feb",
            "dropoff",
            "#d1495b",
            "#111827"
          ],
          "circle-radius": 8,
          "circle-stroke-color": "#ffffff",
          "circle-stroke-width": 2.5
        }
      });

      map.addLayer({
        id: dispatchPointLayerId,
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
        id: selectedSampleTileLayerId,
        type: "line",
        source: dispatchSourceId,
        filter: ["==", ["get", "tile_id"], -1],
        paint: {
          "line-color": "#111827",
          "line-width": 3,
          "line-opacity": 0.85,
          "line-dasharray": [1.5, 1]
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

      map.on("mousemove", dispatchFillLayerId, (event) => {
        const feature = event.features?.[0];
        if (feature) {
          showTileWitnesses(feature);
        }
      });

      map.on("mousemove", dispatchPointLayerId, (event) => {
        const feature = event.features?.[0];
        if (feature) {
          setHoverInfo(describeFeature(feature));
        }
      });

      map.on("mouseleave", dispatchFillLayerId, hideTileWitnesses);
      map.on("mouseleave", dispatchPointLayerId, () => {
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

  useEffect(() => {
    const map = mapRef.current;
    if (!mapReady) {
      return;
    }
    const setVisibility = (layerId: string, visible: boolean) => {
      if (map?.getLayer(layerId)) {
        map.setLayoutProperty(layerId, "visibility", visible ? "visible" : "none");
      }
    };
    setVisibility(dispatchFillLayerId, tilesEnabled);
    setVisibility(dispatchOutlineLayerId, tilesEnabled);
    setVisibility(replayActivityFillLayerId, tilesEnabled && replayMode === "batch");
    setVisibility(replayActivityOutlineLayerId, tilesEnabled && replayMode === "batch");
    setVisibility(selectedTileLayerId, tilesEnabled);
    setVisibility(dispatchPointLayerId, tilePointsEnabled);
    setVisibility(witnessLayerId, witnessesEnabled);
  }, [mapReady, replayMode, tilePointsEnabled, tilesEnabled, witnessesEnabled]);

  useEffect(() => {
    let cancelled = false;

    async function loadSamples() {
      setSampleOrdersMode("loading");
      try {
        const samples = await loadSampleOrders();
        if (cancelled) {
          return;
        }
        if (!samples) {
          setSampleOrdersMode("missing");
          setSampleOrders([]);
          return;
        }
        setSampleOrders(samples);
        setSelectedSampleRequestId((current) => current ?? samples[0]?.request_id ?? null);
        setSampleOrdersMode("geojson");
      } catch (error) {
        console.warn("Sample order explanations are unavailable.", error);
        if (!cancelled) {
          setSampleOrdersMode("error");
          setSampleOrders([]);
        }
      }
    }

    async function loadReplay() {
      setReplayMode("loading");
      setLiveArtifactMode("loading");
      setBatchArtifactMode("loading");
      try {
        let manifest: ReplayManifest | null = null;
        try {
          manifest = await loadReplayManifest();
        } catch (error) {
          console.warn("Replay manifest is unavailable.", error);
        }
        if (cancelled) {
          return;
        }
        setReplayManifest(manifest);

        let liveReady = false;
        try {
          const liveReplay = await loadLiveReplay();
          if (cancelled) {
            return;
          }
          if (liveReplay) {
            setLivePaths(liveReplay.paths);
            setLivePoints(liveReplay.points);
            setLiveSummary(liveReplay.summary);
            setLiveReplayTime(liveReplay.summary.startTime);
            setLiveArtifactMode("geojson");
            liveReady = true;
          } else {
            setLiveArtifactMode("missing");
          }
        } catch (error) {
          console.warn("Live replay artifacts are unavailable.", error);
          if (!cancelled) {
            setLiveArtifactMode("error");
          }
        }

        let batchReady = false;
        try {
          const batches = await loadReplayBatches();
          if (cancelled) {
            return;
          }
          if (batches.length > 0) {
            let batchTiles: ReplayBatchTileFrame[] | null = null;
            try {
              batchTiles = await loadReplayBatchTiles();
              if (!cancelled) {
                setReplayTileMode(batchTiles ? "geojson" : "missing");
              }
            } catch (error) {
              console.warn("Replay batch tile activity is unavailable.", error);
              if (!cancelled) {
                setReplayTileMode("error");
              }
            }
            setReplayBatches(batches);
            setReplayBatchTiles(batchTiles ?? []);
            setReplayCursor(0);
            setBatchArtifactMode("geojson");
            batchReady = true;
          } else {
            setReplayBatches([]);
            setReplayBatchTiles([]);
            setReplayTileMode("missing");
            setBatchArtifactMode("missing");
          }
        } catch (error) {
          console.warn("Batch replay artifacts are unavailable.", error);
          if (!cancelled) {
            setBatchArtifactMode("error");
          }
        }

        if (cancelled) {
          return;
        }
        if (!liveReady && !batchReady) {
          setReplayMode(manifest ? "error" : "missing");
          return;
        }
        if (manifest?.mode === "batch" && batchReady) {
          setReplayMode("batch");
        } else if (manifest?.mode === "live" && liveReady) {
          setReplayMode("live");
        } else {
          setReplayMode(liveReady ? "live" : "batch");
        }
      } catch (error) {
        console.warn("Replay artifacts are unavailable.", error);
        if (!cancelled) {
          setReplayMode("error");
        }
      }
    }

    loadSamples();
    loadReplay();
    return () => {
      cancelled = true;
    };
  }, []);

  useEffect(() => {
    if (!isReplayPlaying || replayMode !== "batch" || replayBatches.length <= 1) {
      return;
    }
    const timer = window.setInterval(() => {
      setReplayCursor((current) => {
        if (current >= replayBatches.length - 1) {
          setIsReplayPlaying(false);
          return current;
        }
        return current + 1;
      });
    }, 400);
    return () => window.clearInterval(timer);
  }, [isReplayPlaying, replayBatches.length, replayMode]);

  useEffect(() => {
    if (!isReplayPlaying || replayMode !== "live" || !liveSummary || liveSummary.endTime <= liveSummary.startTime) {
      return;
    }
    const timer = window.setInterval(() => {
      setLiveReplayTime((current) => {
        if (current >= liveSummary.endTime) {
          setIsReplayPlaying(false);
          return current;
        }
        return Math.min(liveSummary.endTime, current + 30);
      });
    }, 250);
    return () => window.clearInterval(timer);
  }, [isReplayPlaying, liveSummary, replayMode]);

  useEffect(() => {
    const map = mapRef.current;
    if (!mapReady) {
      return;
    }
    const source = map?.getSource(replayActivitySourceId) as GeoJSONSource | undefined;
    if (!source) {
      return;
    }
    if (replayMode !== "batch") {
      source.setData(emptyFeatureCollection);
      return;
    }
    source.setData(activityFrameToGeoJson(selectedBatchTiles, tileFeaturesByIdRef.current));
  }, [mapReady, replayMode, replayCursor, replayBatchTiles, selectedBatchTiles, tileLookupVersion]);

  useEffect(() => {
    const map = mapRef.current;
    if (!mapReady) {
      return;
    }
    const pathSource = map?.getSource(livePathSourceId) as GeoJSONSource | undefined;
    const taxiSource = map?.getSource(liveTaxiSourceId) as GeoJSONSource | undefined;
    const eventSource = map?.getSource(liveEventSourceId) as GeoJSONSource | undefined;
    if (!pathSource || !taxiSource || !eventSource) {
      return;
    }
    if (replayMode !== "live") {
      pathSource.setData(emptyFeatureCollection);
      taxiSource.setData(emptyFeatureCollection);
      eventSource.setData(emptyFeatureCollection);
      setLiveFrameCounts({
        activePathCount: 0,
        activeTaxiCount: 0,
        recentEventCount: 0
      });
      return;
    }
    const frame = liveReplayFrame(livePaths, livePoints, liveReplayTime);
    pathSource.setData(frame.paths);
    taxiSource.setData(frame.taxis);
    eventSource.setData(frame.events);
    setLiveFrameCounts({
      activePathCount: frame.activePathCount,
      activeTaxiCount: frame.activeTaxiCount,
      recentEventCount: frame.recentEventCount
    });
  }, [livePaths, livePoints, liveReplayTime, mapReady, replayMode]);

  useEffect(() => {
    const map = mapRef.current;
    if (!mapReady) {
      return;
    }
    const pathSource = map?.getSource(selectedSamplePathSourceId) as GeoJSONSource | undefined;
    const pointSource = map?.getSource(selectedSamplePointSourceId) as GeoJSONSource | undefined;
    if (pathSource) {
      pathSource.setData(selectedSamplePaths(replayMode === "live" ? livePaths : null, selectedSample?.request_id ?? null));
    }
    if (pointSource) {
      pointSource.setData(selectedSamplePoints(selectedSample));
    }
    if (map?.getLayer(selectedSampleTileLayerId)) {
      map.setFilter(selectedSampleTileLayerId, sampleTileFilter(selectedSample));
    }
  }, [livePaths, mapReady, replayMode, selectedSample]);

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
        <div className="layer-switches" aria-label="Map layer switches">
          <label className="toggle-row">
            <input
              type="checkbox"
              checked={tilesEnabled}
              onChange={(event) => setTilesEnabled(event.target.checked)}
            />
            <span>Tiles</span>
          </label>
          <label className="toggle-row">
            <input
              type="checkbox"
              checked={tilePointsEnabled}
              onChange={(event) => setTilePointsEnabled(event.target.checked)}
            />
            <span>Points</span>
          </label>
          <label className="toggle-row">
            <input
              type="checkbox"
              checked={witnessesEnabled}
              onChange={(event) => setWitnessesEnabled(event.target.checked)}
            />
            <span>Witness</span>
          </label>
        </div>
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
      <section className="replay-panel" aria-label="Replay artifacts">
        <div className="panel-header">
          <h2>Replay</h2>
          <span className={`mode-pill mode-${replayMode}`}>{replayMode}</span>
        </div>
        <div className="segmented-control" aria-label="Replay view mode">
          <button
            type="button"
            className={replayMode === "live" ? "segmented-active" : ""}
            disabled={!canShowLive}
            onClick={() => switchReplayMode("live")}
          >
            Live <small>{liveArtifactMode}</small>
          </button>
          <button
            type="button"
            className={replayMode === "batch" ? "segmented-active" : ""}
            disabled={!canShowBatch}
            onClick={() => switchReplayMode("batch")}
          >
            Batch <small>{batchArtifactMode}</small>
          </button>
        </div>

        {replayManifest ? (
          <dl className="replay-summary">
            <div>
              <dt>Requests</dt>
              <dd>{formatInteger(replayManifest.request_count)}</dd>
            </div>
            <div>
              <dt>Assigned</dt>
              <dd>{formatInteger(replayManifest.assigned_count)}</dd>
            </div>
            <div>
              <dt>Completed</dt>
              <dd>{formatInteger(replayManifest.completed_count)}</dd>
            </div>
            <div>
              <dt>Threshold</dt>
              <dd>{formatInteger(replayManifest.live_threshold)}</dd>
            </div>
          </dl>
        ) : (
          <p className="panel-note">Waiting for replay artifacts.</p>
        )}

        {replayMode === "batch" && selectedBatch ? (
          <div className="batch-console">
            <div className="timeline-controls">
              <button
                type="button"
                className="icon-button"
                aria-label={isReplayPlaying ? "Pause replay" : "Play replay"}
                onClick={() => setIsReplayPlaying((value) => !value)}
              >
                <span className={isReplayPlaying ? "pause-icon" : "play-icon"} aria-hidden="true" />
              </button>
              <input
                type="range"
                min="0"
                max={Math.max(0, replayBatches.length - 1)}
                value={replayCursor}
                aria-label="Replay batch tick"
                onChange={(event) => {
                  setIsReplayPlaying(false);
                  setReplayCursor(Number(event.target.value));
                }}
              />
              <span className="timeline-progress">{replayProgress}%</span>
            </div>
            <dl className="batch-metrics">
              <div>
                <dt>Time</dt>
                <dd>{formatTime(selectedBatch.batch_time)}</dd>
              </div>
              <div>
                <dt>Batch</dt>
                <dd>
                  {formatInteger(replayCursor + 1)} / {formatInteger(replayBatches.length)}
                </dd>
              </div>
              <div>
                <dt>Available</dt>
                <dd>{formatInteger(selectedBatch.available_drivers)}</dd>
              </div>
              <div>
                <dt>Pending</dt>
                <dd>{formatInteger(selectedBatch.pending_requests)}</dd>
              </div>
              <div>
                <dt>Edges</dt>
                <dd>{formatInteger(selectedBatch.candidate_edges)}</dd>
              </div>
              <div>
                <dt>Applied</dt>
                <dd>{formatInteger(selectedBatch.applied_assignments)}</dd>
              </div>
              <div>
                <dt>Total assigned</dt>
                <dd>{formatInteger(selectedBatch.assigned_cumulative)}</dd>
              </div>
              <div>
                <dt>Total done</dt>
                <dd>{formatInteger(selectedBatch.completed_cumulative)}</dd>
              </div>
              <div>
                <dt>Activity</dt>
                <dd>{replayTileMode}</dd>
              </div>
              <div>
                <dt>Active tiles</dt>
                <dd>{formatInteger(selectedActivityTotals.active_tiles)}</dd>
              </div>
              <div>
                <dt>Window pickup</dt>
                <dd>{formatInteger(selectedActivityTotals.pickup_count)}</dd>
              </div>
              <div>
                <dt>Window assigned</dt>
                <dd>{formatInteger(selectedActivityTotals.assigned_count)}</dd>
              </div>
              <div>
                <dt>Window done</dt>
                <dd>{formatInteger(selectedActivityTotals.completed_count)}</dd>
              </div>
              <div>
                <dt>Score total</dt>
                <dd>{formatInteger(selectedActivityTotals.activity_score)}</dd>
              </div>
            </dl>
          </div>
        ) : null}

        {replayMode === "live" && liveSummary ? (
          <div className="batch-console">
            <div className="timeline-controls">
              <button
                type="button"
                className="icon-button"
                aria-label={isReplayPlaying ? "Pause live replay" : "Play live replay"}
                onClick={() => setIsReplayPlaying((value) => !value)}
              >
                <span className={isReplayPlaying ? "pause-icon" : "play-icon"} aria-hidden="true" />
              </button>
              <input
                type="range"
                min={liveSummary.startTime}
                max={liveSummary.endTime}
                value={liveReplayTime}
                aria-label="Live replay time"
                onChange={(event) => {
                  setIsReplayPlaying(false);
                  setLiveReplayTime(Number(event.target.value));
                }}
              />
              <span className="timeline-progress">{liveProgress}%</span>
            </div>
            <dl className="batch-metrics">
              <div>
                <dt>Time</dt>
                <dd>{formatTime(liveReplayTime)}</dd>
              </div>
              <div>
                <dt>Duration</dt>
                <dd>{formatTime(liveSummary.endTime - liveSummary.startTime)}</dd>
              </div>
              <div>
                <dt>Paths</dt>
                <dd>{formatInteger(liveSummary.pathCount)}</dd>
              </div>
              <div>
                <dt>Route source</dt>
                <dd>{liveSummary.routeSource}</dd>
              </div>
              <div>
                <dt>Routes</dt>
                <dd>{formatInteger(liveSummary.routeCount)}</dd>
              </div>
              <div>
                <dt>Routed</dt>
                <dd>{formatInteger(liveSummary.routedCount)}</dd>
              </div>
              <div>
                <dt>Fallback</dt>
                <dd>{formatInteger(liveSummary.fallbackCount)}</dd>
              </div>
              <div>
                <dt>Points</dt>
                <dd>{formatInteger(liveSummary.pointCount)}</dd>
              </div>
              <div>
                <dt>Active paths</dt>
                <dd>{formatInteger(liveFrameCounts.activePathCount)}</dd>
              </div>
              <div>
                <dt>Active taxis</dt>
                <dd>{formatInteger(liveFrameCounts.activeTaxiCount)}</dd>
              </div>
              <div>
                <dt>Recent events</dt>
                <dd>{formatInteger(liveFrameCounts.recentEventCount)}</dd>
              </div>
            </dl>
          </div>
        ) : null}

        <div className="orders-control">
          <button
            type="button"
            className={`orders-button ${ordersOpen ? "orders-button-open" : ""}`}
            onClick={() => setOrdersOpen((value) => !value)}
          >
            <span>Orders</span>
            <small>{orderStatusText(sampleOrdersMode, sampleOrders.length, selectedSample)}</small>
          </button>
          <span className={`mode-pill mode-${sampleOrdersMode}`}>{sampleOrdersMode}</span>
        </div>

        {ordersOpen ? (
          <div className="orders-drawer">
            {selectedSample ? (
              <>
                <div className="sample-list" role="list" aria-label="Sample orders">
                  {sampleOrders.map((sample) => (
                    <button
                      key={sample.request_id}
                      type="button"
                      className={`sample-order ${sample.request_id === selectedSample.request_id ? "sample-order-active" : ""}`}
                      onClick={() => setSelectedSampleRequestId(sample.request_id)}
                    >
                      <span>{sample.request_id}</span>
                      <small>{sample.status}</small>
                      <strong className={netToneClass(sample.pricing?.estimated_net)}>
                        {formatMoney(sample.pricing?.estimated_net)}
                      </strong>
                      <em>{primaryReasonTag(sample)}</em>
                    </button>
                  ))}
                </div>

                <div className="order-explain-grid">
                  <section className="explain-section">
                    <h3>Lifecycle</h3>
                    <dl className="batch-metrics sample-metrics">
                      <div>
                        <dt>Status</dt>
                        <dd>{selectedSample.status}</dd>
                      </div>
                      <div>
                        <dt>Taxi</dt>
                        <dd>{selectedSample.taxi_id}</dd>
                      </div>
                      <div>
                        <dt>Request</dt>
                        <dd>{formatTime(selectedSample.request_time)}</dd>
                      </div>
                      <div>
                        <dt>Assigned</dt>
                        <dd>{formatEventTime(selectedSample.assignment_time)}</dd>
                      </div>
                      <div>
                        <dt>Pickup</dt>
                        <dd>{formatEventTime(selectedSample.pickup_time)}</dd>
                      </div>
                      <div>
                        <dt>Complete</dt>
                        <dd>{formatEventTime(selectedSample.completion_time)}</dd>
                      </div>
                      <div>
                        <dt>Wait</dt>
                        <dd>{formatTime(selectedSample.wait_time)}</dd>
                      </div>
                      <div>
                        <dt>Trip km</dt>
                        <dd>{formatDistance(selectedSample.trip_distance)}</dd>
                      </div>
                    </dl>
                  </section>

                  <section className="explain-section">
                    <h3>Dispatch</h3>
                    <dl className="batch-metrics sample-metrics">
                      <div>
                        <dt>Pickup cost</dt>
                        <dd>{formatTime(selectedSample.pickup_cost)}</dd>
                      </div>
                      <div>
                        <dt>Edges</dt>
                        <dd>{formatInteger(selectedSample.candidate_edge_count)}</dd>
                      </div>
                      <div>
                        <dt>Pending</dt>
                        <dd>{formatInteger(selectedSample.pending_batch_count)}</dd>
                      </div>
                      <div>
                        <dt>Candidates</dt>
                        <dd>{formatInteger(selectedSample.candidate_batch_count)}</dd>
                      </div>
                      <div>
                        <dt>Pickup hot</dt>
                        <dd>{formatMaybeScore(selectedSample.pickup_hotspot_score)}</dd>
                      </div>
                      <div>
                        <dt>Drop cold</dt>
                        <dd>{formatMaybeScore(selectedSample.dropoff_cold_score)}</dd>
                      </div>
                      <div>
                        <dt>Drop hot</dt>
                        <dd>{formatMaybeScore(selectedSample.dropoff_hotspot_score)}</dd>
                      </div>
                      <div>
                        <dt>Opp adjust</dt>
                        <dd>{formatSigned(selectedSample.opportunity_adjustment)}</dd>
                      </div>
                    </dl>
                    <div className="sample-tags">
                      {selectedSample.reason_tags.map((tag) => (
                        <span key={tag}>{tag}</span>
                      ))}
                    </div>
                  </section>

                  <section className="explain-section pricing-section">
                    <h3>Pricing</h3>
                    <div className="pricing-formula" aria-label="Pricing formula">
                      <span>fare</span>
                      <b>=</b>
                      <span>distance</span>
                      <b>×</b>
                      <span>rate</span>
                      <b>×</b>
                      <span>factor</span>
                      <b>−</b>
                      <span>pickup cost</span>
                    </div>
                    <div className="factor-strip" aria-label="Pricing factor decomposition">
                      <span>1.00</span>
                      <span>{formatSigned(selectedSample.pricing?.pickup_hotspot_component)} pickup hot</span>
                      <span>{formatSigned(selectedSample.pricing?.cold_dropoff_component)} cold dropoff</span>
                      <span>{formatSigned(selectedSample.pricing?.hot_dropoff_component)} hot dropoff</span>
                    </div>
                    <dl className="batch-metrics sample-metrics">
                      <div>
                        <dt>Mode</dt>
                        <dd>{selectedSample.pricing?.mode ?? "n/a"}</dd>
                      </div>
                      <div>
                        <dt>Base fare</dt>
                        <dd>{formatMoney(selectedSample.pricing?.base_revenue)}</dd>
                      </div>
                      <div>
                        <dt>Factor</dt>
                        <dd>{formatMaybeScore(selectedSample.pricing?.price_factor)}</dd>
                      </div>
                      <div>
                        <dt>Revenue</dt>
                        <dd>{formatMoney(selectedSample.pricing?.estimated_revenue)}</dd>
                      </div>
                      <div>
                        <dt>Pickup km</dt>
                        <dd>{formatDistance(selectedSample.pricing?.estimated_pickup_km)}</dd>
                      </div>
                      <div>
                        <dt>Pickup cost</dt>
                        <dd>{formatMoney(selectedSample.pricing?.estimated_pickup_cost)}</dd>
                      </div>
                      <div>
                        <dt>Net</dt>
                        <dd className={netToneClass(selectedSample.pricing?.estimated_net)}>
                          {formatMoney(selectedSample.pricing?.estimated_net)}
                        </dd>
                      </div>
                    </dl>
                  </section>
                </div>
              </>
            ) : (
              <p className="panel-note">
                {sampleOrdersMode === "missing"
                  ? "No sampled order explanations found."
                  : sampleOrdersMode === "error"
                    ? "Sample order explanation schema failed to load."
                    : "Waiting for sampled order explanations."}
              </p>
            )}
          </div>
        ) : null}

        {replayMode === "missing" ? (
          <p className="panel-note">No replay manifest found under /data/replay.</p>
        ) : null}
        {replayMode === "error" ? (
          <p className="panel-note">Replay artifact schema failed to load.</p>
        ) : null}
      </section>
    </main>
  );
}

export default App;
