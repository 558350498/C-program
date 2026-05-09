package main

import (
	"encoding/csv"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
)

const (
	schemaVersion             = 1
	defaultBatchWindowSeconds = 600
)

type config struct {
	requestsPath        string
	driversPath         string
	requestOutcomesPath string
	batchLogsPath       string
	outputDir           string
	liveThreshold       int
	batchWindowSeconds  int
	mode                string
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
	RequestID      string
	Assigned       bool
	Completed      bool
	TaxiID         string
	AssignmentTime int
	PickupTime     int
	CompletionTime int
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
		},
	}
	return writeJSON(filepath.Join(cfg.outputDir, "replay_manifest.json"), manifest)
}

func withDefaults(cfg config) config {
	if cfg.batchWindowSeconds == 0 {
		cfg.batchWindowSeconds = defaultBatchWindowSeconds
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
			RequestID: row["request_id"],
			TaxiID:    row["taxi_id"],
			Assigned:  parseBool(row["assigned"]),
			Completed: parseBool(row["completed"]),
		}
		if outcome.RequestID == "" {
			return nil, rowError(path, i, "missing request_id")
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
		outcomes = append(outcomes, outcome)
	}
	return outcomes, nil
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
