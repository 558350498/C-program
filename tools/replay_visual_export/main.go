package main

import (
	"encoding/csv"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"math"
	"math/rand"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
)

const (
	schemaVersion             = 1
	defaultBatchWindowSeconds = 600
	defaultSampleOrderCount   = 12
	defaultSampleSeed         = 20260510
	defaultFarePerKm          = 1.0
	defaultPickupCostPerKm    = 1.0
	defaultKmPerDegree        = 111.0
	defaultPickupHotWeight    = 0.15
	defaultColdDropoffPenalty = 0.20
	defaultHotDropoffDiscount = 0.10
	defaultPriceFloor         = 0.8
	defaultPriceCap           = 1.8
	defaultPricingMode        = "linear"
)

type config struct {
	requestsPath           string
	driversPath            string
	requestOutcomesPath    string
	batchLogsPath          string
	outputDir              string
	liveThreshold          int
	batchWindowSeconds     int
	mode                   string
	sampleOrderCount       int
	sampleSeed             int64
	tileStatsPath          string
	farePerKm              float64
	pickupCostPerKm        float64
	kmPerDegree            float64
	secondsPerDistanceUnit float64
	pickupHotWeight        float64
	coldDropoffPenalty     float64
	hotDropoffDiscount     float64
	priceFloor             float64
	priceCap               float64
	pricingMode            string
}

type requestRow struct {
	RequestID   string
	CustomerID  string
	RequestTime int
	PickupX     float64
	PickupY     float64
	DropoffX    float64
	DropoffY    float64
	PickupTile  string
	DropoffTile string
}

type driverRow struct {
	TaxiID        string
	X             float64
	Y             float64
	Tile          string
	AvailableTime int
	Status        string
}

type outcomeRow struct {
	RequestID           string
	PendingBatchCount   int
	CandidateBatchCount int
	CandidateEdgeCount  int
	HasCandidateEdge    bool
	Assigned            bool
	Completed           bool
	TaxiID              string
	AssignmentTime      int
	PickupTime          int
	CompletionTime      int
	WaitTime            int
	PickupCost          int
}

type batchLogRow struct {
	BatchTime           int `json:"batch_time"`
	AvailableDrivers    int `json:"available_drivers"`
	PendingRequests     int `json:"pending_requests"`
	CandidateEdges      int `json:"candidate_edges"`
	AppliedAssignments  int `json:"applied_assignments"`
	AssignedCumulative  int `json:"assigned_cumulative"`
	CompletedCumulative int `json:"completed_cumulative"`
}

type replayBatchTileFrame struct {
	BatchTime       int                        `json:"batch_time"`
	WindowStartTime int                        `json:"window_start_time"`
	WindowSeconds   int                        `json:"window_seconds"`
	Tiles           []replayBatchTileActivity  `json:"tiles"`
	Totals          replayBatchTileActivitySum `json:"totals"`
}

type replayBatchTileActivity struct {
	TileID         int `json:"tile_id"`
	PickupCount    int `json:"pickup_count"`
	AssignedCount  int `json:"assigned_count"`
	CompletedCount int `json:"completed_count"`
	ActivityScore  int `json:"activity_score"`
}

type replayBatchTileActivitySum struct {
	ActiveTiles    int `json:"active_tiles"`
	PickupCount    int `json:"pickup_count"`
	AssignedCount  int `json:"assigned_count"`
	CompletedCount int `json:"completed_count"`
	ActivityScore  int `json:"activity_score"`
}

type replayManifest struct {
	SchemaVersion      int               `json:"schema_version"`
	Mode               string            `json:"mode"`
	RequestedMode      string            `json:"requested_mode"`
	LiveThreshold      int               `json:"live_threshold"`
	BatchWindowSeconds int               `json:"batch_window_seconds,omitempty"`
	RequestCount       int               `json:"request_count"`
	DriverCount        int               `json:"driver_count"`
	OutcomeCount       int               `json:"outcome_count"`
	BatchCount         int               `json:"batch_count"`
	AssignedCount      int               `json:"assigned_count"`
	CompletedCount     int               `json:"completed_count"`
	GeneratedFiles     []string          `json:"generated_files"`
	Inputs             map[string]string `json:"inputs"`
}

type tileStatsRow struct {
	TileID       string
	HotspotScore float64
	ColdScore    float64
}

type pricingParams struct {
	FarePerKm              float64
	PickupCostPerKm        float64
	KmPerDegree            float64
	SecondsPerDistanceUnit float64
	PickupHotWeight        float64
	ColdDropoffPenalty     float64
	HotDropoffDiscount     float64
	PriceFloor             float64
	PriceCap               float64
	Mode                   string
}

type pricingExplanation struct {
	Mode                   string   `json:"mode"`
	BaseRevenue            float64  `json:"base_revenue"`
	PriceFactor            float64  `json:"price_factor"`
	PickupHotspotComponent *float64 `json:"pickup_hotspot_component"`
	ColdDropoffComponent   *float64 `json:"cold_dropoff_component"`
	HotDropoffComponent    *float64 `json:"hot_dropoff_component"`
	EstimatedRevenue       float64  `json:"estimated_revenue"`
	EstimatedPickupKm      float64  `json:"estimated_pickup_km"`
	EstimatedPickupCost    float64  `json:"estimated_pickup_cost"`
	EstimatedNet           float64  `json:"estimated_net"`
}

type sampleOrderExplanation struct {
	RequestID             string             `json:"request_id"`
	TaxiID                string             `json:"taxi_id"`
	Status                string             `json:"status"`
	ReasonTags            []string           `json:"reason_tags"`
	RequestTime           int                `json:"request_time"`
	AssignmentTime        int                `json:"assignment_time"`
	PickupTime            int                `json:"pickup_time"`
	CompletionTime        int                `json:"completion_time"`
	WaitTime              int                `json:"wait_time"`
	PickupCost            int                `json:"pickup_cost"`
	PendingBatchCount     int                `json:"pending_batch_count"`
	CandidateBatchCount   int                `json:"candidate_batch_count"`
	CandidateEdgeCount    int                `json:"candidate_edge_count"`
	HasCandidateEdge      bool               `json:"has_candidate_edge"`
	Pickup                samplePoint        `json:"pickup"`
	Dropoff               samplePoint        `json:"dropoff"`
	TripDistance          float64            `json:"trip_distance"`
	PickupHotspotScore    *float64           `json:"pickup_hotspot_score"`
	PickupColdScore       *float64           `json:"pickup_cold_score"`
	DropoffHotspotScore   *float64           `json:"dropoff_hotspot_score"`
	DropoffColdScore      *float64           `json:"dropoff_cold_score"`
	OpportunityAdjustment *float64           `json:"opportunity_adjustment"`
	Pricing               pricingExplanation `json:"pricing"`
}

