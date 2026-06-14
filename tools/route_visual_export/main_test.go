package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
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
	costCSVPath := filepath.Join(root, "route_costs.csv")
	writeFile(t, inputPath, fixtureFeatureCollection())

	if err := runExport(config{
		inputLivePaths: inputPath,
		outputPath:     outputPath,
		routeCostCSV:   costCSVPath,
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

	costCSV, err := os.ReadFile(costCSVPath)
	if err != nil {
		t.Fatalf("read route cost CSV: %v", err)
	}
	costText := string(costCSV)
	if !strings.Contains(costText, "taxi_id,request_id,leg_type,route_status,route_duration_s,route_distance_m") {
		t.Fatalf("route cost CSV missing header: %s", costText)
	}
	if !strings.Contains(costText, "t1,r1,dispatch_to_pickup,routed,56.7,123.4") {
		t.Fatalf("route cost CSV missing routed row: %s", costText)
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

func TestRunExportRoutePairCostCSV(t *testing.T) {
	var requestCount int32
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		atomic.AddInt32(&requestCount, 1)
		if !strings.Contains(r.URL.Path, "/route/v1/driving/") {
			t.Fatalf("unexpected route path: %s", r.URL.Path)
		}
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte(`{
			"code": "Ok",
			"routes": [{
				"distance": 321.5,
				"duration": 45.2,
				"geometry": {"coordinates": [[-73.0,40.0],[-73.2,40.2]]}
			}]
		}`))
	}))
	defer server.Close()

	root := t.TempDir()
	inputPath := filepath.Join(root, "candidate_routes.csv")
	outputPath := filepath.Join(root, "route_costs.csv")
	cachePath := filepath.Join(root, "route_cache.csv")
	writeFile(t, inputPath,
		"batch_time,taxi_id,request_id,leg_type,start_lon,start_lat,end_lon,end_lat,pickup_cost,dispatch_cost\n"+
			"30,1,101,dispatch_to_pickup,-73.0,40.0,-73.2,40.2,50,50\n"+
			"60,2,102,dispatch_to_pickup,-73.0,40.0,-73.2,40.2,70,70\n")

	if err := runExport(config{
		inputRoutePairCSV: inputPath,
		routeCostCSV:      outputPath,
		routeCacheCSV:     cachePath,
		routerURL:         server.URL,
		concurrency:       1,
		timeoutMS:         1000,
	}); err != nil {
		t.Fatalf("runExport failed: %v", err)
	}

	costCSV, err := os.ReadFile(outputPath)
	if err != nil {
		t.Fatalf("read route cost CSV: %v", err)
	}
	costText := string(costCSV)
	if !strings.Contains(costText, "batch_time,taxi_id,request_id,leg_type,route_status,start_lon,start_lat,end_lon,end_lat,route_duration_s,route_distance_m") {
		t.Fatalf("route cost CSV missing header: %s", costText)
	}
	if !strings.Contains(costText, "30,1,101,dispatch_to_pickup,routed,-73,40,-73.2,40.2,45.2,321.5,,50,50") {
		t.Fatalf("route cost CSV missing routed row: %s", costText)
	}
	if !strings.Contains(costText, "60,2,102,dispatch_to_pickup,routed,-73,40,-73.2,40.2,45.2,321.5,,70,70") {
		t.Fatalf("route cost CSV missing cached duplicate row: %s", costText)
	}
	if got := atomic.LoadInt32(&requestCount); got != 1 {
		t.Fatalf("router requests = %d, want 1 for duplicate route pair", got)
	}

	secondOutputPath := filepath.Join(root, "route_costs_second.csv")
	if err := runExport(config{
		inputRoutePairCSV: inputPath,
		routeCostCSV:      secondOutputPath,
		routeCacheCSV:     cachePath,
		routerURL:         server.URL,
		concurrency:       1,
		timeoutMS:         1000,
	}); err != nil {
		t.Fatalf("second runExport failed: %v", err)
	}
	if got := atomic.LoadInt32(&requestCount); got != 1 {
		t.Fatalf("router requests after cached run = %d, want still 1", got)
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
