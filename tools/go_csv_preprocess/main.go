package main

import (
	"encoding/csv"
	"flag"
	"fmt"
	"io"
	"math"
	"math/rand"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"
)

type csvRow map[string]string

type options struct {
	inputPath     string
	outputPath    string
	driversOutput string
	limit         int
	baseTime      string
	windowSeconds int64
	driverEvery   int
	driverRadius  float64
	seed          int64
}

func main() {
	var opts options
	flag.StringVar(&opts.inputPath, "input", "", "input Kaggle taxi trip CSV")
	flag.StringVar(&opts.outputPath, "output", "requests.csv", "output normalized requests CSV")
	flag.StringVar(&opts.driversOutput, "drivers-output", "drivers.csv", "output synthesized drivers CSV")
	flag.IntVar(&opts.limit, "limit", 1000, "maximum valid rows to write; <=0 means no limit")
	flag.StringVar(&opts.baseTime, "base-time", "", "optional base time in RFC3339 or taxi timestamp format")
	flag.Int64Var(&opts.windowSeconds, "window-seconds", 0, "optional continuous pickup-time window to sample before applying -limit; <=0 disables windowing")
	flag.IntVar(&opts.driverEvery, "driver-every", 2, "synthesize one driver for every N valid requests; 2 means about 0.5 driver per request")
	flag.Float64Var(&opts.driverRadius, "driver-radius", 0.003, "random driver offset radius around pickup point in lon/lat degrees")
	flag.Int64Var(&opts.seed, "seed", 20260503, "random seed for synthesized drivers")
	flag.Parse()

	if opts.inputPath == "" {
		fatalf("missing -input")
	}
	if opts.driverEvery <= 0 {
		fatalf("-driver-every must be positive")
	}
	if opts.driverRadius < 0 {
		fatalf("-driver-radius must be non-negative")
	}
	if opts.windowSeconds < 0 {
		fatalf("-window-seconds must be non-negative")
	}

	if err := convertRequestsAndDrivers(opts); err != nil {
		fatalf("%v", err)
	}
}