type samplePoint struct {
	X    float64 `json:"x"`
	Y    float64 `json:"y"`
	Tile string  `json:"tile"`
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
	Type        string `json:"type"`
	Coordinates any    `json:"coordinates"`
}

type taxiPosition struct {
	X float64
	Y float64
}

type tileActivityEvent struct {
	Time   int
	TileID int
	Kind   string
}

func main() {
	cfg := config{}
	flag.StringVar(&cfg.requestsPath, "requests", "", "Path to normalized requests.csv")
	flag.StringVar(&cfg.driversPath, "drivers", "", "Path to normalized drivers.csv")
	flag.StringVar(&cfg.requestOutcomesPath, "request-outcomes", "", "Path to request_outcomes.csv")
	flag.StringVar(&cfg.batchLogsPath, "batch-logs", "", "Path to batch_logs.csv")
	flag.StringVar(&cfg.outputDir, "output-dir", "", "Directory for replay artifacts")
	flag.IntVar(&cfg.liveThreshold, "live-threshold", 1000, "Max request count for auto live mode")
	flag.IntVar(&cfg.batchWindowSeconds, "batch-window-seconds", defaultBatchWindowSeconds, "Sliding window in seconds for batch tile activity")
	flag.StringVar(&cfg.mode, "mode", "auto", "Replay export mode: auto, live, or batch")
	flag.IntVar(&cfg.sampleOrderCount, "sample-order-count", defaultSampleOrderCount, "Representative order explanations to write; 0 disables sampled_order_explanations.json")
	flag.Int64Var(&cfg.sampleSeed, "sample-seed", defaultSampleSeed, "Random seed for reproducible sample order fill")
	flag.StringVar(&cfg.tileStatsPath, "tile-stats", "", "Optional tile_stats.csv path for hot/cold explanation fields")
	flag.Float64Var(&cfg.farePerKm, "fare-per-km", defaultFarePerKm, "fare revenue per completed trip kilometer for sampled pricing")
	flag.Float64Var(&cfg.pickupCostPerKm, "pickup-cost-per-km", defaultPickupCostPerKm, "cost per pickup/deadhead kilometer for sampled pricing")
	flag.Float64Var(&cfg.kmPerDegree, "km-per-degree", defaultKmPerDegree, "rough conversion from replay degree distance to kilometers for sampled pricing")
	flag.Float64Var(&cfg.secondsPerDistanceUnit, "seconds-per-distance-unit", 100000.0, "pickup cost scale used by replay for sampled pricing")
	flag.Float64Var(&cfg.pickupHotWeight, "pickup-hot-weight", defaultPickupHotWeight, "fixed pricing pickup hotspot weight")
	flag.Float64Var(&cfg.coldDropoffPenalty, "cold-dropoff-penalty", defaultColdDropoffPenalty, "Opportunity cold dropoff penalty for sampled order explanations")
	flag.Float64Var(&cfg.hotDropoffDiscount, "hot-dropoff-discount", defaultHotDropoffDiscount, "Opportunity hot dropoff discount for sampled order explanations")
	flag.Float64Var(&cfg.priceFloor, "price-floor", defaultPriceFloor, "minimum sampled pricing factor")
	flag.Float64Var(&cfg.priceCap, "price-cap", defaultPriceCap, "maximum sampled pricing factor")
	flag.StringVar(&cfg.pricingMode, "pricing-mode", defaultPricingMode, "sampled pricing mode: linear or diminishing")
	flag.Parse()

	if err := runExport(cfg); err != nil {
		fmt.Fprintf(os.Stderr, "replay_visual_export: %v\n", err)
		os.Exit(1)
	}
}

func runExport(cfg config) error {
	cfg = withDefaults(cfg)
	if err := validateConfig(cfg); err != nil {
		return err
	}

	requests, err := loadRequests(cfg.requestsPath)
	if err != nil {
		return err
	}
	drivers, err := loadDrivers(cfg.driversPath)
	if err != nil {
		return err
	}
	outcomes, err := loadOutcomes(cfg.requestOutcomesPath)
	if err != nil {
		return err
	}
	batches, err := loadBatchLogs(cfg.batchLogsPath)
	if err != nil {
		return err
	}
	tileStats, err := loadOptionalTileStats(cfg.tileStatsPath)
	if err != nil {
		return err
	}

	mode, err := chooseMode(cfg.mode, len(requests), cfg.liveThreshold)
	if err != nil {
		return err
	}

	if err := os.MkdirAll(cfg.outputDir, 0o755); err != nil {
		return fmt.Errorf("create output dir: %w", err)
	}

	assignedCount, completedCount := countOutcomes(outcomes)
	files := []string{"replay_manifest.json"}
	if mode == "live" {
		if err := removeGenerated(cfg.outputDir, "replay_batches.json", "replay_batch_tiles.json"); err != nil {
			return err
		}
		paths, points, err := buildLiveArtifacts(requests, drivers, outcomes)
		if err != nil {
			return err
		}
		if err := writeJSON(filepath.Join(cfg.outputDir, "replay_live_paths.geojson"), paths); err != nil {
			return err
		}
		if err := writeJSON(filepath.Join(cfg.outputDir, "replay_live_points.geojson"), points); err != nil {
			return err
		}
		files = append(files, "replay_live_paths.geojson", "replay_live_points.geojson")
	} else {
		if err := removeGenerated(cfg.outputDir, "replay_live_paths.geojson", "replay_live_points.geojson"); err != nil {
			return err
		}
		replayBatches := buildBatchArtifacts(batches, outcomes)
		if err := writeJSON(filepath.Join(cfg.outputDir, "replay_batches.json"), replayBatches); err != nil {
			return err
		}
		replayBatchTiles, err := buildBatchTileArtifacts(batches, requests, outcomes, cfg.batchWindowSeconds)
		if err != nil {
			return err
		}
		if err := writeJSON(filepath.Join(cfg.outputDir, "replay_batch_tiles.json"), replayBatchTiles); err != nil {
			return err
		}
		files = append(files, "replay_batches.json", "replay_batch_tiles.json")
	}
	if cfg.sampleOrderCount > 0 {
		pricing := pricingParams{
			FarePerKm:              cfg.farePerKm,
			PickupCostPerKm:        cfg.pickupCostPerKm,
			KmPerDegree:            cfg.kmPerDegree,
			SecondsPerDistanceUnit: cfg.secondsPerDistanceUnit,
			PickupHotWeight:        cfg.pickupHotWeight,
			ColdDropoffPenalty:     cfg.coldDropoffPenalty,
			HotDropoffDiscount:     cfg.hotDropoffDiscount,
			PriceFloor:             cfg.priceFloor,
			PriceCap:               cfg.priceCap,
			Mode:                   cfg.pricingMode,
		}
		samples := buildSampleOrderExplanations(requests, outcomes, tileStats, cfg.sampleOrderCount, cfg.sampleSeed, pricing)
		if err := writeJSON(filepath.Join(cfg.outputDir, "sampled_order_explanations.json"), samples); err != nil {
			return err
		}
		files = append(files, "sampled_order_explanations.json")
	} else if err := removeGenerated(cfg.outputDir, "sampled_order_explanations.json"); err != nil {
		return err
	}

	manifest := replayManifest{
		SchemaVersion:      schemaVersion,
		Mode:               mode,
		RequestedMode:      cfg.mode,
		LiveThreshold:      cfg.liveThreshold,
		BatchWindowSeconds: cfg.batchWindowSeconds,
		RequestCount:       len(requests),
		DriverCount:        len(drivers),
		OutcomeCount:       len(outcomes),
		BatchCount:         len(batches),
		AssignedCount:      assignedCount,
		CompletedCount:     completedCount,
		GeneratedFiles:     files,
		Inputs: map[string]string{
			"requests":         cfg.requestsPath,
			"drivers":          cfg.driversPath,
			"request_outcomes": cfg.requestOutcomesPath,
			"batch_logs":       cfg.batchLogsPath,
			"tile_stats":       cfg.tileStatsPath,
		},
	}
	return writeJSON(filepath.Join(cfg.outputDir, "replay_manifest.json"), manifest)
}

