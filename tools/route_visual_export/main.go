package main

import (
	"context"
	"encoding/csv"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

type config struct {
	inputLivePaths    string
	inputRoutePairCSV string
	outputPath        string
	routeCostCSV      string
	routeCacheCSV     string
	routerURL         string
	concurrency       int
	timeoutMS         int
	maxFeatures       int
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

type routePair struct {
	BatchTime    string
	TaxiID       string
	RequestID    string
	LegType      string
	StartLon     float64
	StartLat     float64
	EndLon       float64
	EndLat       float64
	PickupCost   string
	DispatchCost string
}

type routePairJob struct {
	Index int
	Pair  routePair
}

type routeCostRow struct {
	BatchTime      string
	TaxiID         string
	RequestID      string
	LegType        string
	RouteStatus    string
	StartLon       float64
	StartLat       float64
	EndLon         float64
	EndLat         float64
	RouteDurationS float64
	RouteDistanceM float64
	RouteError     string
	PickupCost     string
	DispatchCost   string
}

func main() {
	cfg := config{}
	flag.StringVar(&cfg.inputLivePaths, "input-live-paths", "", "Path to replay_live_paths.geojson")
	flag.StringVar(&cfg.inputRoutePairCSV, "input-route-pairs-csv", "", "Path to candidate taxi-to-pickup route pairs CSV")
	flag.StringVar(&cfg.outputPath, "output", "", "Path to write replay_live_routes.geojson")
	flag.StringVar(&cfg.routeCostCSV, "route-cost-csv", "", "Optional path to write route costs as CSV")
	flag.StringVar(&cfg.routeCacheCSV, "route-cache-csv", "", "Optional route-pair cache CSV path")
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
	if cfg.inputRoutePairCSV != "" {
		return runRoutePairCostExport(cfg)
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
	if cfg.routeCostCSV != "" {
		if err := writeRouteCostCSV(cfg.routeCostCSV, output.Features); err != nil {
			return err
		}
	}
	return nil
}

func validateConfig(cfg config) error {
	var missing []string
	if cfg.inputLivePaths == "" && cfg.inputRoutePairCSV == "" {
		missing = append(missing, "-input-live-paths or -input-route-pairs-csv")
	}
	if cfg.inputLivePaths != "" && cfg.inputRoutePairCSV != "" {
		return errors.New("use only one of -input-live-paths or -input-route-pairs-csv")
	}
	if cfg.inputLivePaths != "" && cfg.outputPath == "" {
		missing = append(missing, "-output")
	}
	if cfg.inputRoutePairCSV != "" && cfg.routeCostCSV == "" {
		missing = append(missing, "-route-cost-csv")
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

func runRoutePairCostExport(cfg config) error {
	pairs, err := loadRoutePairsCSV(cfg.inputRoutePairCSV)
	if err != nil {
		return err
	}
	if cfg.maxFeatures > 0 && cfg.maxFeatures < len(pairs) {
		pairs = pairs[:cfg.maxFeatures]
	}

	cache := map[string]routeCostRow{}
	if cfg.routeCacheCSV != "" {
		cachedRows, err := loadRouteCostRowsCSV(cfg.routeCacheCSV)
		if err != nil {
			return err
		}
		for _, row := range cachedRows {
			cache[routeCostRowKey(row)] = row
		}
	}

	uniquePairs := make(map[string]routePair)
	for _, pair := range pairs {
		key := routePairKey(pair)
		if _, ok := cache[key]; ok {
			continue
		}
		if _, ok := uniquePairs[key]; !ok {
			uniquePairs[key] = pair
		}
	}

	client := &http.Client{}
	jobs := make(chan routePairJob)
	results := make(chan struct {
		Key string
		Row routeCostRow
	})

	workerCount := min(cfg.concurrency, max(1, len(uniquePairs)))
	var wg sync.WaitGroup
	for range workerCount {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for job := range jobs {
				results <- struct {
					Key string
					Row routeCostRow
				}{Key: routePairKey(job.Pair), Row: routePairCostRow(client, cfg, job.Pair)}
			}
		}()
	}

	go func() {
		index := 0
		for _, pair := range uniquePairs {
			jobs <- routePairJob{Index: index, Pair: pair}
			index++
		}
		close(jobs)
		wg.Wait()
		close(results)
	}()

	for result := range results {
		cache[result.Key] = result.Row
	}

	rows := make([]routeCostRow, 0, len(pairs))
	for _, pair := range pairs {
		rows = append(rows, routePairCostRowFromCached(pair, cache[routePairKey(pair)]))
	}

	if cfg.routeCacheCSV != "" {
		if err := writeRouteCostRowsCSV(cfg.routeCacheCSV, sortedRouteCostRows(cache)); err != nil {
			return err
		}
	}
	return writeRouteCostRowsCSV(cfg.routeCostCSV, rows)
}

func routePairCostRow(client *http.Client, cfg config, pair routePair) routeCostRow {
	row := routeCostRow{
		BatchTime:    pair.BatchTime,
		TaxiID:       pair.TaxiID,
		RequestID:    pair.RequestID,
		LegType:      pair.LegType,
		StartLon:     pair.StartLon,
		StartLat:     pair.StartLat,
		EndLon:       pair.EndLon,
		EndLat:       pair.EndLat,
		PickupCost:   pair.PickupCost,
		DispatchCost: pair.DispatchCost,
	}
	if row.LegType == "" {
		row.LegType = "dispatch_to_pickup"
	}

	route, err := fetchRoute(client, cfg, []float64{pair.StartLon, pair.StartLat}, []float64{pair.EndLon, pair.EndLat})
	if err != nil {
		row.RouteStatus = "fallback"
		row.RouteError = err.Error()
		return row
	}
	row.RouteStatus = "routed"
	row.RouteDurationS = route.Duration
	row.RouteDistanceM = route.Distance
	return row
}

func routePairCostRowFromCached(pair routePair, cached routeCostRow) routeCostRow {
	row := cached
	row.BatchTime = pair.BatchTime
	row.TaxiID = pair.TaxiID
	row.RequestID = pair.RequestID
	row.LegType = pair.LegType
	if row.LegType == "" {
		row.LegType = "dispatch_to_pickup"
	}
	row.StartLon = pair.StartLon
	row.StartLat = pair.StartLat
	row.EndLon = pair.EndLon
	row.EndLat = pair.EndLat
	row.PickupCost = pair.PickupCost
	row.DispatchCost = pair.DispatchCost
	return row
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

func writeRouteCostCSV(path string, features []feature) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return fmt.Errorf("create route cost CSV directory: %w", err)
	}

	file, err := os.Create(path)
	if err != nil {
		return fmt.Errorf("create route cost CSV: %w", err)
	}
	defer file.Close()

	if _, err := fmt.Fprintln(file, "taxi_id,request_id,leg_type,route_status,route_duration_s,route_distance_m"); err != nil {
		return fmt.Errorf("write route cost CSV header: %w", err)
	}
	for _, feature := range features {
		props := feature.Properties
		if props == nil {
			continue
		}
		row := []string{
			propertyString(props, "taxi_id"),
			propertyString(props, "request_id"),
			propertyString(props, "leg_type"),
			propertyString(props, "route_status"),
			propertyString(props, "route_duration_s"),
			propertyString(props, "route_distance_m"),
		}
		if _, err := fmt.Fprintln(file, strings.Join(escapeCSVRow(row), ",")); err != nil {
			return fmt.Errorf("write route cost CSV row: %w", err)
		}
	}
	return nil
}

func writeRouteCostRowsCSV(path string, rows []routeCostRow) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return fmt.Errorf("create route cost CSV directory: %w", err)
	}

	file, err := os.Create(path)
	if err != nil {
		return fmt.Errorf("create route cost CSV: %w", err)
	}
	defer file.Close()

	writer := csv.NewWriter(file)
	header := []string{
		"batch_time",
		"taxi_id",
		"request_id",
		"leg_type",
		"route_status",
		"start_lon",
		"start_lat",
		"end_lon",
		"end_lat",
		"route_duration_s",
		"route_distance_m",
		"route_error",
		"pickup_cost",
		"dispatch_cost",
	}
	if err := writer.Write(header); err != nil {
		return fmt.Errorf("write route cost CSV header: %w", err)
	}
	for _, row := range rows {
		if err := writer.Write([]string{
			row.BatchTime,
			row.TaxiID,
			row.RequestID,
			row.LegType,
			row.RouteStatus,
			formatFloat(row.StartLon),
			formatFloat(row.StartLat),
			formatFloat(row.EndLon),
			formatFloat(row.EndLat),
			formatFloat(row.RouteDurationS),
			formatFloat(row.RouteDistanceM),
			row.RouteError,
			row.PickupCost,
			row.DispatchCost,
		}); err != nil {
			return fmt.Errorf("write route cost CSV row: %w", err)
		}
	}
	writer.Flush()
	if err := writer.Error(); err != nil {
		return fmt.Errorf("flush route cost CSV: %w", err)
	}
	return nil
}

func loadRouteCostRowsCSV(path string) ([]routeCostRow, error) {
	if _, err := os.Stat(path); errors.Is(err, os.ErrNotExist) {
		return nil, nil
	}
	file, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("read route cache CSV: %w", err)
	}
	defer file.Close()

	reader := csv.NewReader(file)
	reader.FieldsPerRecord = -1
	reader.TrimLeadingSpace = true
	header, err := reader.Read()
	if err != nil {
		return nil, fmt.Errorf("read route cache CSV header: %w", err)
	}
	columns := make(map[string]int, len(header))
	for index, name := range header {
		columns[normalizeCSVHeader(name)] = index
	}

	records, err := reader.ReadAll()
	if err != nil {
		return nil, fmt.Errorf("read route cache CSV rows: %w", err)
	}
	rows := make([]routeCostRow, 0, len(records))
	for rowIndex, record := range records {
		if len(record) == 0 {
			continue
		}
		row, err := parseRouteCostRow(record, columns)
		if err != nil {
			return nil, fmt.Errorf("route cache CSV row %d: %w", rowIndex+2, err)
		}
		rows = append(rows, row)
	}
	return rows, nil
}

