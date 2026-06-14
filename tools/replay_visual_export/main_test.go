package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestRunExportAutoLive(t *testing.T) {
	root := t.TempDir()
	inputDir := filepath.Join(root, "input")
	outputDir := filepath.Join(root, "out")
	mustMkdir(t, inputDir)

	requests := filepath.Join(inputDir, "requests.csv")
	drivers := filepath.Join(inputDir, "drivers.csv")
	outcomes := filepath.Join(inputDir, "request_outcomes.csv")
	batches := filepath.Join(inputDir, "batch_logs.csv")

	writeFile(t, requests, "request_id,customer_id,request_time,pickup_x,pickup_y,dropoff_x,dropoff_y,pickup_tile,dropoff_tile\nr1,c1,0,-73.99,40.75,-73.98,40.76,1,2\nr2,c2,10,-73.97,40.74,-73.96,40.73,3,4\n")
	writeFile(t, drivers, "taxi_id,x,y,tile,available_time,status\nt1,-74.00,40.70,1,0,free\n")
	writeFile(t, outcomes, "request_id,pending_batch_count,candidate_batch_count,candidate_edge_count,has_candidate_edge,assigned,completed,taxi_id,assignment_time,pickup_time,completion_time,wait_time,pickup_cost\nr1,1,1,1,1,1,1,t1,0,30,90,0,30\nr2,1,0,0,0,0,0,-1,-1,-1,-1,0,0\n")
	writeFile(t, batches, "batch_time,available_drivers,pending_requests,candidate_edges,applied_assignments\n0,1,1,1,1\n30,0,1,0,0\n")

	err := runExport(withTestPricing(config{
		requestsPath:        requests,
		driversPath:         drivers,
		requestOutcomesPath: outcomes,
		batchLogsPath:       batches,
		outputDir:           outputDir,
		liveThreshold:       1000,
		mode:                "auto",
	}))
	if err != nil {
		t.Fatalf("runExport failed: %v", err)
	}

	manifest := readJSON[replayManifest](t, filepath.Join(outputDir, "replay_manifest.json"))
	if manifest.Mode != "live" {
		t.Fatalf("manifest mode = %q, want live", manifest.Mode)
	}

	paths := readJSON[featureCollection](t, filepath.Join(outputDir, "replay_live_paths.geojson"))
	if len(paths.Features) != 2 {
		t.Fatalf("live path feature count = %d, want 2", len(paths.Features))
	}
	for _, f := range paths.Features {
		start := int(f.Properties["start_time"].(float64))
		end := int(f.Properties["end_time"].(float64))
		if start > end {
			t.Fatalf("path start_time %d is after end_time %d", start, end)
		}
	}
}