func withDefaults(cfg config) config {
	if cfg.batchWindowSeconds == 0 {
		cfg.batchWindowSeconds = defaultBatchWindowSeconds
	}
	if cfg.sampleSeed == 0 {
		cfg.sampleSeed = defaultSampleSeed
	}
	if cfg.pricingMode == "" {
		cfg.pricingMode = defaultPricingMode
	}
	return cfg
}

func validateConfig(cfg config) error {
	var missing []string
	if cfg.requestsPath == "" {
		missing = append(missing, "-requests")
	}
	if cfg.driversPath == "" {
		missing = append(missing, "-drivers")
	}
	if cfg.requestOutcomesPath == "" {
		missing = append(missing, "-request-outcomes")
	}
	if cfg.batchLogsPath == "" {
		missing = append(missing, "-batch-logs")
	}
	if cfg.outputDir == "" {
		missing = append(missing, "-output-dir")
	}
	if len(missing) > 0 {
		return fmt.Errorf("missing required flags: %s", strings.Join(missing, ", "))
	}
	if cfg.liveThreshold < 0 {
		return errors.New("-live-threshold must be non-negative")
	}
	if cfg.batchWindowSeconds <= 0 {
		return errors.New("-batch-window-seconds must be positive")
	}
	if cfg.sampleOrderCount < 0 {
		return errors.New("-sample-order-count must be non-negative")
	}
	if cfg.farePerKm < 0 || math.IsNaN(cfg.farePerKm) || math.IsInf(cfg.farePerKm, 0) ||
		cfg.pickupCostPerKm < 0 || math.IsNaN(cfg.pickupCostPerKm) || math.IsInf(cfg.pickupCostPerKm, 0) {
		return errors.New("fare and cost values must be non-negative")
	}
	if cfg.kmPerDegree <= 0 || math.IsNaN(cfg.kmPerDegree) || math.IsInf(cfg.kmPerDegree, 0) {
		return errors.New("-km-per-degree must be positive")
	}
	if cfg.secondsPerDistanceUnit <= 0 || math.IsNaN(cfg.secondsPerDistanceUnit) || math.IsInf(cfg.secondsPerDistanceUnit, 0) {
		return errors.New("-seconds-per-distance-unit must be positive")
	}
	if cfg.pickupHotWeight < 0 || math.IsNaN(cfg.pickupHotWeight) || math.IsInf(cfg.pickupHotWeight, 0) {
		return errors.New("-pickup-hot-weight must be non-negative")
	}
	if cfg.coldDropoffPenalty < 0 || math.IsNaN(cfg.coldDropoffPenalty) || math.IsInf(cfg.coldDropoffPenalty, 0) {
		return errors.New("-cold-dropoff-penalty must be non-negative")
	}
	if cfg.hotDropoffDiscount < 0 || math.IsNaN(cfg.hotDropoffDiscount) || math.IsInf(cfg.hotDropoffDiscount, 0) {
		return errors.New("-hot-dropoff-discount must be non-negative")
	}
	if cfg.priceFloor <= 0 || math.IsNaN(cfg.priceFloor) || math.IsInf(cfg.priceFloor, 0) {
		return errors.New("-price-floor must be positive")
	}
	if cfg.priceCap < cfg.priceFloor || math.IsNaN(cfg.priceCap) || math.IsInf(cfg.priceCap, 0) {
		return errors.New("-price-cap must be greater than or equal to -price-floor")
	}
	if cfg.pricingMode != "linear" && cfg.pricingMode != "diminishing" {
		return errors.New("-pricing-mode must be linear or diminishing")
	}
	return nil
}

func chooseMode(requested string, requestCount int, liveThreshold int) (string, error) {
	switch requested {
	case "live", "batch":
		return requested, nil
	case "auto":
		if requestCount <= liveThreshold {
			return "live", nil
		}
		return "batch", nil
	default:
		return "", fmt.Errorf("invalid -mode %q; want auto, live, or batch", requested)
	}
}