func parseRouteCostRow(record []string, columns map[string]int) (routeCostRow, error) {
	startLon, err := requiredFloat(record, columns, "start_lon")
	if err != nil {
		return routeCostRow{}, err
	}
	startLat, err := requiredFloat(record, columns, "start_lat")
	if err != nil {
		return routeCostRow{}, err
	}
	endLon, err := requiredFloat(record, columns, "end_lon")
	if err != nil {
		return routeCostRow{}, err
	}
	endLat, err := requiredFloat(record, columns, "end_lat")
	if err != nil {
		return routeCostRow{}, err
	}
	duration, err := requiredFloat(record, columns, "route_duration_s")
	if err != nil {
		return routeCostRow{}, err
	}
	distance, err := requiredFloat(record, columns, "route_distance_m")
	if err != nil {
		return routeCostRow{}, err
	}
	return routeCostRow{
		BatchTime:      optionalString(record, columns, "batch_time"),
		TaxiID:         optionalString(record, columns, "taxi_id"),
		RequestID:      optionalString(record, columns, "request_id"),
		LegType:        optionalString(record, columns, "leg_type"),
		RouteStatus:    optionalString(record, columns, "route_status"),
		StartLon:       startLon,
		StartLat:       startLat,
		EndLon:         endLon,
		EndLat:         endLat,
		RouteDurationS: duration,
		RouteDistanceM: distance,
		RouteError:     optionalString(record, columns, "route_error"),
		PickupCost:     optionalString(record, columns, "pickup_cost"),
		DispatchCost:   optionalString(record, columns, "dispatch_cost"),
	}, nil
}