func TestRunExportAutoBatch(t *testing.T) {
	root := t.TempDir()
	inputDir := filepath.Join(root, "input")
	outputDir := filepath.Join(root, "out")
	mustMkdir(t, inputDir)

	requests := filepath.Join(inputDir, "requests.csv")
	drivers := filepath.Join(inputDir, "drivers.csv")
	outcomes := filepath.Join(inputDir, "request_outcomes.csv")
	batches := filepath.Join(inputDir, "batch_logs.csv")

	writeFile(t, requests, "request_id,customer_id,request_time,pickup_x,pickup_y,dropoff_x,dropoff_y,pickup_tile,dropoff_tile\nr1,c1,0,-73.99,40.75,-73.98,40.76,1,2\nr2,c2,10,-73.97,40.74,-73.96,40.73,3,4\n")
	writeFile(t, drivers, "taxi_id,x,y,tile,available_time,status\nt1,-74.00,40.70,1,0,free\n")
	writeFile(t, outcomes, "request_id,pending_batch_count,candidate_batch_count,candidate_edge_count,has_candidate_edge,assigned,completed,taxi_id,assignment_time,pickup_time,completion_time,wait_time,pickup_cost\nr1,1,1,1,1,1,1,t1,0,30,90,0,30\nr2,1,1,1,1,1,0,t1,120,150,150,110,30\n")
	writeFile(t, batches, "batch_time,available_drivers,pending_requests,candidate_edges,applied_assignments\n0,1,1,1,1\n120,1,1,1,1\n180,0,0,0,0\n")

	err := runExport(withTestPricing(config{
		requestsPath:        requests,
		driversPath:         drivers,
		requestOutcomesPath: outcomes,
		batchLogsPath:       batches,
		outputDir:           outputDir,
		liveThreshold:       1,
		batchWindowSeconds:  60,
		mode:                "auto",
	}))
	if err != nil {
		t.Fatalf("runExport failed: %v", err)
	}

	manifest := readJSON[replayManifest](t, filepath.Join(outputDir, "replay_manifest.json"))
	if manifest.Mode != "batch" {
		t.Fatalf("manifest mode = %q, want batch", manifest.Mode)
	}
	if _, err := os.Stat(filepath.Join(outputDir, "replay_live_paths.geojson")); !os.IsNotExist(err) {
		t.Fatalf("live paths should not exist in batch mode")
	}
	if !containsString(manifest.GeneratedFiles, "replay_batch_tiles.json") {
		t.Fatalf("manifest generated_files does not include replay_batch_tiles.json: %v", manifest.GeneratedFiles)
	}

	replayBatches := readJSON[[]batchLogRow](t, filepath.Join(outputDir, "replay_batches.json"))
	if len(replayBatches) != 3 {
		t.Fatalf("batch count = %d, want 3", len(replayBatches))
	}
	for i := 1; i < len(replayBatches); i++ {
		if replayBatches[i].AssignedCumulative < replayBatches[i-1].AssignedCumulative {
			t.Fatalf("assigned cumulative decreased at batch %d", i)
		}
		if replayBatches[i].CompletedCumulative < replayBatches[i-1].CompletedCumulative {
			t.Fatalf("completed cumulative decreased at batch %d", i)
		}
	}

	replayBatchTiles := readJSON[[]replayBatchTileFrame](t, filepath.Join(outputDir, "replay_batch_tiles.json"))
	if len(replayBatchTiles) != len(replayBatches) {
		t.Fatalf("batch tile frames = %d, want %d", len(replayBatchTiles), len(replayBatches))
	}
	for _, frame := range replayBatchTiles {
		for _, tile := range frame.Tiles {
			if tile.ActivityScore != tile.PickupCount+tile.AssignedCount+tile.CompletedCount {
				t.Fatalf("tile %d activity_score mismatch: %+v", tile.TileID, tile)
			}
		}
	}
	lastFrame := replayBatchTiles[len(replayBatchTiles)-1]
	for _, tile := range lastFrame.Tiles {
		if tile.CompletedCount != 0 {
			t.Fatalf("last frame should not include completed events outside the window: %+v", tile)
		}
	}
}