func buildLiveArtifacts(requests map[string]requestRow, drivers map[string]driverRow, outcomes []outcomeRow) (featureCollection, featureCollection, error) {
	taxiPositions := make(map[string]taxiPosition, len(drivers))
	for taxiID, driver := range drivers {
		taxiPositions[taxiID] = taxiPosition{X: driver.X, Y: driver.Y}
	}

	sorted := append([]outcomeRow(nil), outcomes...)
	sort.SliceStable(sorted, func(i, j int) bool {
		if sorted[i].AssignmentTime != sorted[j].AssignmentTime {
			return sorted[i].AssignmentTime < sorted[j].AssignmentTime
		}
		if sorted[i].PickupTime != sorted[j].PickupTime {
			return sorted[i].PickupTime < sorted[j].PickupTime
		}
		return sorted[i].RequestID < sorted[j].RequestID
	})

	paths := featureCollection{Type: "FeatureCollection"}
	points := featureCollection{Type: "FeatureCollection"}

	for _, outcome := range sorted {
		if !outcome.Assigned || outcome.TaxiID == "" || outcome.TaxiID == "-1" {
			continue
		}
		req, ok := requests[outcome.RequestID]
		if !ok {
			return paths, points, fmt.Errorf("outcome request_id %s not found in requests", outcome.RequestID)
		}
		pos, ok := taxiPositions[outcome.TaxiID]
		if !ok {
			return paths, points, fmt.Errorf("outcome taxi_id %s not found in drivers", outcome.TaxiID)
		}
		if outcome.PickupTime < outcome.AssignmentTime {
			return paths, points, fmt.Errorf("request %s has pickup_time before assignment_time", outcome.RequestID)
		}
		if outcome.Completed && outcome.CompletionTime < outcome.PickupTime {
			return paths, points, fmt.Errorf("request %s has completion_time before pickup_time", outcome.RequestID)
		}

		paths.Features = append(paths.Features, lineFeature(
			[][]float64{{pos.X, pos.Y}, {req.PickupX, req.PickupY}},
			outcome, "dispatch_to_pickup", outcome.AssignmentTime, outcome.PickupTime,
		))
		points.Features = append(points.Features, pointFeature(req.PickupX, req.PickupY, req, outcome, "pickup", outcome.PickupTime))

		if outcome.Completed {
			paths.Features = append(paths.Features, lineFeature(
				[][]float64{{req.PickupX, req.PickupY}, {req.DropoffX, req.DropoffY}},
				outcome, "pickup_to_dropoff", outcome.PickupTime, outcome.CompletionTime,
			))
			points.Features = append(points.Features, pointFeature(req.DropoffX, req.DropoffY, req, outcome, "dropoff", outcome.CompletionTime))
			taxiPositions[outcome.TaxiID] = taxiPosition{X: req.DropoffX, Y: req.DropoffY}
		} else {
			taxiPositions[outcome.TaxiID] = taxiPosition{X: req.PickupX, Y: req.PickupY}
		}
	}

	return paths, points, nil
}

func lineFeature(coords [][]float64, outcome outcomeRow, legType string, startTime int, endTime int) feature {
	return feature{
		Type: "Feature",
		Geometry: geometry{
			Type:        "LineString",
			Coordinates: coords,
		},
		Properties: map[string]any{
			"taxi_id":    outcome.TaxiID,
			"request_id": outcome.RequestID,
			"start_time": startTime,
			"end_time":   endTime,
			"leg_type":   legType,
		},
	}
}

func pointFeature(x float64, y float64, req requestRow, outcome outcomeRow, pointType string, eventTime int) feature {
	return feature{
		Type: "Feature",
		Geometry: geometry{
			Type:        "Point",
			Coordinates: []float64{x, y},
		},
		Properties: map[string]any{
			"point_type":   pointType,
			"event_time":   eventTime,
			"taxi_id":      outcome.TaxiID,
			"request_id":   req.RequestID,
			"customer_id":  req.CustomerID,
			"request_time": req.RequestTime,
			"pickup_tile":  req.PickupTile,
			"dropoff_tile": req.DropoffTile,
		},
	}
}

func buildBatchArtifacts(batches []batchLogRow, outcomes []outcomeRow) []batchLogRow {
	result := append([]batchLogRow(nil), batches...)
	sort.SliceStable(result, func(i, j int) bool {
		return result[i].BatchTime < result[j].BatchTime
	})

	var assignmentTimes []int
	var completionTimes []int
	for _, outcome := range outcomes {
		if outcome.Assigned {
			assignmentTimes = append(assignmentTimes, outcome.AssignmentTime)
		}
		if outcome.Completed {
			completionTimes = append(completionTimes, outcome.CompletionTime)
		}
	}
	sort.Ints(assignmentTimes)
	sort.Ints(completionTimes)

	assignedIdx := 0
	completedIdx := 0
	for i := range result {
		for assignedIdx < len(assignmentTimes) && assignmentTimes[assignedIdx] <= result[i].BatchTime {
			assignedIdx++
		}
		for completedIdx < len(completionTimes) && completionTimes[completedIdx] <= result[i].BatchTime {
			completedIdx++
		}
		result[i].AssignedCumulative = assignedIdx
		result[i].CompletedCumulative = completedIdx
	}
	return result
}

func buildBatchTileArtifacts(batches []batchLogRow, requests map[string]requestRow, outcomes []outcomeRow, windowSeconds int) ([]replayBatchTileFrame, error) {
	sortedBatches := append([]batchLogRow(nil), batches...)
	sort.SliceStable(sortedBatches, func(i, j int) bool {
		return sortedBatches[i].BatchTime < sortedBatches[j].BatchTime
	})

	events, err := buildTileActivityEvents(requests, outcomes)
	if err != nil {
		return nil, err
	}
	sort.SliceStable(events, func(i, j int) bool {
		if events[i].Time != events[j].Time {
			return events[i].Time < events[j].Time
		}
		if events[i].TileID != events[j].TileID {
			return events[i].TileID < events[j].TileID
		}
		return events[i].Kind < events[j].Kind
	})

	activityByTile := make(map[int]*replayBatchTileActivity)
	addIdx := 0
	removeIdx := 0
	frames := make([]replayBatchTileFrame, 0, len(sortedBatches))

	for _, batch := range sortedBatches {
		windowStart := batch.BatchTime - windowSeconds
		for addIdx < len(events) && events[addIdx].Time <= batch.BatchTime {
			applyTileActivity(activityByTile, events[addIdx], 1)
			addIdx++
		}
		for removeIdx < addIdx && events[removeIdx].Time < windowStart {
			applyTileActivity(activityByTile, events[removeIdx], -1)
			removeIdx++
		}
		tiles, totals := snapshotTileActivity(activityByTile)
		frames = append(frames, replayBatchTileFrame{
			BatchTime:       batch.BatchTime,
			WindowStartTime: windowStart,
			WindowSeconds:   windowSeconds,
			Tiles:           tiles,
			Totals:          totals,
		})
	}
	return frames, nil
}

