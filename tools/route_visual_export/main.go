package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

type config struct {
	inputLivePaths string
	outputPath     string
	routerURL      string
	concurrency    int
	timeoutMS      int
	maxFeatures    int
}

type featureCollection struct {
	Type     string    `json:"type"`
	Features []feature `json:"features"`
}

type feature struct {
	Type       string         `json:"type"`
	Geometry   geometry       `json:"geometry"`
	Properties map[string]any `json:"properties"`
}

type geometry struct {
	Type        string      `json:"type"`
	Coordinates [][]float64 `json:"coordinates"`
}

type osrmResponse struct {
	Code   string      `json:"code"`
	Routes []osrmRoute `json:"routes"`
}

type osrmRoute struct {
	Distance float64      `json:"distance"`
	Duration float64      `json:"duration"`
	Geometry osrmGeometry `json:"geometry"`
}

type osrmGeometry struct {
	Coordinates [][]float64 `json:"coordinates"`
}

type routeResult struct {
	Index   int
	Feature feature
}

func main() {
	cfg := config{}
	flag.StringVar(&cfg.inputLivePaths, "input-live-paths", "", "Path to replay_live_paths.geojson")
	flag.StringVar(&cfg.outputPath, "output", "", "Path to write replay_live_routes.geojson")
	flag.StringVar(&cfg.routerURL, "router-url", "http://127.0.0.1:5000", "Base URL for OSRM-compatible router")
	flag.IntVar(&cfg.concurrency, "concurrency", 8, "Maximum parallel route requests")
	flag.IntVar(&cfg.timeoutMS, "timeout-ms", 5000, "Per-route request timeout in milliseconds")
	flag.IntVar(&cfg.maxFeatures, "max-features", 0, "Limit processed features; 0 means all")
	flag.Parse()

	if err := runExport(cfg); err != nil {
		fmt.Fprintf(os.Stderr, "route_visual_export: %v\n", err)
		os.Exit(1)
	}
}

func runExport(cfg config) error {
	if err := validateConfig(cfg); err != nil {
		return err
	}

	input, err := loadFeatureCollection(cfg.inputLivePaths)
	if err != nil {
		return err
	}
	if cfg.maxFeatures > 0 && cfg.maxFeatures < len(input.Features) {
		input.Features = input.Features[:cfg.maxFeatures]
	}

	client := &http.Client{}
	output := featureCollection{
		Type:     "FeatureCollection",
		Features: make([]feature, len(input.Features)),
	}

	jobs := make(chan routeResult)
	results := make(chan routeResult)
	workerCount := min(cfg.concurrency, max(1, len(input.Features)))
	var wg sync.WaitGroup
	for range workerCount {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for job := range jobs {
				routed := routeFeature(client, cfg, job.Feature)
				results <- routeResult{Index: job.Index, Feature: routed}
			}
		}()
	}

	go func() {
		for index, feature := range input.Features {
			jobs <- routeResult{Index: index, Feature: feature}
		}
		close(jobs)
		wg.Wait()
		close(results)
	}()

	for result := range results {
		output.Features[result.Index] = result.Feature
	}

	if err := os.MkdirAll(filepath.Dir(cfg.outputPath), 0o755); err != nil {
		return fmt.Errorf("create output directory: %w", err)
	}
	if err := writeJSON(cfg.outputPath, output); err != nil {
		return err
	}
	return nil
}

func validateConfig(cfg config) error {
	var missing []string
	if cfg.inputLivePaths == "" {
		missing = append(missing, "-input-live-paths")
	}
	if cfg.outputPath == "" {
		missing = append(missing, "-output")
	}
	if len(missing) > 0 {
		return fmt.Errorf("missing required flags: %s", strings.Join(missing, ", "))
	}
	if cfg.routerURL == "" {
		return errors.New("-router-url must not be empty")
	}
	if _, err := url.ParseRequestURI(cfg.routerURL); err != nil {
		return fmt.Errorf("invalid -router-url: %w", err)
	}
	if cfg.concurrency <= 0 {
		return errors.New("-concurrency must be positive")
	}
	if cfg.timeoutMS <= 0 {
		return errors.New("-timeout-ms must be positive")
	}
	if cfg.maxFeatures < 0 {
		return errors.New("-max-features must be non-negative")
	}
	return nil
}

