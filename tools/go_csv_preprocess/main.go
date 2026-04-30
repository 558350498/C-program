package main

import (
	"encoding/csv"
	"flag"
	"fmt"
	"io"
	"math"
	"os"
	"strconv"
	"strings"
	"time"
)

type csvRow map[string]string

type options struct {
	inputPath  string
	outputPath string
	limit      int
	baseTime   string
}

func main() {
	var opts options
	flag.StringVar(&opts.inputPath, "input", "", "input Kaggle taxi trip CSV")
	flag.StringVar(&opts.outputPath, "output", "requests.csv", "output normalized requests CSV")
	flag.IntVar(&opts.limit, "limit", 1000, "maximum valid rows to write; <=0 means no limit")
	flag.StringVar(&opts.baseTime, "base-time", "", "optional base time in RFC3339 or taxi timestamp format")
	flag.Parse()

	if opts.inputPath == "" {
		fatalf("missing -input")
	}

	if err := convertRequests(opts); err != nil {
		fatalf("%v", err)
	}
}

func convertRequests(opts options) error {
	input, err := os.Open(opts.inputPath)
	if err != nil {
		return err
	}
	defer input.Close()

	reader := csv.NewReader(input)
	reader.FieldsPerRecord = -1
	reader.TrimLeadingSpace = true

	header, err := reader.Read()
	if err != nil {
		return fmt.Errorf("read header: %w", err)
	}
	normalizeHeader(header)

	output, err := os.Create(opts.outputPath)
	if err != nil {
		return err
	}
	defer output.Close()

	writer := csv.NewWriter(output)
	defer writer.Flush()

	if err := writer.Write([]string{
		"request_id", "customer_id", "request_time", "pickup_x", "pickup_y",
		"dropoff_x", "dropoff_y", "pickup_tile", "dropoff_tile",
	}); err != nil {
		return err
	}

	var base *time.Time
	if opts.baseTime != "" {
		parsed, err := parseTaxiTime(opts.baseTime)
		if err != nil {
			return fmt.Errorf("parse -base-time: %w", err)
		}
		base = &parsed
	}

	written := 0
	rowNumber := 1
	for {
		record, err := reader.Read()
		if err == io.EOF {
			break
		}
		if err != nil {
			return fmt.Errorf("read row %d: %w", rowNumber+1, err)
		}
		rowNumber++

		row := makeRow(header, record)
		request, err := normalizeRequest(row, rowNumber, base)
		if err != nil {
			continue
		}
		if base == nil {
			parsed := request.pickupTime
			base = &parsed
			request.requestTime = 0
		}

		if err := writer.Write(request.csvRecord()); err != nil {
			return err
		}
		written++
		if opts.limit > 0 && written >= opts.limit {
			break
		}
	}

	if err := writer.Error(); err != nil {
		return err
	}

	fmt.Printf("wrote %d normalized requests to %s\n", written, opts.outputPath)
	return nil
}

type normalizedRequest struct {
	requestID  int
	customerID int
	requestTime int64
	pickupX    float64
	pickupY    float64
	dropoffX   float64
	dropoffY   float64
	pickupTile int
	dropoffTile int
	pickupTime time.Time
}

func (r normalizedRequest) csvRecord() []string {
	return []string{
		strconv.Itoa(r.requestID),
		strconv.Itoa(r.customerID),
		strconv.FormatInt(r.requestTime, 10),
		formatFloat(r.pickupX),
		formatFloat(r.pickupY),
		formatFloat(r.dropoffX),
		formatFloat(r.dropoffY),
		strconv.Itoa(r.pickupTile),
		strconv.Itoa(r.dropoffTile),
	}
}