func buildSampleOrderExplanations(requests map[string]requestRow, outcomes []outcomeRow, tileStats map[string]tileStatsRow, sampleCount int, sampleSeed int64, pricing pricingParams) []sampleOrderExplanation {
	if sampleCount <= 0 {
		return nil
	}

	eligible := make([]outcomeRow, 0, len(outcomes))
	for _, outcome := range outcomes {
		if _, ok := requests[outcome.RequestID]; ok {
			eligible = append(eligible, outcome)
		}
	}
	sort.SliceStable(eligible, func(i, j int) bool {
		left := requests[eligible[i].RequestID]
		right := requests[eligible[j].RequestID]
		if left.RequestTime != right.RequestTime {
			return left.RequestTime < right.RequestTime
		}
		return eligible[i].RequestID < eligible[j].RequestID
	})

	selected := make([]outcomeRow, 0, sampleCount)
	reasons := make(map[string][]string)
	seen := make(map[string]bool)
	add := func(outcome outcomeRow, reason string) {
		if outcome.RequestID == "" {
			return
		}
		reasons[outcome.RequestID] = appendUnique(reasons[outcome.RequestID], reason)
		if seen[outcome.RequestID] || len(selected) >= sampleCount {
			return
		}
		seen[outcome.RequestID] = true
		selected = append(selected, outcome)
	}
	addFirst := func(reason string, matches func(outcomeRow) bool) {
		for _, outcome := range eligible {
			if matches(outcome) {
				add(outcome, reason)
				return
			}
		}
	}
	addMax := func(reason string, score func(outcomeRow) (float64, bool)) {
		bestIndex := -1
		bestScore := math.Inf(-1)
		for index, outcome := range eligible {
			value, ok := score(outcome)
			if !ok {
				continue
			}
			if value > bestScore {
				bestScore = value
				bestIndex = index
			}
		}
		if bestIndex >= 0 {
			add(eligible[bestIndex], reason)
		}
	}

	if hotOutcome, coldOutcome, ok := distanceMatchedDropoffContrast(eligible, requests, tileStats); ok {
		add(hotOutcome, "distance_matched_hot_dropoff")
		add(hotOutcome, "comparison_pair")
		add(coldOutcome, "distance_matched_cold_dropoff")
		add(coldOutcome, "comparison_pair")
	}
	addFirst("completed", func(outcome outcomeRow) bool { return outcome.Completed })
	addFirst("assigned_incomplete", func(outcome outcomeRow) bool { return outcome.Assigned && !outcome.Completed })
	addFirst("unserved", func(outcome outcomeRow) bool { return !outcome.Assigned })
	addFirst("no_candidate_edge", func(outcome outcomeRow) bool { return !outcome.HasCandidateEdge })
	addMax("high_wait", func(outcome outcomeRow) (float64, bool) {
		return float64(outcome.WaitTime), outcome.Assigned
	})
	addMax("high_pickup_cost", func(outcome outcomeRow) (float64, bool) {
		return float64(outcome.PickupCost), outcome.Assigned
	})
	addMax("hot_dropoff", func(outcome outcomeRow) (float64, bool) {
		req := requests[outcome.RequestID]
		stats, ok := tileStats[req.DropoffTile]
		return stats.HotspotScore, ok
	})
	addMax("cold_dropoff", func(outcome outcomeRow) (float64, bool) {
		req := requests[outcome.RequestID]
		stats, ok := tileStats[req.DropoffTile]
		return stats.ColdScore, ok
	})

	remaining := make([]outcomeRow, 0, len(eligible))
	for _, outcome := range eligible {
		if !seen[outcome.RequestID] {
			remaining = append(remaining, outcome)
		}
	}
	rng := rand.New(rand.NewSource(sampleSeed))
	rng.Shuffle(len(remaining), func(i, j int) {
		remaining[i], remaining[j] = remaining[j], remaining[i]
	})
	for _, outcome := range remaining {
		add(outcome, "seeded_fill")
		if len(selected) >= sampleCount {
			break
		}
	}

	samples := make([]sampleOrderExplanation, 0, len(selected))
	for _, outcome := range selected {
		req := requests[outcome.RequestID]
		samples = append(samples, sampleExplanation(req, outcome, reasons[outcome.RequestID], tileStats, pricing))
	}
	return samples
}

func distanceMatchedDropoffContrast(outcomes []outcomeRow, requests map[string]requestRow, tileStats map[string]tileStatsRow) (outcomeRow, outcomeRow, bool) {
	if len(tileStats) == 0 {
		return outcomeRow{}, outcomeRow{}, false
	}

	type candidate struct {
		outcome outcomeRow
		req     requestRow
		dist    float64
		hot     float64
	}

	candidates := make([]candidate, 0, len(outcomes))
	for _, outcome := range outcomes {
		if !outcome.Assigned || !outcome.HasCandidateEdge {
			continue
		}
		req, ok := requests[outcome.RequestID]
		if !ok {
			continue
		}
		stats, ok := tileStats[req.DropoffTile]
		if !ok {
			continue
		}
		dist := tripDistance(req)
		if dist <= 0 || math.IsNaN(dist) || math.IsInf(dist, 0) {
			continue
		}
		candidates = append(candidates, candidate{
			outcome: outcome,
			req:     req,
			dist:    dist,
			hot:     stats.HotspotScore,
		})
	}

	bestScore := math.Inf(-1)
	bestLeft := -1
	bestRight := -1
	for i := 0; i < len(candidates); i++ {
		for j := i + 1; j < len(candidates); j++ {
			left := candidates[i]
			right := candidates[j]
			hotDiff := math.Abs(left.hot - right.hot)
			if hotDiff < 0.35 {
				continue
			}
			avgDist := (left.dist + right.dist) / 2
			relativeDistDiff := math.Abs(left.dist-right.dist) / avgDist
			if relativeDistDiff > 0.20 {
				continue
			}
			score := hotDiff - relativeDistDiff
			if left.outcome.Completed && right.outcome.Completed {
				score += 0.05
			}
			if score > bestScore {
				bestScore = score
				bestLeft = i
				bestRight = j
			}
		}
	}
	if bestLeft < 0 || bestRight < 0 {
		return outcomeRow{}, outcomeRow{}, false
	}

	left := candidates[bestLeft]
	right := candidates[bestRight]
	if left.hot >= right.hot {
		return left.outcome, right.outcome, true
	}
	return right.outcome, left.outcome, true
}

