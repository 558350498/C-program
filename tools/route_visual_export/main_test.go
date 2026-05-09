package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestRunExportRoutedFeature(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if !strings.Contains(r.URL.Path, "/route/v1/driving/") {
			t.Fatalf("unexpected route path: %s", r.URL.Path)
		}
		if r.URL.Query().Get("geometries") != "geojson" {
			t.Fatalf("expected geojson geometries query")
		}
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte(`{
			"code": "Ok",
			"routes": [{
				"distance": 123.4,
				"duration": 56.7,
				"geometry": {"coordinates": [[-73.0,40.0],[-73.1,40.1],[-73.2,40.2]]}
			}]
		}`))
	}))
	defer server.Close()

	root := t.TempDir()
	inputPath := filepath.Join(root, "replay_live_paths.geojson")
	outputPath := filepath.Join(root, "replay_live_routes.geojson")
	writeFile(t, inputPath, fixtureFeatureCollection())

	if err := runExport(config{
		inputLivePaths: inputPath,
		outputPath:     outputPath,
		routerURL:      server.URL,
		concurrency:    1,
		timeoutMS:      1000,
	}); err != nil {
		t.Fatalf("runExport failed: %v", err)
	}

	output := readJSON[featureCollection](t, outputPath)
	if len(output.Features) != 1 {
		t.Fatalf("feature count = %d, want 1", len(output.Features))
	}
	feature := output.Features[0]
	if feature.Properties["route_status"] != "routed" {
		t.Fatalf("route_status = %v, want routed", feature.Properties["route_status"])
	}
	if len(feature.Geometry.Coordinates) != 3 {
		t.Fatalf("coordinate count = %d, want 3", len(feature.Geometry.Coordinates))
	}
	if feature.Properties["taxi_id"] != "t1" || feature.Properties["request_id"] != "r1" {
		t.Fatalf("original properties were not preserved: %+v", feature.Properties)
	}
}

func TestRunExportFallbackFeature(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusBadGateway)
		_, _ = w.Write([]byte(`bad gateway`))
	}))
	defer server.Close()

	root := t.TempDir()
	inputPath := filepath.Join(root, "replay_live_paths.geojson")
	outputPath := filepath.Join(root, "replay_live_routes.geojson")
	writeFile(t, inputPath, fixtureFeatureCollection())

	if err := runExport(config{
		inputLivePaths: inputPath,
		outputPath:     outputPath,
		routerURL:      server.URL,
		concurrency:    2,
		timeoutMS:      1000,
	}); err != nil {
		t.Fatalf("runExport failed: %v", err)
	}

	output := readJSON[featureCollection](t, outputPath)
	feature := output.Features[0]
	if feature.Properties["route_status"] != "fallback" {
		t.Fatalf("route_status = %v, want fallback", feature.Properties["route_status"])
	}
	if len(feature.Geometry.Coordinates) != 2 {
		t.Fatalf("fallback coordinate count = %d, want original 2", len(feature.Geometry.Coordinates))
	}
	if feature.Properties["route_error"] == "" {
		t.Fatalf("fallback route_error should be populated")
	}
}

func TestBuildRouteURL(t *testing.T) {
	routeURL, err := buildRouteURL("http://127.0.0.1:5000/base", []float64{-73.1, 40.1}, []float64{-73.2, 40.2})
	if err != nil {
		t.Fatalf("buildRouteURL failed: %v", err)
	}
	if !strings.Contains(routeURL, "/base/route/v1/driving/-73.1,40.1;-73.2,40.2") {
		t.Fatalf("unexpected route URL: %s", routeURL)
	}
	if !strings.Contains(routeURL, "overview=full") || !strings.Contains(routeURL, "geometries=geojson") {
		t.Fatalf("route URL missing expected query: %s", routeURL)
	}
}

func fixtureFeatureCollection() string {
	return `{
		"type": "FeatureCollection",
		"features": [{
			"type": "Feature",
			"geometry": {
				"type": "LineString",
				"coordinates": [[-73.0,40.0],[-73.2,40.2]]
			},
			"properties": {
				"taxi_id": "t1",
				"request_id": "r1",
				"start_time": 0,
				"end_time": 60,
				"leg_type": "dispatch_to_pickup"
			}
		}]
	}`
}

func writeFile(t *testing.T, path string, content string) {
	t.Helper()
	if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
}

func readJSON[T any](t *testing.T, path string) T {
	t.Helper()
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read %s: %v", path, err)
	}
	var value T
	if err := json.Unmarshal(data, &value); err != nil {
		t.Fatalf("unmarshal %s: %v", path, err)
	}
	return value
}