func normalizeRequest(row csvRow, rowNumber int, base *time.Time) (normalizedRequest, error) {
	pickupTime, err := parseTaxiTime(first(row, "pickup_datetime", "tpep_pickup_datetime", "lpep_pickup_datetime"))
	if err != nil {
		return normalizedRequest{}, err
	}

	pickupX, err := parseFloat(first(row, "pickup_longitude", "pickup_lon", "pickup_x"))
	if err != nil {
		return normalizedRequest{}, err
	}
	pickupY, err := parseFloat(first(row, "pickup_latitude", "pickup_lat", "pickup_y"))
	if err != nil {
		return normalizedRequest{}, err
	}
	dropoffX, err := parseFloat(first(row, "dropoff_longitude", "dropoff_lon", "dropoff_x"))
	if err != nil {
		return normalizedRequest{}, err
	}
	dropoffY, err := parseFloat(first(row, "dropoff_latitude", "dropoff_lat", "dropoff_y"))
	if err != nil {
		return normalizedRequest{}, err
	}

	if !validNYCCoordinate(pickupX, pickupY) || !validNYCCoordinate(dropoffX, dropoffY) {
		return normalizedRequest{}, fmt.Errorf("coordinates outside NYC bounds")
	}

	requestTime := int64(0)
	if base != nil {
		requestTime = int64(pickupTime.Sub(*base).Seconds())
		if requestTime < 0 {
			return normalizedRequest{}, fmt.Errorf("request before base time")
		}
	}

	requestID := rowNumber - 1
	if idText := first(row, "id", "trip_id"); idText != "" {
		if parsedID, err := parseStableID(idText); err == nil {
			requestID = parsedID
		}
	}

	return normalizedRequest{
		requestID: requestID,
		customerID: requestID,
		requestTime: requestTime,
		pickupX: pickupX,
		pickupY: pickupY,
		dropoffX: dropoffX,
		dropoffY: dropoffY,
		pickupTile: simpleTile(pickupX, pickupY),
		dropoffTile: simpleTile(dropoffX, dropoffY),
		pickupTime: pickupTime,
	}, nil
}

func normalizeHeader(header []string) {
	for i, value := range header {
		header[i] = normalizeName(value)
	}
}

func makeRow(header []string, record []string) csvRow {
	row := csvRow{}
	for i, name := range header {
		if i < len(record) {
			row[name] = strings.TrimSpace(record[i])
		}
	}
	return row
}

func normalizeName(value string) string {
	return strings.ToLower(strings.TrimSpace(strings.ReplaceAll(value, " ", "_")))
}

func first(row csvRow, names ...string) string {
	for _, name := range names {
		if value := strings.TrimSpace(row[normalizeName(name)]); value != "" {
			return value
		}
	}
	return ""
}

func parseTaxiTime(value string) (time.Time, error) {
	value = strings.TrimSpace(value)
	layouts := []string{
		time.RFC3339,
		"2006-01-02 15:04:05",
		"2006-01-02 15:04:05 UTC",
		"1/2/2006 15:04",
		"1/2/2006 15:04:05",
	}
	for _, layout := range layouts {
		if parsed, err := time.Parse(layout, value); err == nil {
			return parsed, nil
		}
	}
	return time.Time{}, fmt.Errorf("unsupported time %q", value)
}

func parseFloat(value string) (float64, error) {
	parsed, err := strconv.ParseFloat(strings.TrimSpace(value), 64)
	if err != nil {
		return 0, err
	}
	if math.IsNaN(parsed) || math.IsInf(parsed, 0) {
		return 0, fmt.Errorf("invalid float")
	}
	return parsed, nil
}

func parseStableID(value string) (int, error) {
	if parsed, err := strconv.Atoi(value); err == nil && parsed >= 0 {
		return parsed, nil
	}
	hash := 0
	for _, ch := range value {
		hash = (hash*131 + int(ch)) & 0x7fffffff
	}
	if hash == 0 {
		return 0, fmt.Errorf("empty stable id")
	}
	return hash, nil
}

func validNYCCoordinate(lon, lat float64) bool {
	return lon >= -75.0 && lon <= -72.0 && lat >= 40.0 && lat <= 42.0
}

func simpleTile(lon, lat float64) int {
	const cols = 100
	const minLon = -75.0
	const maxLon = -72.0
	const minLat = 40.0
	const maxLat = 42.0

	col := int((lon - minLon) / (maxLon - minLon) * cols)
	row := int((lat - minLat) / (maxLat - minLat) * cols)
	if col < 0 {
		col = 0
	}
	if col >= cols {
		col = cols - 1
	}
	if row < 0 {
		row = 0
	}
	if row >= cols {
		row = cols - 1
	}
	return row*cols + col
}

func formatFloat(value float64) string {
	return strconv.FormatFloat(value, 'f', 6, 64)
}

func fatalf(format string, args ...any) {
	fmt.Fprintf(os.Stderr, format+"\n", args...)
	os.Exit(1)
}