func sampleExplanation(req requestRow, outcome outcomeRow, reasons []string, tileStats map[string]tileStatsRow, pricing pricingParams) sampleOrderExplanation {
	var pickupHotspot, pickupCold, dropoffHotspot, dropoffCold, opportunityAdjustment *float64
	if stats, ok := tileStats[req.PickupTile]; ok {
		pickupHotspot = floatPtr(stats.HotspotScore)
		pickupCold = floatPtr(stats.ColdScore)
	}
	if stats, ok := tileStats[req.DropoffTile]; ok {
		dropoffHotspot = floatPtr(stats.HotspotScore)
		dropoffCold = floatPtr(stats.ColdScore)
		adjustment := pricing.ColdDropoffPenalty*stats.ColdScore - pricing.HotDropoffDiscount*stats.HotspotScore
		opportunityAdjustment = floatPtr(adjustment)
	}

	return sampleOrderExplanation{
		RequestID:             req.RequestID,
		TaxiID:                outcome.TaxiID,
		Status:                outcomeStatus(outcome),
		ReasonTags:            append([]string(nil), reasons...),
		RequestTime:           req.RequestTime,
		AssignmentTime:        outcome.AssignmentTime,
		PickupTime:            outcome.PickupTime,
		CompletionTime:        outcome.CompletionTime,
		WaitTime:              outcome.WaitTime,
		PickupCost:            outcome.PickupCost,
		PendingBatchCount:     outcome.PendingBatchCount,
		CandidateBatchCount:   outcome.CandidateBatchCount,
		CandidateEdgeCount:    outcome.CandidateEdgeCount,
		HasCandidateEdge:      outcome.HasCandidateEdge,
		Pickup:                samplePoint{X: req.PickupX, Y: req.PickupY, Tile: req.PickupTile},
		Dropoff:               samplePoint{X: req.DropoffX, Y: req.DropoffY, Tile: req.DropoffTile},
		TripDistance:          tripDistance(req),
		PickupHotspotScore:    pickupHotspot,
		PickupColdScore:       pickupCold,
		DropoffHotspotScore:   dropoffHotspot,
		DropoffColdScore:      dropoffCold,
		OpportunityAdjustment: opportunityAdjustment,
		Pricing:               priceSample(req, outcome, pickupHotspot, dropoffHotspot, dropoffCold, pricing),
	}
}

func priceSample(req requestRow, outcome outcomeRow, pickupHotspot *float64, dropoffHotspot *float64, dropoffCold *float64, params pricingParams) pricingExplanation {
	tripKm := haversineKm(req.PickupY, req.PickupX, req.DropoffY, req.DropoffX)
	baseRevenue := tripKm * params.FarePerKm
	estimatedPickupKm := float64(outcome.PickupCost) / params.SecondsPerDistanceUnit * params.KmPerDegree
	estimatedPickupCost := estimatedPickupKm * params.PickupCostPerKm

	priceFactor := 1.0
	var pickupComponent, coldComponent, hotComponent *float64
	if pickupHotspot != nil && dropoffHotspot != nil && dropoffCold != nil {
		pickupHeat := *pickupHotspot
		dropoffHeat := *dropoffHotspot
		coldScore := *dropoffCold
		if params.Mode == "diminishing" {
			pickupHeat = diminishingReturn(pickupHeat)
			dropoffHeat = diminishingReturn(dropoffHeat)
			coldScore = diminishingReturn(coldScore)
		}
		pickupValue := params.PickupHotWeight * pickupHeat
		coldValue := params.ColdDropoffPenalty * coldScore
		hotValue := -params.HotDropoffDiscount * dropoffHeat
		pickupComponent = floatPtr(pickupValue)
		coldComponent = floatPtr(coldValue)
		hotComponent = floatPtr(hotValue)
		priceFactor = clamp(1.0+pickupValue+coldValue+hotValue, params.PriceFloor, params.PriceCap)
	}
	estimatedRevenue := baseRevenue * priceFactor
	return pricingExplanation{
		Mode:                   params.Mode,
		BaseRevenue:            baseRevenue,
		PriceFactor:            priceFactor,
		PickupHotspotComponent: pickupComponent,
		ColdDropoffComponent:   coldComponent,
		HotDropoffComponent:    hotComponent,
		EstimatedRevenue:       estimatedRevenue,
		EstimatedPickupKm:      estimatedPickupKm,
		EstimatedPickupCost:    estimatedPickupCost,
		EstimatedNet:           estimatedRevenue - estimatedPickupCost,
	}
}

func outcomeStatus(outcome outcomeRow) string {
	if outcome.Completed {
		return "completed"
	}
	if outcome.Assigned {
		return "assigned_incomplete"
	}
	return "unserved"
}

func tripDistance(req requestRow) float64 {
	averageLat := ((req.PickupY + req.DropoffY) / 2) * (math.Pi / 180)
	deltaLon := (req.DropoffX - req.PickupX) * math.Cos(averageLat)
	deltaLat := req.DropoffY - req.PickupY
	return math.Hypot(deltaLon, deltaLat)
}

func haversineKm(lat1, lon1, lat2, lon2 float64) float64 {
	const earthRadiusKm = 6371.0088
	lat1Rad := degreesToRadians(lat1)
	lat2Rad := degreesToRadians(lat2)
	deltaLat := degreesToRadians(lat2 - lat1)
	deltaLon := degreesToRadians(lon2 - lon1)
	a := math.Sin(deltaLat/2)*math.Sin(deltaLat/2) +
		math.Cos(lat1Rad)*math.Cos(lat2Rad)*math.Sin(deltaLon/2)*math.Sin(deltaLon/2)
	c := 2 * math.Atan2(math.Sqrt(a), math.Sqrt(1-a))
	return earthRadiusKm * c
}

func degreesToRadians(value float64) float64 {
	return value * math.Pi / 180
}

func diminishingReturn(value float64) float64 {
	if value <= 0 {
		return 0
	}
	if value <= 0.3 {
		return value
	}
	if value <= 0.6 {
		return 0.3 + (value-0.3)*0.8
	}
	if value <= 0.9 {
		return 0.3 + 0.3*0.8 + (value-0.6)*0.5
	}
	return 0.3 + 0.3*0.8 + 0.3*0.5 + (value-0.9)*0.2
}

func clamp(value float64, minValue float64, maxValue float64) float64 {
	if value < minValue {
		return minValue
	}
	if value > maxValue {
		return maxValue
	}
	return value
}