func convertRequestsAndDrivers(opts options) error {
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

	if err := ensureParentDir(opts.outputPath); err != nil {
		return err
	}
	output, err := os.Create(opts.outputPath)
	if err != nil {
		return err
	}
	defer output.Close()

	writer := csv.NewWriter(output)
	defer writer.Flush()

	if err := ensureParentDir(opts.driversOutput); err != nil {
		return err
	}
	driversOutput, err := os.Create(opts.driversOutput)
	if err != nil {
		return err
	}
	defer driversOutput.Close()

	driversWriter := csv.NewWriter(driversOutput)
	defer driversWriter.Flush()

	if err := writer.Write([]string{
		"request_id", "customer_id", "request_time", "pickup_x", "pickup_y",
		"dropoff_x", "dropoff_y", "pickup_tile", "dropoff_tile",
	}); err != nil {
		return err
	}

	if err := driversWriter.Write([]string{
		"taxi_id", "x", "y", "tile", "available_time", "status",
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

	rng := rand.New(rand.NewSource(opts.seed))
	requests := make([]normalizedRequest, 0)
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
		request, err := normalizeRequest(row, rowNumber)
		if err != nil {
			continue
		}

		requests = append(requests, request)
		if opts.windowSeconds <= 0 && opts.limit > 0 && len(requests) >= opts.limit {
			break
		}
	}

	if opts.windowSeconds > 0 {
		var err error
		requests, base, err = selectWindow(requests, base, opts.windowSeconds)
		if err != nil {
			return err
		}
		if opts.limit > 0 && len(requests) > opts.limit {
			requests = requests[:opts.limit]
		}
	}

	if len(requests) == 0 {
		fmt.Printf("wrote 0 normalized requests to %s\n", opts.outputPath)
		fmt.Printf("wrote 0 synthesized drivers to %s\n", opts.driversOutput)
		return nil
	}

	if base == nil {
		minPickupTime := requests[0].pickupTime
		for _, request := range requests[1:] {
			if request.pickupTime.Before(minPickupTime) {
				minPickupTime = request.pickupTime
			}
		}
		base = &minPickupTime
	}

	for index := range requests {
		requestTime := int64(requests[index].pickupTime.Sub(*base).Seconds())
		if requestTime < 0 {
			return fmt.Errorf("request_id=%d before base time", requests[index].requestID)
		}
		requests[index].requestTime = requestTime
	}

	sort.Slice(requests, func(i, j int) bool {
		if requests[i].requestTime != requests[j].requestTime {
			return requests[i].requestTime < requests[j].requestTime
		}
		return requests[i].requestID < requests[j].requestID
	})

	written := 0
	driversWritten := 0
	for _, request := range requests {
		if err := writer.Write(request.csvRecord()); err != nil {
			return err
		}
		written++

		if shouldSynthesizeDriver(written, opts.driverEvery) {
			driver := synthesizeDriver(request, driversWritten+1, opts.driverRadius, rng)
			if err := driversWriter.Write(driver.csvRecord()); err != nil {
				return err
			}
			driversWritten++
		}
	}

	if err := writer.Error(); err != nil {
		return err
	}
	if err := driversWriter.Error(); err != nil {
		return err
	}

	fmt.Printf("wrote %d normalized requests to %s\n", written, opts.outputPath)
	fmt.Printf("wrote %d synthesized drivers to %s\n", driversWritten, opts.driversOutput)
	if opts.windowSeconds > 0 {
		fmt.Printf("used pickup-time window starting at %s for %d seconds\n", base.Format("2006-01-02 15:04:05"), opts.windowSeconds)
	}
	return nil
}

func selectWindow(requests []normalizedRequest, base *time.Time, windowSeconds int64) ([]normalizedRequest, *time.Time, error) {
	if len(requests) == 0 {
		return requests, base, nil
	}

	sort.Slice(requests, func(i, j int) bool {
		if !requests[i].pickupTime.Equal(requests[j].pickupTime) {
			return requests[i].pickupTime.Before(requests[j].pickupTime)
		}
		return requests[i].requestID < requests[j].requestID
	})

	windowStart := requests[0].pickupTime
	if base != nil {
		windowStart = *base
	}
	windowEnd := windowStart.Add(time.Duration(windowSeconds) * time.Second)

	selected := make([]normalizedRequest, 0)
	for _, request := range requests {
		if request.pickupTime.Before(windowStart) {
			continue
		}
		if !request.pickupTime.Before(windowEnd) {
			break
		}
		selected = append(selected, request)
	}

	if len(selected) == 0 {
		return nil, &windowStart, fmt.Errorf("no valid requests in pickup-time window starting at %s for %d seconds", windowStart.Format("2006-01-02 15:04:05"), windowSeconds)
	}

	return selected, &windowStart, nil
}

type normalizedRequest struct {
	requestID   int
	customerID  int
	requestTime int64
	pickupX     float64
	pickupY     float64
	dropoffX    float64
	dropoffY    float64
	pickupTile  int
	dropoffTile int
	pickupTime  time.Time
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

type normalizedDriver struct {
	taxiID        int
	x             float64
	y             float64
	tile          int
	availableTime int64
	status        string
}

func (d normalizedDriver) csvRecord() []string {
	return []string{
		strconv.Itoa(d.taxiID),
		formatFloat(d.x),
		formatFloat(d.y),
		strconv.Itoa(d.tile),
		strconv.FormatInt(d.availableTime, 10),
		d.status,
	}
}

func shouldSynthesizeDriver(validRequestIndex int, driverEvery int) bool {
	return (validRequestIndex-1)%driverEvery == 0
}

func synthesizeDriver(request normalizedRequest, taxiID int, radius float64, rng *rand.Rand) normalizedDriver {
	x, y := randomPointInRadius(request.pickupX, request.pickupY, radius, rng)
	if !validNYCCoordinate(x, y) {
		x = request.pickupX
		y = request.pickupY
	}

	return normalizedDriver{
		taxiID:        taxiID,
		x:             x,
		y:             y,
		tile:          simpleTile(x, y),
		availableTime: 0,
		status:        "free",
	}
}

func randomPointInRadius(centerX, centerY, radius float64, rng *rand.Rand) (float64, float64) {
	if radius == 0 {
		return centerX, centerY
	}

	angle := rng.Float64() * 2.0 * math.Pi
	distance := math.Sqrt(rng.Float64()) * radius
	return centerX + math.Cos(angle)*distance, centerY + math.Sin(angle)*distance
}

func normalizeRequest(row csvRow, rowNumber int) (normalizedRequest, error) {
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

	requestID := rowNumber - 1
	if idText := first(row, "id", "trip_id"); idText != "" {
		if parsedID, err := parseStableID(idText); err == nil {
			requestID = parsedID
		}
	}

	return normalizedRequest{
		requestID:   requestID,
		customerID:  requestID,
		requestTime: 0,
		pickupX:     pickupX,
		pickupY:     pickupY,
		dropoffX:    dropoffX,
		dropoffY:    dropoffY,
		pickupTile:  simpleTile(pickupX, pickupY),
		dropoffTile: simpleTile(dropoffX, dropoffY),
		pickupTime:  pickupTime,
	}, nil
}

func ensureParentDir(path string) error {
	parent := filepath.Dir(path)
	if parent == "." || parent == "" {
		return nil
	}
	return os.MkdirAll(parent, 0755)
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