func TestRunExportWritesSampleOrderExplanations(t *testing.T) {
	root := t.TempDir()
	inputDir := filepath.Join(root, "input")
	outputDir := filepath.Join(root, "out")
	mustMkdir(t, inputDir)

	requests := filepath.Join(inputDir, "requests.csv")
	drivers := filepath.Join(inputDir, "drivers.csv")
	outcomes := filepath.Join(inputDir, "request_outcomes.csv")
	batches := filepath.Join(inputDir, "batch_logs.csv")
	tileStats := filepath.Join(inputDir, "tile_stats.csv")

	writeFile(t, requests, "request_id,customer_id,request_time,pickup_x,pickup_y,dropoff_x,dropoff_y,pickup_tile,dropoff_tile\nr1,c1,0,-73.99,40.75,-73.98,40.76,1,2\nr2,c2,10,-73.97,40.74,-73.96,40.73,3,4\nr3,c3,20,-73.95,40.72,-73.94,40.71,5,6\nr4,c4,30,-73.93,40.70,-73.92,40.69,7,8\n")
	writeFile(t, drivers, "taxi_id,x,y,tile,available_time,status\nt1,-74.00,40.70,1,0,free\n")
	writeFile(t, outcomes, "request_id,pending_batch_count,candidate_batch_count,candidate_edge_count,has_candidate_edge,assigned,completed,taxi_id,assignment_time,pickup_time,completion_time,wait_time,pickup_cost\nr1,1,1,2,1,1,1,t1,0,30,90,0,30\nr2,3,2,2,1,1,0,t1,120,180,-1,110,60\nr3,2,0,0,0,0,0,-1,-1,-1,-1,0,0\nr4,2,1,1,1,1,1,t1,150,300,900,120,150\n")
	writeFile(t, batches, "batch_time,available_drivers,pending_requests,candidate_edges,applied_assignments\n0,1,1,2,1\n120,1,2,2,1\n150,1,1,1,1\n")
	writeFile(t, tileStats, "tile_id,pickup_count,dropoff_count,available_driver_count,hotspot_score,cold_score\n1,1,0,1,0.20,0.80\n2,0,1,0,0.80,0.20\n3,1,0,0,0.10,0.90\n4,0,1,0,0.05,0.95\n5,1,0,0,0.40,0.60\n6,0,1,0,0.00,1.00\n7,1,0,0,0.70,0.30\n8,0,1,0,0.60,0.40\n")

	err := runExport(config{
		requestsPath:           requests,
		driversPath:            drivers,
		requestOutcomesPath:    outcomes,
		batchLogsPath:          batches,
		outputDir:              outputDir,
		liveThreshold:          1000,
		mode:                   "auto",
		sampleOrderCount:       4,
		sampleSeed:             7,
		tileStatsPath:          tileStats,
		farePerKm:              2,
		pickupCostPerKm:        0.5,
		kmPerDegree:            100,
		secondsPerDistanceUnit: 1000,
		pickupHotWeight:        0.1,
		coldDropoffPenalty:     2,
		hotDropoffDiscount:     0.5,
		priceFloor:             0.8,
		priceCap:               2.5,
		pricingMode:            "linear",
	})
	if err != nil {
		t.Fatalf("runExport failed: %v", err)
	}

	manifest := readJSON[replayManifest](t, filepath.Join(outputDir, "replay_manifest.json"))
	if !containsString(manifest.GeneratedFiles, "sampled_order_explanations.json") {
		t.Fatalf("manifest generated_files does not include sampled_order_explanations.json: %v", manifest.GeneratedFiles)
	}

	samples := readJSON[[]sampleOrderExplanation](t, filepath.Join(outputDir, "sampled_order_explanations.json"))
	if len(samples) != 4 {
		t.Fatalf("sample count = %d, want 4", len(samples))
	}
	if samples[0].RequestID != "r1" || samples[0].Status != "completed" {
		t.Fatalf("first sample = %+v, want completed r1", samples[0])
	}
	if !hasSampleWithTag(samples, "distance_matched_hot_dropoff") || !hasSampleWithTag(samples, "distance_matched_cold_dropoff") {
		t.Fatalf("samples do not include distance matched hot/cold pair: %+v", samples)
	}
	if !hasSampleWithTag(samples, "unserved") {
		t.Fatalf("samples do not include unserved tag: %+v", samples)
	}
	if !hasSampleWithTag(samples, "high_pickup_cost") {
		t.Fatalf("samples do not include high_pickup_cost tag: %+v", samples)
	}
	coldSample := sampleByTag(samples, "cold_dropoff")
	if coldSample == nil || coldSample.OpportunityAdjustment == nil {
		t.Fatalf("cold dropoff sample missing opportunity adjustment: %+v", samples)
	}
	want := 2.0
	if mathAbs(*coldSample.OpportunityAdjustment-want) > 0.000001 {
		t.Fatalf("opportunity adjustment = %f, want %f", *coldSample.OpportunityAdjustment, want)
	}
	if mathAbs(coldSample.Pricing.EstimatedNet-(coldSample.Pricing.EstimatedRevenue-coldSample.Pricing.EstimatedPickupCost)) > 0.000001 {
		t.Fatalf("pricing net mismatch: %+v", coldSample.Pricing)
	}
}