func appendUnique(values []string, value string) []string {
	for _, existing := range values {
		if existing == value {
			return values
		}
	}
	return append(values, value)
}

func floatPtr(value float64) *float64 {
	return &value
}

func buildTileActivityEvents(requests map[string]requestRow, outcomes []outcomeRow) ([]tileActivityEvent, error) {
	events := make([]tileActivityEvent, 0, len(requests)+len(outcomes)*2)
	for _, req := range requests {
		tileID, err := parseTileID(req.PickupTile)
		if err != nil {
			return nil, fmt.Errorf("request %s pickup_tile: %w", req.RequestID, err)
		}
		events = append(events, tileActivityEvent{Time: req.RequestTime, TileID: tileID, Kind: "pickup"})
	}
	for _, outcome := range outcomes {
		req, ok := requests[outcome.RequestID]
		if !ok {
			return nil, fmt.Errorf("outcome request_id %s not found in requests", outcome.RequestID)
		}
		if outcome.Assigned {
			tileID, err := parseTileID(req.PickupTile)
			if err != nil {
				return nil, fmt.Errorf("request %s pickup_tile: %w", req.RequestID, err)
			}
			events = append(events, tileActivityEvent{Time: outcome.AssignmentTime, TileID: tileID, Kind: "assigned"})
		}
		if outcome.Completed {
			tileID, err := parseTileID(req.DropoffTile)
			if err != nil {
				return nil, fmt.Errorf("request %s dropoff_tile: %w", req.RequestID, err)
			}
			events = append(events, tileActivityEvent{Time: outcome.CompletionTime, TileID: tileID, Kind: "completed"})
		}
	}
	return events, nil
}

func applyTileActivity(activityByTile map[int]*replayBatchTileActivity, event tileActivityEvent, delta int) {
	activity := activityByTile[event.TileID]
	if activity == nil {
		activity = &replayBatchTileActivity{TileID: event.TileID}
		activityByTile[event.TileID] = activity
	}
	switch event.Kind {
	case "pickup":
		activity.PickupCount += delta
	case "assigned":
		activity.AssignedCount += delta
	case "completed":
		activity.CompletedCount += delta
	}
	activity.ActivityScore = activity.PickupCount + activity.AssignedCount + activity.CompletedCount
	if activity.ActivityScore <= 0 {
		delete(activityByTile, event.TileID)
	}
}

func snapshotTileActivity(activityByTile map[int]*replayBatchTileActivity) ([]replayBatchTileActivity, replayBatchTileActivitySum) {
	tileIDs := make([]int, 0, len(activityByTile))
	for tileID, activity := range activityByTile {
		if activity.ActivityScore > 0 {
			tileIDs = append(tileIDs, tileID)
		}
	}
	sort.Ints(tileIDs)

	tiles := make([]replayBatchTileActivity, 0, len(tileIDs))
	totals := replayBatchTileActivitySum{ActiveTiles: len(tileIDs)}
	for _, tileID := range tileIDs {
		activity := *activityByTile[tileID]
		tiles = append(tiles, activity)
		totals.PickupCount += activity.PickupCount
		totals.AssignedCount += activity.AssignedCount
		totals.CompletedCount += activity.CompletedCount
		totals.ActivityScore += activity.ActivityScore
	}
	return tiles, totals
}

func parseTileID(raw string) (int, error) {
	if raw == "" {
		return 0, errors.New("missing tile id")
	}
	tileID, err := strconv.Atoi(raw)
	if err != nil {
		return 0, fmt.Errorf("invalid tile id %q", raw)
	}
	return tileID, nil
}

func loadRequests(path string) (map[string]requestRow, error) {
	rows, err := readCSVMaps(path)
	if err != nil {
		return nil, err
	}
	requests := make(map[string]requestRow, len(rows))
	for i, row := range rows {
		req := requestRow{
			RequestID:   row["request_id"],
			CustomerID:  row["customer_id"],
			PickupTile:  row["pickup_tile"],
			DropoffTile: row["dropoff_tile"],
		}
		if req.RequestID == "" {
			return nil, rowError(path, i, "missing request_id")
		}
		req.RequestTime, err = parseIntField(path, i, row, "request_time")
		if err != nil {
			return nil, err
		}
		req.PickupX, err = parseFloatField(path, i, row, "pickup_x")
		if err != nil {
			return nil, err
		}
		req.PickupY, err = parseFloatField(path, i, row, "pickup_y")
		if err != nil {
			return nil, err
		}
		req.DropoffX, err = parseFloatField(path, i, row, "dropoff_x")
		if err != nil {
			return nil, err
		}
		req.DropoffY, err = parseFloatField(path, i, row, "dropoff_y")
		if err != nil {
			return nil, err
		}
		requests[req.RequestID] = req
	}
	return requests, nil
}

func loadDrivers(path string) (map[string]driverRow, error) {
	rows, err := readCSVMaps(path)
	if err != nil {
		return nil, err
	}
	drivers := make(map[string]driverRow, len(rows))
	for i, row := range rows {
		driver := driverRow{
			TaxiID: row["taxi_id"],
			Tile:   row["tile"],
			Status: row["status"],
		}
		if driver.TaxiID == "" {
			return nil, rowError(path, i, "missing taxi_id")
		}
		driver.X, err = parseFloatField(path, i, row, "x")
		if err != nil {
			return nil, err
		}
		driver.Y, err = parseFloatField(path, i, row, "y")
		if err != nil {
			return nil, err
		}
		driver.AvailableTime, _ = parseOptionalInt(row["available_time"], 0)
		drivers[driver.TaxiID] = driver
	}
	return drivers, nil
}