func routeFeature(client *http.Client, cfg config, input feature) feature {
	routed := cloneFeature(input)
	coords := input.Geometry.Coordinates
	if input.Geometry.Type != "LineString" || len(coords) < 2 {
		return fallbackFeature(routed, "feature geometry is not a LineString with at least two coordinates")
	}

	start := coords[0]
	end := coords[len(coords)-1]
	if len(start) < 2 || len(end) < 2 {
		return fallbackFeature(routed, "feature has malformed coordinates")
	}

	route, err := fetchRoute(client, cfg, start, end)
	if err != nil {
		return fallbackFeature(routed, err.Error())
	}

	routed.Geometry = geometry{
		Type:        "LineString",
		Coordinates: route.Geometry.Coordinates,
	}
	setRouteProperties(routed.Properties, "routed", route.Distance, route.Duration, "")
	return routed
}

func fetchRoute(client *http.Client, cfg config, start []float64, end []float64) (osrmRoute, error) {
	routeURL, err := buildRouteURL(cfg.routerURL, start, end)
	if err != nil {
		return osrmRoute{}, err
	}

	ctx, cancel := context.WithTimeout(context.Background(), time.Duration(cfg.timeoutMS)*time.Millisecond)
	defer cancel()

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, routeURL, nil)
	if err != nil {
		return osrmRoute{}, err
	}
	resp, err := client.Do(req)
	if err != nil {
		return osrmRoute{}, err
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return osrmRoute{}, err
	}
	if resp.StatusCode != http.StatusOK {
		return osrmRoute{}, fmt.Errorf("router returned HTTP %d", resp.StatusCode)
	}

	var parsed osrmResponse
	if err := json.Unmarshal(body, &parsed); err != nil {
		return osrmRoute{}, err
	}
	if parsed.Code != "Ok" {
		return osrmRoute{}, fmt.Errorf("router returned code %q", parsed.Code)
	}
	if len(parsed.Routes) == 0 {
		return osrmRoute{}, errors.New("router returned no routes")
	}
	route := parsed.Routes[0]
	if len(route.Geometry.Coordinates) < 2 {
		return osrmRoute{}, errors.New("router returned route with fewer than two coordinates")
	}
	return route, nil
}

func buildRouteURL(routerURL string, start []float64, end []float64) (string, error) {
	base, err := url.Parse(routerURL)
	if err != nil {
		return "", err
	}
	base.Path = strings.TrimRight(base.Path, "/") + fmt.Sprintf(
		"/route/v1/driving/%g,%g;%g,%g",
		start[0],
		start[1],
		end[0],
		end[1],
	)
	query := base.Query()
	query.Set("overview", "full")
	query.Set("geometries", "geojson")
	base.RawQuery = query.Encode()
	return base.String(), nil
}

func fallbackFeature(input feature, message string) feature {
	setRouteProperties(input.Properties, "fallback", 0, 0, message)
	return input
}

func setRouteProperties(properties map[string]any, status string, distance float64, duration float64, routeError string) {
	if properties == nil {
		return
	}
	properties["route_status"] = status
	properties["route_distance_m"] = distance
	properties["route_duration_s"] = duration
	properties["route_error"] = routeError
}

func cloneFeature(input feature) feature {
	properties := make(map[string]any, len(input.Properties)+4)
	for key, value := range input.Properties {
		properties[key] = value
	}
	coords := make([][]float64, len(input.Geometry.Coordinates))
	for i, coord := range input.Geometry.Coordinates {
		coords[i] = append([]float64(nil), coord...)
	}
	return feature{
		Type: input.Type,
		Geometry: geometry{
			Type:        input.Geometry.Type,
			Coordinates: coords,
		},
		Properties: properties,
	}
}

func loadFeatureCollection(path string) (featureCollection, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return featureCollection{}, fmt.Errorf("read %s: %w", path, err)
	}
	var collection featureCollection
	if err := json.Unmarshal(data, &collection); err != nil {
		return featureCollection{}, fmt.Errorf("parse %s: %w", path, err)
	}
	if collection.Type != "FeatureCollection" {
		return featureCollection{}, fmt.Errorf("%s is not a FeatureCollection", path)
	}
	return collection, nil
}

func writeJSON(path string, value any) error {
	data, err := json.MarshalIndent(value, "", "  ")
	if err != nil {
		return fmt.Errorf("marshal %s: %w", path, err)
	}
	data = append(data, '\n')
	if err := os.WriteFile(path, data, 0o644); err != nil {
		return fmt.Errorf("write %s: %w", path, err)
	}
	return nil
}
