package main

import (
	"encoding/csv"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"math"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
)

const (
	minLon = -75.0
	maxLon = -72.0
	minLat = 40.0
	maxLat = 42.0
)

type options struct {
	tileStatsPath            string
	requestsPath             string
	outputDir                string
	tileGridCols             int
	cornerWitnessesPerCorner int
}

type featureCollection struct {
	Type     string        `json:"type"`
	Features []tileFeature `json:"features"`
}

type tileFeature struct {
	Type       string         `json:"type"`
	Properties tileProperties `json:"properties"`
	Geometry   polygon        `json:"geometry"`
}

type witnessFeatureCollection struct {
	Type     string           `json:"type"`
	Features []witnessFeature `json:"features"`
}

type witnessFeature struct {
	Type       string            `json:"type"`
	Properties witnessProperties `json:"properties"`
	Geometry   point             `json:"geometry"`
}

type polygon struct {
	Type        string        `json:"type"`
	Coordinates [][][]float64 `json:"coordinates"`
}

type point struct {
	Type        string    `json:"type"`
	Coordinates []float64 `json:"coordinates"`
}

type tileProperties struct {
	TileID               int     `json:"tile_id"`
	PickupCount          int     `json:"pickup_count"`
	DropoffCount         int     `json:"dropoff_count"`
	AvailableDriverCount int     `json:"available_driver_count"`
	HotspotScore         float64 `json:"hotspot_score"`
	ColdScore            float64 `json:"cold_score"`
}

type requestPoint struct {
	RequestID   int
	CustomerID  int
	RequestTime int
	PickupX     float64
	PickupY     float64
	PickupTile  int
}

type witnessCandidate struct {
	Request          requestPoint
	Corner           string
	DistanceToCorner float64
}

type witnessProperties struct {
	TileID           int     `json:"tile_id"`
	Corner           string  `json:"corner"`
	RequestID        int     `json:"request_id"`
	CustomerID       int     `json:"customer_id"`
	RequestTime      int     `json:"request_time"`
	PickupX          float64 `json:"pickup_x"`
	PickupY          float64 `json:"pickup_y"`
	DistanceToCorner float64 `json:"distance_to_corner"`
}

func main() {
	var opts options
	flag.StringVar(&opts.tileStatsPath, "tile-stats", "", "input tile_stats.csv path")
	flag.StringVar(&opts.requestsPath, "requests", "", "optional normalized requests.csv path for tile corner witness points")
	flag.IntVar(&opts.tileGridCols, "tile-grid-cols", 100, "number of columns/rows used by simpleTile encoding")
	flag.IntVar(&opts.cornerWitnessesPerCorner, "corner-witnesses-per-corner", 1, "pickup witness count to keep per tile corner when -requests is set")
	flag.StringVar(&opts.outputDir, "output-dir", ".", "directory for tile_stats.geojson")
	flag.Parse()

	if opts.tileStatsPath == "" {
		fatalf("missing -tile-stats")
	}
	if opts.tileGridCols <= 0 {
		fatalf("-tile-grid-cols must be positive")
	}
	if opts.cornerWitnessesPerCorner <= 0 {
		fatalf("-corner-witnesses-per-corner must be positive")
	}

	if err := exportTileStats(opts); err != nil {
		fatalf("%v", err)
	}
}

func exportTileStats(opts options) error {
	features, err := loadTileStats(opts.tileStatsPath, opts.tileGridCols)
	if err != nil {
		return err
	}

	if err := os.MkdirAll(opts.outputDir, 0755); err != nil {
		return fmt.Errorf("create output dir: %w", err)
	}

	outputPath := filepath.Join(opts.outputDir, "tile_stats.geojson")
	output, err := os.Create(outputPath)
	if err != nil {
		return fmt.Errorf("create %s: %w", outputPath, err)
	}
	defer output.Close()

	encoder := json.NewEncoder(output)
	encoder.SetIndent("", "  ")
	collection := featureCollection{
		Type:     "FeatureCollection",
		Features: features,
	}
	if err := encoder.Encode(collection); err != nil {
		return fmt.Errorf("write %s: %w", outputPath, err)
	}

	fmt.Printf("wrote %d tile features to %s\n", len(features), outputPath)
	if opts.requestsPath != "" {
		witnessFeatures, err := loadCornerWitnesses(opts.requestsPath, opts.tileGridCols, opts.cornerWitnessesPerCorner)
		if err != nil {
			return err
		}
		witnessPath := filepath.Join(opts.outputDir, "tile_corner_witnesses.geojson")
		if err := writeWitnessFeatures(witnessPath, witnessFeatures); err != nil {
			return err
		}
		fmt.Printf("wrote %d corner witness features to %s\n", len(witnessFeatures), witnessPath)
	}
	return nil
}