func loadRoutePairsCSV(path string) ([]routePair, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("read route pairs CSV: %w", err)
	}
	defer file.Close()

	reader := csv.NewReader(file)
	reader.FieldsPerRecord = -1
	reader.TrimLeadingSpace = true
	header, err := reader.Read()
	if err != nil {
		return nil, fmt.Errorf("read route pairs CSV header: %w", err)
	}
	columns := make(map[string]int, len(header))
	for index, name := range header {
		columns[normalizeCSVHeader(name)] = index
	}

	records, err := reader.ReadAll()
	if err != nil {
		return nil, fmt.Errorf("read route pairs CSV rows: %w", err)
	}
	pairs := make([]routePair, 0, len(records))
	for rowIndex, record := range records {
		if len(record) == 0 {
			continue
		}
		pair, err := parseRoutePair(record, columns)
		if err != nil {
			return nil, fmt.Errorf("route pairs CSV row %d: %w", rowIndex+2, err)
		}
		pairs = append(pairs, pair)
	}
	return pairs, nil
}

func parseRoutePair(record []string, columns map[string]int) (routePair, error) {
	startLon, err := requiredFloat(record, columns, "start_lon")
	if err != nil {
		return routePair{}, err
	}
	startLat, err := requiredFloat(record, columns, "start_lat")
	if err != nil {
		return routePair{}, err
	}
	endLon, err := requiredFloat(record, columns, "end_lon")
	if err != nil {
		return routePair{}, err
	}
	endLat, err := requiredFloat(record, columns, "end_lat")
	if err != nil {
		return routePair{}, err
	}
	taxiID := requiredString(record, columns, "taxi_id")
	if taxiID == "" {
		return routePair{}, errors.New("missing taxi_id")
	}
	requestID := requiredString(record, columns, "request_id")
	if requestID == "" {
		return routePair{}, errors.New("missing request_id")
	}
	return routePair{
		BatchTime:    optionalString(record, columns, "batch_time"),
		TaxiID:       taxiID,
		RequestID:    requestID,
		LegType:      optionalString(record, columns, "leg_type"),
		StartLon:     startLon,
		StartLat:     startLat,
		EndLon:       endLon,
		EndLat:       endLat,
		PickupCost:   optionalString(record, columns, "pickup_cost"),
		DispatchCost: optionalString(record, columns, "dispatch_cost"),
	}, nil
}

