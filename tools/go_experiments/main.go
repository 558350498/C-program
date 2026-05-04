package main

import (
	"bytes"
	"encoding/csv"
	"flag"
	"fmt"
	"math"
	"math/rand"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
)

type options struct {
	requestsPath           string
	driversPath            string
	kSweepPath             string
	radii                  string
	kValues                string
	secondsPerDistanceUnit float64
	farePerKm              float64
	pickupCostPerKm        float64
	kmPerDegree            float64
	hotspotTrials          int
	seed                   int64
	priceFloor             float64
	priceCap               float64
}

type requestRow struct {
	pickupLon   float64
	pickupLat   float64
	dropoffLon  float64
	dropoffLat  float64
	pickupTile  int
	dropoffTile int
}

func main() {
	var opts options
	flag.StringVar(&opts.requestsPath, "requests", "../../data/normalized/requests.csv", "normalized requests.csv path")
	flag.StringVar(&opts.driversPath, "drivers", "../../data/normalized/drivers.csv", "normalized drivers.csv path")
	flag.StringVar(&opts.kSweepPath, "k-sweep", "../../build-mingw/k_sweep.exe", "compiled k_sweep executable path")
	flag.StringVar(&opts.radii, "radii", "0.01,0.03,0.05", "comma-separated candidate radii")
	flag.StringVar(&opts.kValues, "k-values", "1,2,5,unlimited", "comma-separated k values")
	flag.Float64Var(&opts.secondsPerDistanceUnit, "seconds-per-distance-unit", 100000.0, "pickup cost scale used by replay")
	flag.Float64Var(&opts.farePerKm, "fare-per-km", 1.0, "fare revenue per completed trip kilometer")
	flag.Float64Var(&opts.pickupCostPerKm, "pickup-cost-per-km", 1.0, "cost per pickup/deadhead kilometer")
	flag.Float64Var(&opts.kmPerDegree, "km-per-degree", 111.0, "rough conversion from replay degree distance to kilometers")
	flag.IntVar(&opts.hotspotTrials, "hotspot-trials", 0, "random hotspot pricing trials per sweep row; 0 disables hotspot pricing")
	flag.Int64Var(&opts.seed, "seed", 20260504, "random seed for hotspot pricing trials")
	flag.Float64Var(&opts.priceFloor, "price-floor", 0.8, "minimum hotspot price factor")
	flag.Float64Var(&opts.priceCap, "price-cap", 1.8, "maximum hotspot price factor")
	flag.Parse()

	if err := run(opts); err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}

func run(opts options) error {
	if opts.secondsPerDistanceUnit <= 0 {
		return fmt.Errorf("-seconds-per-distance-unit must be positive")
	}
	if opts.farePerKm < 0 || opts.pickupCostPerKm < 0 {
		return fmt.Errorf("fare and cost values must be non-negative")
	}
	if opts.kmPerDegree <= 0 {
		return fmt.Errorf("-km-per-degree must be positive")
	}
	if opts.hotspotTrials < 0 {
		return fmt.Errorf("-hotspot-trials must be non-negative")
	}
	if opts.priceFloor <= 0 {
		return fmt.Errorf("-price-floor must be positive")
	}
	if opts.priceCap < opts.priceFloor {
		return fmt.Errorf("-price-cap must be greater than or equal to -price-floor")
	}

	requests, err := loadRequests(opts.requestsPath)
	if err != nil {
		return err
	}
	totalTripKm := sumTripKm(requests)

	rows, err := runKSweep(opts)
	if err != nil {
		return err
	}
	if len(rows) == 0 {
		return fmt.Errorf("k_sweep returned no rows")
	}

	writer := csv.NewWriter(os.Stdout)
	defer writer.Flush()

	header := append([]string{}, rows[0].header...)
	header = append(header,
		"supply_demand_ratio",
		"total_trip_km",
		"gross_possible_revenue",
		"estimated_completed_revenue",
		"estimated_pickup_km",
		"estimated_pickup_cost",
		"estimated_net_revenue",
	)
	if opts.hotspotTrials > 0 {
		header = append(header,
			"trial",
			"pickup_hot_weight",
			"dropoff_hot_discount",
			"cold_dropoff_penalty",
			"pricing_mode",
			"avg_price_factor",
			"max_price_factor",
			"hotspot_completed_revenue",
			"hotspot_net_revenue",
			"hotspot_net_delta",
		)
	}
	if err := writer.Write(header); err != nil {
		return err
	}

	hotspot := buildHotspotSideTable(requests)
	rng := rand.New(rand.NewSource(opts.seed))

	for _, row := range rows {
		completionRate, err := row.float("completion_rate")
		if err != nil {
			return err
		}
		requestCount, err := row.float("requests")
		if err != nil {
			return err
		}
		driverCount, err := row.float("drivers")
		if err != nil {
			return err
		}
		appliedPickupCost, err := row.float("applied_pickup_cost")
		if err != nil {
			return err
		}

		supplyDemandRatio := 0.0
		if requestCount > 0 {
			supplyDemandRatio = driverCount / requestCount
		}
		grossPossibleRevenue := totalTripKm * opts.farePerKm
		estimatedCompletedRevenue := grossPossibleRevenue * completionRate
		estimatedPickupKm := appliedPickupCost / opts.secondsPerDistanceUnit * opts.kmPerDegree
		estimatedPickupCost := estimatedPickupKm * opts.pickupCostPerKm
		estimatedNetRevenue := estimatedCompletedRevenue - estimatedPickupCost

		baseOutput := append([]string{}, row.values...)
		baseOutput = append(baseOutput,
			formatFloat(supplyDemandRatio),
			formatFloat(totalTripKm),
			formatFloat(grossPossibleRevenue),
			formatFloat(estimatedCompletedRevenue),
			formatFloat(estimatedPickupKm),
			formatFloat(estimatedPickupCost),
			formatFloat(estimatedNetRevenue),
		)

		if opts.hotspotTrials == 0 {
			if err := writer.Write(baseOutput); err != nil {
				return err
			}
			continue
		}

		for trial := 1; trial <= opts.hotspotTrials; trial++ {
			params := randomHotspotParams(rng)
			for _, mode := range []string{"linear", "diminishing"} {
				hotspotRevenue, avgFactor, maxFactor := hotspotRevenue(
					requests, hotspot, opts.farePerKm, completionRate, params,
					mode, opts.priceFloor, opts.priceCap)
				hotspotNet := hotspotRevenue - estimatedPickupCost
				output := append([]string{}, baseOutput...)
				output = append(output,
					strconv.Itoa(trial),
					formatFloat(params.pickupHotWeight),
					formatFloat(params.dropoffHotDiscount),
					formatFloat(params.coldDropoffPenalty),
					mode,
					formatFloat(avgFactor),
					formatFloat(maxFactor),
					formatFloat(hotspotRevenue),
					formatFloat(hotspotNet),
					formatFloat(hotspotNet-estimatedNetRevenue),
				)
				if err := writer.Write(output); err != nil {
					return err
				}
			}
		}
	}

	return writer.Error()
}