func loadTileStats(path string, gridCols int) ([]tileFeature, error) {
	input, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("open tile stats: %w", err)
	}
	defer input.Close()

	reader := csv.NewReader(input)
	reader.FieldsPerRecord = -1
	reader.TrimLeadingSpace = true

	header, err := reader.Read()
	if err != nil {
		return nil, fmt.Errorf("read tile stats header: %w", err)
	}
	columns := indexHeader(header)
	for _, name := range []string{
		"tile_id",
		"pickup_count",
		"dropoff_count",
		"available_driver_count",
		"hotspot_score",
		"cold_score",
	} {
		if _, ok := columns[name]; !ok {
			return nil, fmt.Errorf("tile stats missing required column %q", name)
		}
	}

	features := make([]tileFeature, 0)
	rowNumber := 1
	for {
		record, err := reader.Read()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, fmt.Errorf("read tile stats row %d: %w", rowNumber+1, err)
		}
		rowNumber++

		props, err := parseTileProperties(record, columns, rowNumber)
		if err != nil {
			return nil, err
		}
		if props.TileID < 0 || props.TileID >= gridCols*gridCols {
			return nil, fmt.Errorf("row %d tile_id=%d outside grid size %d", rowNumber, props.TileID, gridCols)
		}

		features = append(features, tileFeature{
			Type:       "Feature",
			Properties: props,
			Geometry:   tilePolygon(props.TileID, gridCols),
		})
	}
	return features, nil
}

func writeWitnessFeatures(path string, features []witnessFeature) error {
	output, err := os.Create(path)
	if err != nil {
		return fmt.Errorf("create %s: %w", path, err)
	}
	defer output.Close()

	encoder := json.NewEncoder(output)
	encoder.SetIndent("", "  ")
	collection := witnessFeatureCollection{
		Type:     "FeatureCollection",
		Features: features,
	}
	if err := encoder.Encode(collection); err != nil {
		return fmt.Errorf("write %s: %w", path, err)
	}
	return nil
}

func loadCornerWitnesses(path string, gridCols int, perCorner int) ([]witnessFeature, error) {
	requests, err := loadRequests(path, gridCols)
	if err != nil {
		return nil, err
	}

	grouped := make(map[int][]requestPoint)
	for _, request := range requests {
		grouped[request.PickupTile] = append(grouped[request.PickupTile], request)
	}

	tileIDs := make([]int, 0, len(grouped))
	for tileID := range grouped {
		tileIDs = append(tileIDs, tileID)
	}
	sort.Ints(tileIDs)

	features := make([]witnessFeature, 0)
	for _, tileID := range tileIDs {
		corners := tileCorners(tileID, gridCols)
		for _, corner := range []string{"southwest", "southeast", "northeast", "northwest"} {
			candidates := nearestCornerCandidates(grouped[tileID], corner, corners[corner])
			limit := perCorner
			if len(candidates) < limit {
				limit = len(candidates)
			}
			for i := 0; i < limit; i++ {
				features = append(features, witnessFeatureFromCandidate(candidates[i], tileID))
			}
		}
	}
	return features, nil
}