func normalizeCSVHeader(name string) string {
	return strings.ToLower(strings.ReplaceAll(strings.TrimSpace(name), " ", "_"))
}

func requiredString(record []string, columns map[string]int, name string) string {
	value, ok := optionalCSVValue(record, columns, name)
	if !ok {
		return ""
	}
	return value
}

func optionalString(record []string, columns map[string]int, name string) string {
	value, ok := optionalCSVValue(record, columns, name)
	if !ok {
		return ""
	}
	return value
}

func optionalCSVValue(record []string, columns map[string]int, name string) (string, bool) {
	index, ok := columns[name]
	if !ok || index < 0 || index >= len(record) {
		return "", false
	}
	return strings.TrimSpace(record[index]), true
}

func requiredFloat(record []string, columns map[string]int, name string) (float64, error) {
	value, ok := optionalCSVValue(record, columns, name)
	if !ok || value == "" {
		return 0, fmt.Errorf("missing %s", name)
	}
	parsed, err := strconv.ParseFloat(value, 64)
	if err != nil {
		return 0, fmt.Errorf("invalid %s: %w", name, err)
	}
	return parsed, nil
}

func formatFloat(value float64) string {
	return strconv.FormatFloat(value, 'f', -1, 64)
}

func routePairKey(pair routePair) string {
	return routeCoordinateKey(pair.StartLon, pair.StartLat, pair.EndLon, pair.EndLat)
}

func routeCostRowKey(row routeCostRow) string {
	return routeCoordinateKey(row.StartLon, row.StartLat, row.EndLon, row.EndLat)
}

func routeCoordinateKey(startLon float64, startLat float64, endLon float64, endLat float64) string {
	values := []float64{startLon, startLat, endLon, endLat}
	parts := make([]string, len(values))
	for index, value := range values {
		parts[index] = strconv.FormatFloat(value, 'g', 17, 64)
	}
	return strings.Join(parts, ",")
}

func sortedRouteCostRows(rowsByKey map[string]routeCostRow) []routeCostRow {
	keys := make([]string, 0, len(rowsByKey))
	for key := range rowsByKey {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	rows := make([]routeCostRow, 0, len(keys))
	for _, key := range keys {
		rows = append(rows, rowsByKey[key])
	}
	return rows
}

func propertyString(properties map[string]any, name string) string {
	value, ok := properties[name]
	if !ok || value == nil {
		return ""
	}
	return fmt.Sprint(value)
}

func escapeCSVRow(row []string) []string {
	escaped := make([]string, len(row))
	for index, value := range row {
		if strings.ContainsAny(value, "\",\n\r") {
			value = `"` + strings.ReplaceAll(value, `"`, `""`) + `"`
		}
		escaped[index] = value
	}
	return escaped
}