type sweepRow struct {
	header []string
	values []string
	index  map[string]int
}

func (r sweepRow) float(name string) (float64, error) {
	idx, ok := r.index[name]
	if !ok || idx < 0 || idx >= len(r.values) {
		return 0, fmt.Errorf("missing k_sweep column %q", name)
	}
	value, err := strconv.ParseFloat(r.values[idx], 64)
	if err != nil {
		return 0, fmt.Errorf("parse column %s=%q: %w", name, r.values[idx], err)
	}
	return value, nil
}

func runKSweep(opts options) ([]sweepRow, error) {
	kSweepPath := filepath.FromSlash(opts.kSweepPath)
	cmd := exec.Command(kSweepPath,
		"--requests", filepath.FromSlash(opts.requestsPath),
		"--drivers", filepath.FromSlash(opts.driversPath),
		"--radii", opts.radii,
		"--k-values", opts.kValues,
		"--seconds-per-distance-unit", strconv.FormatFloat(opts.secondsPerDistanceUnit, 'f', -1, 64),
	)
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	output, err := cmd.Output()
	if err != nil {
		return nil, fmt.Errorf("run k_sweep: %w: %s", err, strings.TrimSpace(stderr.String()))
	}

	reader := csv.NewReader(bytes.NewReader(output))
	records, err := reader.ReadAll()
	if err != nil {
		return nil, fmt.Errorf("parse k_sweep CSV: %w", err)
	}
	if len(records) < 2 {
		return nil, nil
	}

	header := records[0]
	index := make(map[string]int, len(header))
	for i, name := range header {
		index[name] = i
	}

	rows := make([]sweepRow, 0, len(records)-1)
	for _, record := range records[1:] {
		rows = append(rows, sweepRow{
			header: header,
			values: record,
			index:  index,
		})
	}
	return rows, nil
}

func loadRequests(path string) ([]requestRow, error) {
	file, err := os.Open(filepath.FromSlash(path))
	if err != nil {
		return nil, err
	}
	defer file.Close()

	reader := csv.NewReader(file)
	records, err := reader.ReadAll()
	if err != nil {
		return nil, err
	}
	if len(records) < 2 {
		return nil, fmt.Errorf("no request rows in %s", path)
	}

	header := records[0]
	index := make(map[string]int, len(header))
	for i, name := range header {
		index[strings.TrimSpace(name)] = i
	}

	requests := make([]requestRow, 0, len(records)-1)
	for rowNumber, record := range records[1:] {
		request, err := parseRequestRow(index, record)
		if err != nil {
			return nil, fmt.Errorf("row %d: %w", rowNumber+2, err)
		}
		requests = append(requests, request)
	}
	return requests, nil
}