func loadRequests(path string, gridCols int) ([]requestPoint, error) {
	input, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("open requests: %w", err)
	}
	defer input.Close()

	reader := csv.NewReader(input)
	reader.FieldsPerRecord = -1
	reader.TrimLeadingSpace = true

	header, err := reader.Read()
	if err != nil {
		return nil, fmt.Errorf("read requests header: %w", err)
	}
	columns := indexHeader(header)
	for _, name := range []string{
		"request_id",
		"customer_id",
		"request_time",
		"pickup_x",
		"pickup_y",
		"pickup_tile",
	} {
		if _, ok := columns[name]; !ok {
			return nil, fmt.Errorf("requests missing required column %q", name)
		}
	}

	requests := make([]requestPoint, 0)
	rowNumber := 1
	for {
		record, err := reader.Read()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, fmt.Errorf("read requests row %d: %w", rowNumber+1, err)
		}
		rowNumber++

		request, err := parseRequestPoint(record, columns, rowNumber)
		if err != nil {
			return nil, err
		}
		if request.PickupTile < 0 || request.PickupTile >= gridCols*gridCols {
			return nil, fmt.Errorf("row %d pickup_tile=%d outside grid size %d", rowNumber, request.PickupTile, gridCols)
		}
		requests = append(requests, request)
	}
	return requests, nil
}

func parseRequestPoint(record []string, columns map[string]int, rowNumber int) (requestPoint, error) {
	requestID, err := parseInt(record, columns, "request_id")
	if err != nil {
		return requestPoint{}, fmt.Errorf("row %d: %w", rowNumber, err)
	}
	customerID, err := parseInt(record, columns, "customer_id")
	if err != nil {
		return requestPoint{}, fmt.Errorf("row %d: %w", rowNumber, err)
	}
	requestTime, err := parseInt(record, columns, "request_time")
	if err != nil {
		return requestPoint{}, fmt.Errorf("row %d: %w", rowNumber, err)
	}
	pickupX, err := parseFloat(record, columns, "pickup_x")
	if err != nil {
		return requestPoint{}, fmt.Errorf("row %d: %w", rowNumber, err)
	}
	pickupY, err := parseFloat(record, columns, "pickup_y")
	if err != nil {
		return requestPoint{}, fmt.Errorf("row %d: %w", rowNumber, err)
	}
	pickupTile, err := parseInt(record, columns, "pickup_tile")
	if err != nil {
		return requestPoint{}, fmt.Errorf("row %d: %w", rowNumber, err)
	}
	return requestPoint{
		RequestID:   requestID,
		CustomerID:  customerID,
		RequestTime: requestTime,
		PickupX:     pickupX,
		PickupY:     pickupY,
		PickupTile:  pickupTile,
	}, nil
}

func nearestCornerCandidates(requests []requestPoint, corner string, cornerPoint []float64) []witnessCandidate {
	candidates := make([]witnessCandidate, 0, len(requests))
	for _, request := range requests {
		dx := request.PickupX - cornerPoint[0]
		dy := request.PickupY - cornerPoint[1]
		candidates = append(candidates, witnessCandidate{
			Request:          request,
			Corner:           corner,
			DistanceToCorner: math.Sqrt(dx*dx + dy*dy),
		})
	}
	sort.Slice(candidates, func(i, j int) bool {
		if candidates[i].DistanceToCorner != candidates[j].DistanceToCorner {
			return candidates[i].DistanceToCorner < candidates[j].DistanceToCorner
		}
		return candidates[i].Request.RequestID < candidates[j].Request.RequestID
	})
	return candidates
}

func witnessFeatureFromCandidate(candidate witnessCandidate, tileID int) witnessFeature {
	request := candidate.Request
	return witnessFeature{
		Type: "Feature",
		Properties: witnessProperties{
			TileID:           tileID,
			Corner:           candidate.Corner,
			RequestID:        request.RequestID,
			CustomerID:       request.CustomerID,
			RequestTime:      request.RequestTime,
			PickupX:          request.PickupX,
			PickupY:          request.PickupY,
			DistanceToCorner: candidate.DistanceToCorner,
		},
		Geometry: point{
			Type:        "Point",
			Coordinates: []float64{request.PickupX, request.PickupY},
		},
	}
}

func indexHeader(header []string) map[string]int {
	columns := make(map[string]int, len(header))
	for i, value := range header {
		columns[strings.ToLower(strings.TrimSpace(value))] = i
	}
	return columns
}

