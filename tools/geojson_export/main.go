package main

import (
	"encoding/csv"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
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
	tileStatsPath string
	outputDir     string
	tileGridCols  int
}

type featureCollection struct {
	Type     string    `json:"type"`
	Features []feature `json:"features"`
}

type feature struct {
	Type       string         `json:"type"`
	Properties tileProperties `json:"properties"`
	Geometry   polygon        `json:"geometry"`
}

type polygon struct {
	Type        string        `json:"type"`
	Coordinates [][][]float64 `json:"coordinates"`
}

type tileProperties struct {
	TileID               int     `json:"tile_id"`
	PickupCount          int     `json:"pickup_count"`
	DropoffCount         int     `json:"dropoff_count"`
	AvailableDriverCount int     `json:"available_driver_count"`
	HotspotScore         float64 `json:"hotspot_score"`
	ColdScore            float64 `json:"cold_score"`
}

func main() {
	var opts options
	flag.StringVar(&opts.tileStatsPath, "tile-stats", "", "input tile_stats.csv path")
	flag.IntVar(&opts.tileGridCols, "tile-grid-cols", 100, "number of columns/rows used by simpleTile encoding")
	flag.StringVar(&opts.outputDir, "output-dir", ".", "directory for tile_stats.geojson")
	flag.Parse()

	if opts.tileStatsPath == "" {
		fatalf("missing -tile-stats")
	}
	if opts.tileGridCols <= 0 {
		fatalf("-tile-grid-cols must be positive")
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
	return nil
}

func loadTileStats(path string, gridCols int) ([]feature, error) {
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

	features := make([]feature, 0)
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

		features = append(features, feature{
			Type:       "Feature",
			Properties: props,
			Geometry:   tilePolygon(props.TileID, gridCols),
		})
	}
	return features, nil
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
	row := tileID / gridCols
	col := tileID % gridCols
	lonStep := (maxLon - minLon) / float64(gridCols)
	latStep := (maxLat - minLat) / float64(gridCols)

	west := minLon + float64(col)*lonStep
	east := west + lonStep
	south := minLat + float64(row)*latStep
	north := south + latStep

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

func fatalf(format string, args ...any) {
	fmt.Fprintf(os.Stderr, format+"\n", args...)
	os.Exit(1)
}