func TestBuildSampleOrderExplanationsWithoutTileStats(t *testing.T) {
	requests := map[string]requestRow{
		"r1": {RequestID: "r1", RequestTime: 0, PickupX: -73.99, PickupY: 40.75, DropoffX: -73.98, DropoffY: 40.76, PickupTile: "1", DropoffTile: "2"},
	}
	outcomes := []outcomeRow{
		{RequestID: "r1", Assigned: true, Completed: true, TaxiID: "t1", HasCandidateEdge: true},
	}

	samples := buildSampleOrderExplanations(requests, outcomes, nil, 1, 1, pricingParams{
		FarePerKm:              1,
		PickupCostPerKm:        1,
		KmPerDegree:            111,
		SecondsPerDistanceUnit: 100000,
		PickupHotWeight:        0.15,
		ColdDropoffPenalty:     0.20,
		HotDropoffDiscount:     0.10,
		PriceFloor:             0.8,
		PriceCap:               1.8,
		Mode:                   "linear",
	})
	if len(samples) != 1 {
		t.Fatalf("sample count = %d, want 1", len(samples))
	}
	if samples[0].DropoffHotspotScore != nil || samples[0].OpportunityAdjustment != nil {
		t.Fatalf("tile-derived fields should be nil without tile stats: %+v", samples[0])
	}
	if samples[0].Pricing.PriceFactor != 1 {
		t.Fatalf("price factor without tile stats = %f, want 1", samples[0].Pricing.PriceFactor)
	}
}

func TestBuildBatchTileArtifactsUsesSlidingWindow(t *testing.T) {
	requests := map[string]requestRow{
		"r1": {RequestID: "r1", RequestTime: 0, PickupTile: "10", DropoffTile: "20"},
		"r2": {RequestID: "r2", RequestTime: 100, PickupTile: "10", DropoffTile: "30"},
	}
	outcomes := []outcomeRow{
		{RequestID: "r1", Assigned: true, Completed: true, AssignmentTime: 0, CompletionTime: 90},
		{RequestID: "r2", Assigned: true, Completed: false, AssignmentTime: 120},
	}
	batches := []batchLogRow{
		{BatchTime: 0},
		{BatchTime: 90},
		{BatchTime: 121},
	}

	frames, err := buildBatchTileArtifacts(batches, requests, outcomes, 30)
	if err != nil {
		t.Fatalf("buildBatchTileArtifacts failed: %v", err)
	}
	if len(frames) != 3 {
		t.Fatalf("frame count = %d, want 3", len(frames))
	}
	if frames[0].Totals.ActivityScore != 2 {
		t.Fatalf("frame 0 activity score = %d, want 2", frames[0].Totals.ActivityScore)
	}
	if frames[1].Totals.ActivityScore != 1 {
		t.Fatalf("frame 1 activity score = %d, want 1", frames[1].Totals.ActivityScore)
	}
	if frames[2].Totals.ActivityScore != 2 {
		t.Fatalf("frame 2 activity score = %d, want 2", frames[2].Totals.ActivityScore)
	}
	if findTile(frames[2], 20).ActivityScore != 0 {
		t.Fatalf("frame 2 should not include tile 20 completion outside [91, 121]")
	}
}

func mustMkdir(t *testing.T, path string) {
	t.Helper()
	if err := os.MkdirAll(path, 0o755); err != nil {
		t.Fatalf("mkdir %s: %v", path, err)
	}
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

func containsString(values []string, target string) bool {
	for _, value := range values {
		if value == target {
			return true
		}
	}
	return false
}

func findTile(frame replayBatchTileFrame, tileID int) replayBatchTileActivity {
	for _, tile := range frame.Tiles {
		if tile.TileID == tileID {
			return tile
		}
	}
	return replayBatchTileActivity{}
}

func withTestPricing(cfg config) config {
	cfg.farePerKm = 1
	cfg.pickupCostPerKm = 1
	cfg.kmPerDegree = 111
	cfg.secondsPerDistanceUnit = 100000
	cfg.pickupHotWeight = 0.15
	cfg.coldDropoffPenalty = 0.20
	cfg.hotDropoffDiscount = 0.10
	cfg.priceFloor = 0.8
	cfg.priceCap = 1.8
	cfg.pricingMode = "linear"
	return cfg
}

func hasSampleWithTag(samples []sampleOrderExplanation, tag string) bool {
	return sampleByTag(samples, tag) != nil
}

func sampleByTag(samples []sampleOrderExplanation, tag string) *sampleOrderExplanation {
	for i := range samples {
		for _, sampleTag := range samples[i].ReasonTags {
			if sampleTag == tag {
				return &samples[i]
			}
		}
	}
	return nil
}

func mathAbs(value float64) float64 {
	if value < 0 {
		return -value
	}
	return value
}