func parseTileProperties(record []string, columns map[string]int, rowNumber int) (tileProperties, error) {
	tileID, err := parseInt(record, columns, "tile_id")
	if err != nil {
		return tileProperties{}, fmt.Errorf("row %d: %w", rowNumber, err)
	}
	pickupCount, err := parseInt(record, columns, "pickup_count")
	if err != nil {
		return tileProperties{}, fmt.Errorf("row %d: %w", rowNumber, err)
	}
	dropoffCount, err := parseInt(record, columns, "dropoff_count")
	if err != nil {
		return tileProperties{}, fmt.Errorf("row %d: %w", rowNumber, err)
	}
	availableDriverCount, err := parseInt(record, columns, "available_driver_count")
	if err != nil {
		return tileProperties{}, fmt.Errorf("row %d: %w", rowNumber, err)
	}
	hotspotScore, err := parseFloat(record, columns, "hotspot_score")
	if err != nil {
		return tileProperties{}, fmt.Errorf("row %d: %w", rowNumber, err)
	}
	coldScore, err := parseFloat(record, columns, "cold_score")
	if err != nil {
		return tileProperties{}, fmt.Errorf("row %d: %w", rowNumber, err)
	}

	return tileProperties{
		TileID:               tileID,
		PickupCount:          pickupCount,
		DropoffCount:         dropoffCount,
		AvailableDriverCount: availableDriverCount,
		HotspotScore:         hotspotScore,
		ColdScore:            coldScore,
	}, nil
}

func parseInt(record []string, columns map[string]int, name string) (int, error) {
	value, err := field(record, columns, name)
	if err != nil {
		return 0, err
	}
	parsed, err := strconv.Atoi(value)
	if err != nil {
		return 0, fmt.Errorf("parse %s=%q as int: %w", name, value, err)
	}
	return parsed, nil
}

func parseFloat(record []string, columns map[string]int, name string) (float64, error) {
	value, err := field(record, columns, name)
	if err != nil {
		return 0, err
	}
	parsed, err := strconv.ParseFloat(value, 64)
	if err != nil {
		return 0, fmt.Errorf("parse %s=%q as float: %w", name, value, err)
	}
	return parsed, nil
}

func field(record []string, columns map[string]int, name string) (string, error) {
	index, ok := columns[name]
	if !ok {
		return "", fmt.Errorf("missing column %s", name)
	}
	if index >= len(record) {
		return "", fmt.Errorf("missing field %s", name)
	}
	value := strings.TrimSpace(record[index])
	if value == "" {
		return "", fmt.Errorf("empty field %s", name)
	}
	return value, nil
}

func tilePolygon(tileID int, gridCols int) polygon {
	bounds := tileBounds(tileID, gridCols)
	west := bounds[0]
	south := bounds[1]
	east := bounds[2]
	north := bounds[3]

	return polygon{
		Type: "Polygon",
		Coordinates: [][][]float64{
			{
				{west, south},
				{east, south},
				{east, north},
				{west, north},
				{west, south},
			},
		},
	}
}

func tileCorners(tileID int, gridCols int) map[string][]float64 {
	bounds := tileBounds(tileID, gridCols)
	west := bounds[0]
	south := bounds[1]
	east := bounds[2]
	north := bounds[3]
	return map[string][]float64{
		"southwest": {west, south},
		"southeast": {east, south},
		"northeast": {east, north},
		"northwest": {west, north},
	}
}

func tileBounds(tileID int, gridCols int) []float64 {
	row := tileID / gridCols
	col := tileID % gridCols
	lonStep := (maxLon - minLon) / float64(gridCols)
	latStep := (maxLat - minLat) / float64(gridCols)

	west := minLon + float64(col)*lonStep
	east := west + lonStep
	south := minLat + float64(row)*latStep
	north := south + latStep
	return []float64{west, south, east, north}
}

func fatalf(format string, args ...any) {
	fmt.Fprintf(os.Stderr, format+"\n", args...)
	os.Exit(1)
}