func loadOutcomes(path string) ([]outcomeRow, error) {
	rows, err := readCSVMaps(path)
	if err != nil {
		return nil, err
	}
	outcomes := make([]outcomeRow, 0, len(rows))
	for i, row := range rows {
		outcome := outcomeRow{
			RequestID:        row["request_id"],
			TaxiID:           row["taxi_id"],
			HasCandidateEdge: parseBool(row["has_candidate_edge"]),
			Assigned:         parseBool(row["assigned"]),
			Completed:        parseBool(row["completed"]),
		}
		if outcome.RequestID == "" {
			return nil, rowError(path, i, "missing request_id")
		}
		outcome.PendingBatchCount, err = parseOptionalIntField(path, i, row, "pending_batch_count", 0)
		if err != nil {
			return nil, err
		}
		outcome.CandidateBatchCount, err = parseOptionalIntField(path, i, row, "candidate_batch_count", 0)
		if err != nil {
			return nil, err
		}
		outcome.CandidateEdgeCount, err = parseOptionalIntField(path, i, row, "candidate_edge_count", 0)
		if err != nil {
			return nil, err
		}
		if _, ok := row["has_candidate_edge"]; !ok {
			outcome.HasCandidateEdge = outcome.CandidateEdgeCount > 0
		}
		outcome.AssignmentTime, err = parseIntField(path, i, row, "assignment_time")
		if err != nil {
			return nil, err
		}
		outcome.PickupTime, err = parseIntField(path, i, row, "pickup_time")
		if err != nil {
			return nil, err
		}
		outcome.CompletionTime, err = parseIntField(path, i, row, "completion_time")
		if err != nil {
			return nil, err
		}
		outcome.WaitTime, err = parseOptionalIntField(path, i, row, "wait_time", 0)
		if err != nil {
			return nil, err
		}
		outcome.PickupCost, err = parseOptionalIntField(path, i, row, "pickup_cost", 0)
		if err != nil {
			return nil, err
		}
		outcomes = append(outcomes, outcome)
	}
	return outcomes, nil
}

func loadOptionalTileStats(path string) (map[string]tileStatsRow, error) {
	if path == "" {
		return map[string]tileStatsRow{}, nil
	}
	rows, err := readCSVMaps(path)
	if err != nil {
		return nil, err
	}
	stats := make(map[string]tileStatsRow, len(rows))
	for i, row := range rows {
		tileID := row["tile_id"]
		if tileID == "" {
			return nil, rowError(path, i, "missing tile_id")
		}
		hotspot, err := parseFloatField(path, i, row, "hotspot_score")
		if err != nil {
			return nil, err
		}
		cold, err := parseFloatField(path, i, row, "cold_score")
		if err != nil {
			return nil, err
		}
		stats[tileID] = tileStatsRow{
			TileID:       tileID,
			HotspotScore: hotspot,
			ColdScore:    cold,
		}
	}
	return stats, nil
}

func loadBatchLogs(path string) ([]batchLogRow, error) {
	rows, err := readCSVMaps(path)
	if err != nil {
		return nil, err
	}
	batches := make([]batchLogRow, 0, len(rows))
	for i, row := range rows {
		batch := batchLogRow{}
		batch.BatchTime, err = parseIntField(path, i, row, "batch_time")
		if err != nil {
			return nil, err
		}
		batch.AvailableDrivers, err = parseIntField(path, i, row, "available_drivers")
		if err != nil {
			return nil, err
		}
		batch.PendingRequests, err = parseIntField(path, i, row, "pending_requests")
		if err != nil {
			return nil, err
		}
		batch.CandidateEdges, err = parseIntField(path, i, row, "candidate_edges")
		if err != nil {
			return nil, err
		}
		batch.AppliedAssignments, err = parseIntField(path, i, row, "applied_assignments")
		if err != nil {
			return nil, err
		}
		batches = append(batches, batch)
	}
	return batches, nil
}

func readCSVMaps(path string) ([]map[string]string, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("open %s: %w", path, err)
	}
	defer file.Close()

	reader := csv.NewReader(file)
	reader.FieldsPerRecord = -1
	reader.TrimLeadingSpace = true
	records, err := reader.ReadAll()
	if err != nil {
		return nil, fmt.Errorf("read csv %s: %w", path, err)
	}
	if len(records) == 0 {
		return nil, fmt.Errorf("csv %s is empty", path)
	}

	headers := records[0]
	for i := range headers {
		headers[i] = strings.TrimSpace(headers[i])
	}
	rows := make([]map[string]string, 0, len(records)-1)
	for _, record := range records[1:] {
		if len(record) == 1 && strings.TrimSpace(record[0]) == "" {
			continue
		}
		row := make(map[string]string, len(headers))
		for i, header := range headers {
			if i < len(record) {
				row[header] = strings.TrimSpace(record[i])
			}
		}
		rows = append(rows, row)
	}
	return rows, nil
}

func parseFloatField(path string, rowIndex int, row map[string]string, field string) (float64, error) {
	raw, ok := row[field]
	if !ok || raw == "" {
		return 0, rowError(path, rowIndex, "missing "+field)
	}
	value, err := strconv.ParseFloat(raw, 64)
	if err != nil || math.IsNaN(value) || math.IsInf(value, 0) {
		return 0, rowError(path, rowIndex, fmt.Sprintf("invalid %s %q", field, raw))
	}
	return value, nil
}

func parseIntField(path string, rowIndex int, row map[string]string, field string) (int, error) {
	raw, ok := row[field]
	if !ok || raw == "" {
		return 0, rowError(path, rowIndex, "missing "+field)
	}
	value, err := strconv.Atoi(raw)
	if err != nil {
		return 0, rowError(path, rowIndex, fmt.Sprintf("invalid %s %q", field, raw))
	}
	return value, nil
}

func parseOptionalIntField(path string, rowIndex int, row map[string]string, field string, fallback int) (int, error) {
	raw, ok := row[field]
	if !ok || raw == "" {
		return fallback, nil
	}
	value, err := strconv.Atoi(raw)
	if err != nil {
		return 0, rowError(path, rowIndex, fmt.Sprintf("invalid %s %q", field, raw))
	}
	return value, nil
}

func parseOptionalInt(raw string, fallback int) (int, error) {
	if raw == "" {
		return fallback, nil
	}
	return strconv.Atoi(raw)
}

func parseBool(raw string) bool {
	switch strings.ToLower(strings.TrimSpace(raw)) {
	case "1", "true", "yes", "y":
		return true
	default:
		return false
	}
}

func rowError(path string, rowIndex int, msg string) error {
	return fmt.Errorf("%s data row %d: %s", path, rowIndex+1, msg)
}

func countOutcomes(outcomes []outcomeRow) (int, int) {
	assigned := 0
	completed := 0
	for _, outcome := range outcomes {
		if outcome.Assigned {
			assigned++
		}
		if outcome.Completed {
			completed++
		}
	}
	return assigned, completed
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

func removeGenerated(outputDir string, names ...string) error {
	for _, name := range names {
		err := os.Remove(filepath.Join(outputDir, name))
		if err != nil && !errors.Is(err, os.ErrNotExist) {
			return fmt.Errorf("remove stale %s: %w", name, err)
		}
	}
	return nil
}