func parseRequestRow(index map[string]int, record []string) (requestRow, error) {
	pickupLon, err := parseColumn(index, record, "pickup_x")
	if err != nil {
		return requestRow{}, err
	}
	pickupLat, err := parseColumn(index, record, "pickup_y")
	if err != nil {
		return requestRow{}, err
	}
	dropoffLon, err := parseColumn(index, record, "dropoff_x")
	if err != nil {
		return requestRow{}, err
	}
	dropoffLat, err := parseColumn(index, record, "dropoff_y")
	if err != nil {
		return requestRow{}, err
	}
	pickupTile, err := parseIntColumn(index, record, "pickup_tile")
	if err != nil {
		return requestRow{}, err
	}
	dropoffTile, err := parseIntColumn(index, record, "dropoff_tile")
	if err != nil {
		return requestRow{}, err
	}
	return requestRow{
		pickupLon:   pickupLon,
		pickupLat:   pickupLat,
		dropoffLon:  dropoffLon,
		dropoffLat:  dropoffLat,
		pickupTile:  pickupTile,
		dropoffTile: dropoffTile,
	}, nil
}

func parseColumn(index map[string]int, record []string, name string) (float64, error) {
	idx, ok := index[name]
	if !ok || idx < 0 || idx >= len(record) {
		return 0, fmt.Errorf("missing column %s", name)
	}
	value, err := strconv.ParseFloat(strings.TrimSpace(record[idx]), 64)
	if err != nil {
		return 0, fmt.Errorf("parse column %s=%q: %w", name, record[idx], err)
	}
	return value, nil
}

func parseIntColumn(index map[string]int, record []string, name string) (int, error) {
	idx, ok := index[name]
	if !ok || idx < 0 || idx >= len(record) {
		return 0, fmt.Errorf("missing column %s", name)
	}
	value, err := strconv.Atoi(strings.TrimSpace(record[idx]))
	if err != nil {
		return 0, fmt.Errorf("parse column %s=%q: %w", name, record[idx], err)
	}
	return value, nil
}

type hotspotSideTable struct {
	pickupHeatByTile map[int]int
	maxPickupHeat    int
}

type hotspotParams struct {
	pickupHotWeight    float64
	dropoffHotDiscount float64
	coldDropoffPenalty float64
}

func buildHotspotSideTable(requests []requestRow) hotspotSideTable {
	table := hotspotSideTable{
		pickupHeatByTile: map[int]int{},
		maxPickupHeat:    1,
	}
	for _, request := range requests {
		table.pickupHeatByTile[request.pickupTile]++
		if table.pickupHeatByTile[request.pickupTile] > table.maxPickupHeat {
			table.maxPickupHeat = table.pickupHeatByTile[request.pickupTile]
		}
	}
	return table
}

func randomHotspotParams(rng *rand.Rand) hotspotParams {
	return hotspotParams{
		pickupHotWeight:    rng.Float64() * 0.50,
		dropoffHotDiscount: rng.Float64() * 0.20,
		coldDropoffPenalty: rng.Float64() * 0.30,
	}
}

func hotspotRevenue(requests []requestRow, table hotspotSideTable, farePerKm float64, completionRate float64, params hotspotParams, mode string, priceFloor float64, priceCap float64) (float64, float64, float64) {
	totalRevenue := 0.0
	totalFactor := 0.0
	maxFactor := 0.0
	for _, request := range requests {
		pickupHeat := normalizedHeat(table.pickupHeatByTile[request.pickupTile], table.maxPickupHeat)
		dropoffHeat := normalizedHeat(table.pickupHeatByTile[request.dropoffTile], table.maxPickupHeat)
		coldDropoff := 1.0 - dropoffHeat
		if mode == "diminishing" {
			pickupHeat = diminishingReturn(pickupHeat)
			dropoffHeat = diminishingReturn(dropoffHeat)
			coldDropoff = diminishingReturn(coldDropoff)
		}
		factor := 1.0 +
			params.pickupHotWeight*pickupHeat -
			params.dropoffHotDiscount*dropoffHeat +
			params.coldDropoffPenalty*coldDropoff
		factor = clamp(factor, priceFloor, priceCap)
		if factor > maxFactor {
			maxFactor = factor
		}
		totalFactor += factor
		totalRevenue += haversineKm(request.pickupLat, request.pickupLon, request.dropoffLat, request.dropoffLon) * farePerKm * factor
	}
	if len(requests) == 0 {
		return 0.0, 0.0, 0.0
	}
	return totalRevenue * completionRate, totalFactor / float64(len(requests)), maxFactor
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

func normalizedHeat(value int, maxValue int) float64 {
	if maxValue <= 0 {
		return 0.0
	}
	return float64(value) / float64(maxValue)
}

func diminishingReturn(value float64) float64 {
	if value <= 0.0 {
		return 0.0
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

func sumTripKm(requests []requestRow) float64 {
	total := 0.0
	for _, request := range requests {
		total += haversineKm(request.pickupLat, request.pickupLon, request.dropoffLat, request.dropoffLon)
	}
	return total
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
	return value * math.Pi / 180.0
}

func formatFloat(value float64) string {
	return strconv.FormatFloat(value, 'f', 4, 64)
}
