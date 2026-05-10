package main

import (
	"bytes"
	"encoding/csv"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
)

type options struct {
	rawInput               string
	outputDir              string
	outputCSV              string
	preprocessDir          string
	experimentsDir         string
	kSweepPath             string
	limits                 string
	modes                  string
	radii                  string
	kValues                string
	tileGridCols           string
	windowSeconds          int64
	driverEvery            int
	driverRadius           float64
	seed                   int64
	secondsPerDistanceUnit float64
	farePerKm              float64
	pickupCostPerKm        float64
	kmPerDegree            float64
	hotspotTrials          int
	pickupHotWeight        float64
	priceFloor             float64
	priceCap               float64
	pricingMode            string
	coldDropoffPenalty     float64
	hotDropoffDiscount     float64
	zoneFixedPricing       bool
	zoneFixedBaseFare      float64
	hotHotFactor           float64
	coldColdFactor         float64
}

type normalizedPaths struct {
	requests    string
	drivers     string
	tileStats   string
	regionMap   string
	regionStats string
}

type gridSummaryOutput struct {
	file   *os.File
	writer *csv.Writer
}

func main() {
	var opts options
	flag.StringVar(&opts.rawInput, "input", "../../data/datasets/nyc-taxi-trip-duration/raw/NYC.csv", "raw Kaggle taxi CSV")
	flag.StringVar(&opts.outputDir, "output-dir", "../../build-local/perf-sweeps", "directory for generated normalized data and summary CSV")
	flag.StringVar(&opts.outputCSV, "output-csv", "", "combined summary CSV path; defaults to output-dir/summary.csv")
	flag.StringVar(&opts.preprocessDir, "preprocess-dir", "../go_csv_preprocess", "go_csv_preprocess tool directory")
	flag.StringVar(&opts.experimentsDir, "experiments-dir", "../go_experiments", "go_experiments tool directory")
	flag.StringVar(&opts.kSweepPath, "k-sweep", "../../build-local/k_sweep.exe", "compiled k_sweep executable path")
	flag.StringVar(&opts.limits, "limits", "1000,5000,20000", "comma-separated request limits")
	flag.StringVar(&opts.modes, "modes", "scan,indexed", "comma-separated candidate modes: scan,indexed")
	flag.StringVar(&opts.radii, "radii", "0.01,0.03,0.05", "comma-separated candidate radii")
	flag.StringVar(&opts.kValues, "k-values", "1,2,5,unlimited", "comma-separated k values")
	flag.StringVar(&opts.tileGridCols, "tile-grid-cols", "100", "comma-separated tile grid columns/rows to preprocess and sweep")
	flag.Int64Var(&opts.windowSeconds, "window-seconds", 86400, "continuous pickup-time window for preprocessing")
	flag.IntVar(&opts.driverEvery, "driver-every", 2, "synthesize one driver for every N valid requests")
	flag.Float64Var(&opts.driverRadius, "driver-radius", 0.003, "random driver offset radius around pickup point")
	flag.Int64Var(&opts.seed, "seed", 20260503, "random seed for preprocessing and hotspot trials")
	flag.Float64Var(&opts.secondsPerDistanceUnit, "seconds-per-distance-unit", 100000.0, "pickup cost scale used by replay")
	flag.Float64Var(&opts.farePerKm, "fare-per-km", 1.0, "fare revenue per completed trip kilometer")
	flag.Float64Var(&opts.pickupCostPerKm, "pickup-cost-per-km", 1.0, "cost per pickup/deadhead kilometer")
	flag.Float64Var(&opts.kmPerDegree, "km-per-degree", 111.0, "rough conversion from replay degree distance to kilometers")
	flag.IntVar(&opts.hotspotTrials, "hotspot-trials", 0, "random hotspot pricing trials per sweep row")
	flag.Float64Var(&opts.pickupHotWeight, "pickup-hot-weight", 0.15, "fixed pricing pickup hotspot weight")
	flag.Float64Var(&opts.priceFloor, "price-floor", 0.8, "minimum hotspot price factor")
	flag.Float64Var(&opts.priceCap, "price-cap", 1.8, "maximum hotspot price factor")
	flag.StringVar(&opts.pricingMode, "pricing-mode", "linear", "fixed pricing mode: linear or diminishing")
	flag.Float64Var(&opts.coldDropoffPenalty, "cold-dropoff-penalty", 0.20, "opportunity cold dropoff penalty passed to k_sweep and fixed pricing")
	flag.Float64Var(&opts.hotDropoffDiscount, "hot-dropoff-discount", 0.10, "opportunity hot dropoff discount passed to k_sweep and fixed pricing")
	flag.BoolVar(&opts.zoneFixedPricing, "zone-fixed-pricing", false, "estimate fixed per-trip pricing by hot/cold pickup/dropoff zones")
	flag.Float64Var(&opts.zoneFixedBaseFare, "zone-fixed-base-fare", 1.0, "base fare for zone-fixed pricing")
	flag.Float64Var(&opts.hotHotFactor, "hot-hot-factor", 1.2, "zone-fixed factor for hot pickup to hot dropoff")
	flag.Float64Var(&opts.coldColdFactor, "cold-cold-factor", 0.8, "zone-fixed factor for cold pickup to cold dropoff")
	flag.Parse()

	if err := run(opts); err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}

func run(opts options) error {
	limits, err := parsePositiveIntList(opts.limits, "-limits")
	if err != nil {
		return err
	}
	gridColsList, err := parsePositiveIntList(opts.tileGridCols, "-tile-grid-cols")
	if err != nil {
		return err
	}
	modes, err := parseModes(opts.modes)
	if err != nil {
		return err
	}
	if opts.driverEvery <= 0 {
		return fmt.Errorf("-driver-every must be positive")
	}
	if opts.driverRadius < 0 {
		return fmt.Errorf("-driver-radius must be non-negative")
	}
	if opts.windowSeconds < 0 {
		return fmt.Errorf("-window-seconds must be non-negative")
	}
	if opts.secondsPerDistanceUnit <= 0 {
		return fmt.Errorf("-seconds-per-distance-unit must be positive")
	}
	if opts.pickupHotWeight < 0 {
		return fmt.Errorf("-pickup-hot-weight must be non-negative")
	}
	if opts.coldDropoffPenalty < 0 {
		return fmt.Errorf("-cold-dropoff-penalty must be non-negative")
	}
	if opts.hotDropoffDiscount < 0 {
		return fmt.Errorf("-hot-dropoff-discount must be non-negative")
	}
	if opts.pricingMode != "linear" && opts.pricingMode != "diminishing" {
		return fmt.Errorf("-pricing-mode must be linear or diminishing")
	}
	if opts.zoneFixedBaseFare < 0 {
		return fmt.Errorf("-zone-fixed-base-fare must be non-negative")
	}
	if opts.hotHotFactor <= 0 {
		return fmt.Errorf("-hot-hot-factor must be positive")
	}
	if opts.coldColdFactor <= 0 {
		return fmt.Errorf("-cold-cold-factor must be positive")
	}
	if opts.outputCSV == "" {
		opts.outputCSV = filepath.Join(opts.outputDir, "summary.csv")
	}

	rawInput, err := absPath(opts.rawInput)
	if err != nil {
		return err
	}
	outputDir, err := absPath(opts.outputDir)
	if err != nil {
		return err
	}
	outputCSV, err := absPath(opts.outputCSV)
	if err != nil {
		return err
	}
	preprocessDir, err := absPath(opts.preprocessDir)
	if err != nil {
		return err
	}
	experimentsDir, err := absPath(opts.experimentsDir)
	if err != nil {
		return err
	}
	kSweepPath, err := absPath(opts.kSweepPath)
	if err != nil {
		return err
	}

	if err := os.MkdirAll(filepath.Dir(outputCSV), 0755); err != nil {
		return err
	}
	output, err := os.Create(outputCSV)
	if err != nil {
		return err
	}
	defer output.Close()

	writer := csv.NewWriter(output)
	defer writer.Flush()

	var wroteHeader bool
	var experimentHeader []string
	var summaryHeader []string
	gridSummaries := map[int]*gridSummaryOutput{}
	defer closeGridSummaries(gridSummaries)
	for _, gridCols := range gridColsList {
		for _, limit := range limits {
			normalizedDir := filepath.Join(outputDir, "normalized",
				fmt.Sprintf("grid_%d", gridCols), fmt.Sprintf("limit_%d", limit))
			paths := normalizedPaths{
				requests:    filepath.Join(normalizedDir, "requests.csv"),
				drivers:     filepath.Join(normalizedDir, "drivers.csv"),
				tileStats:   filepath.Join(normalizedDir, "tile_stats.csv"),
				regionMap:   filepath.Join(normalizedDir, "region_map.csv"),
				regionStats: filepath.Join(normalizedDir, "region_stats.csv"),
			}

			fmt.Fprintf(os.Stderr, "preprocess grid=%d limit=%d\n", gridCols, limit)
			if err := runPreprocess(opts, preprocessDir, rawInput, paths, limit, gridCols); err != nil {
				return err
			}

			for _, mode := range modes {
				fmt.Fprintf(os.Stderr, "experiment grid=%d limit=%d mode=%s\n", gridCols, limit, mode)
				records, err := runExperiment(opts, experimentsDir, kSweepPath, paths, mode, gridCols)
				if err != nil {
					return err
				}
				if len(records) < 2 {
					return fmt.Errorf("experiment grid=%d limit=%d mode=%s returned no data rows", gridCols, limit, mode)
				}

				if !wroteHeader {
					experimentHeader = append([]string{}, records[0]...)
					header := append([]string{
						"sample_limit",
						"tile_grid_cols",
						"window_seconds",
						"driver_every",
						"driver_radius",
					}, experimentHeader...)
					if err := writer.Write(header); err != nil {
						return err
					}
					summaryHeader = append([]string{}, header...)
					wroteHeader = true
				} else if !sameStrings(experimentHeader, records[0]) {
					return fmt.Errorf("experiment header changed for grid=%d limit=%d mode=%s", gridCols, limit, mode)
				}

				for _, record := range records[1:] {
					outputRecord := append([]string{
						strconv.Itoa(limit),
						strconv.Itoa(gridCols),
						strconv.FormatInt(opts.windowSeconds, 10),
						strconv.Itoa(opts.driverEvery),
						formatFloat(opts.driverRadius),
					}, record...)
					if err := writer.Write(outputRecord); err != nil {
						return err
					}
					if err := writeGridSummaryRecord(gridSummaries, outputDir, gridCols, summaryHeader, outputRecord); err != nil {
						return err
					}
				}
				writer.Flush()
				if err := writer.Error(); err != nil {
					return err
				}
				if err := flushGridSummary(gridSummaries, gridCols); err != nil {
					return err
				}
			}
		}
	}

	fmt.Fprintf(os.Stderr, "wrote summary CSV: %s\n", outputCSV)
	return writer.Error()
}

func writeGridSummaryRecord(outputs map[int]*gridSummaryOutput, outputDir string, gridCols int, header []string, record []string) error {
	output, ok := outputs[gridCols]
	if !ok {
		path := filepath.Join(outputDir, fmt.Sprintf("grid_%d", gridCols), "summary.csv")
		if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
			return err
		}
		file, err := os.Create(path)
		if err != nil {
			return err
		}
		output = &gridSummaryOutput{
			file:   file,
			writer: csv.NewWriter(file),
		}
		outputs[gridCols] = output
		if err := output.writer.Write(header); err != nil {
			return err
		}
	}
	return output.writer.Write(record)
}

func flushGridSummary(outputs map[int]*gridSummaryOutput, gridCols int) error {
	output, ok := outputs[gridCols]
	if !ok {
		return nil
	}
	output.writer.Flush()
	return output.writer.Error()
}

func closeGridSummaries(outputs map[int]*gridSummaryOutput) {
	for _, output := range outputs {
		if output.writer != nil {
			output.writer.Flush()
		}
		if output.file != nil {
			_ = output.file.Close()
		}
	}
}

func runPreprocess(opts options, preprocessDir string, rawInput string, paths normalizedPaths, limit int, gridCols int) error {
	if err := os.MkdirAll(filepath.Dir(paths.requests), 0755); err != nil {
		return err
	}
	args := []string{
		"run", ".",
		"-input", rawInput,
		"-output", paths.requests,
		"-drivers-output", paths.drivers,
		"-window-seconds", strconv.FormatInt(opts.windowSeconds, 10),
		"-limit", strconv.Itoa(limit),
		"-driver-every", strconv.Itoa(opts.driverEvery),
		"-driver-radius", strconv.FormatFloat(opts.driverRadius, 'f', -1, 64),
		"-tile-grid-cols", strconv.Itoa(gridCols),
		"-seed", strconv.FormatInt(opts.seed, 10),
	}
	return runCommand(preprocessDir, "go", args...)
}

func runExperiment(opts options, experimentsDir string, kSweepPath string, paths normalizedPaths, mode string, gridCols int) ([][]string, error) {
	args := []string{
		"run", ".",
		"-requests", paths.requests,
		"-drivers", paths.drivers,
		"-k-sweep", kSweepPath,
		"-radii", opts.radii,
		"-k-values", opts.kValues,
		"-seconds-per-distance-unit", strconv.FormatFloat(opts.secondsPerDistanceUnit, 'f', -1, 64),
		"-fare-per-km", strconv.FormatFloat(opts.farePerKm, 'f', -1, 64),
		"-pickup-cost-per-km", strconv.FormatFloat(opts.pickupCostPerKm, 'f', -1, 64),
		"-km-per-degree", strconv.FormatFloat(opts.kmPerDegree, 'f', -1, 64),
		"-hotspot-trials", strconv.Itoa(opts.hotspotTrials),
		"-seed", strconv.FormatInt(opts.seed, 10),
		"-pickup-hot-weight", strconv.FormatFloat(opts.pickupHotWeight, 'f', -1, 64),
		"-price-floor", strconv.FormatFloat(opts.priceFloor, 'f', -1, 64),
		"-price-cap", strconv.FormatFloat(opts.priceCap, 'f', -1, 64),
		"-pricing-mode", opts.pricingMode,
		"-cold-dropoff-penalty", strconv.FormatFloat(opts.coldDropoffPenalty, 'f', -1, 64),
		"-hot-dropoff-discount", strconv.FormatFloat(opts.hotDropoffDiscount, 'f', -1, 64),
		"-tile-grid-cols", strconv.Itoa(gridCols),
		"-tile-stats-csv", paths.tileStats,
		"-region-map-csv", paths.regionMap,
		"-region-stats-csv", paths.regionStats,
		"-zone-fixed-base-fare", strconv.FormatFloat(opts.zoneFixedBaseFare, 'f', -1, 64),
		"-hot-hot-factor", strconv.FormatFloat(opts.hotHotFactor, 'f', -1, 64),
		"-cold-cold-factor", strconv.FormatFloat(opts.coldColdFactor, 'f', -1, 64),
	}
	if opts.zoneFixedPricing {
		args = append(args, "-zone-fixed-pricing")
	}
	if mode == "indexed" {
		args = append(args, "-indexed-candidates")
	}

	output, err := commandOutput(experimentsDir, "go", args...)
	if err != nil {
		return nil, err
	}

	reader := csv.NewReader(bytes.NewReader(output))
	records, err := reader.ReadAll()
	if err != nil {
		return nil, fmt.Errorf("parse go_experiments CSV: %w", err)
	}
	return records, nil
}

func commandOutput(dir string, name string, args ...string) ([]byte, error) {
	cmd := exec.Command(name, args...)
	cmd.Dir = dir
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	output, err := cmd.Output()
	if err != nil {
		return nil, fmt.Errorf("%s %s: %w: %s", name, strings.Join(args, " "), err, strings.TrimSpace(stderr.String()))
	}
	return output, nil
}

func runCommand(dir string, name string, args ...string) error {
	cmd := exec.Command(name, args...)
	cmd.Dir = dir
	cmd.Stdout = os.Stderr
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("%s %s: %w: %s", name, strings.Join(args, " "), err, strings.TrimSpace(stderr.String()))
	}
	if stderr.Len() > 0 {
		fmt.Fprint(os.Stderr, stderr.String())
	}
	return nil
}

func parsePositiveIntList(text string, name string) ([]int, error) {
	parts := splitList(text)
	if len(parts) == 0 {
		return nil, fmt.Errorf("%s must contain at least one value", name)
	}
	values := make([]int, 0, len(parts))
	for _, part := range parts {
		value, err := strconv.Atoi(part)
		if err != nil || value <= 0 {
			return nil, fmt.Errorf("invalid %s value: %s", name, part)
		}
		values = append(values, value)
	}
	return values, nil
}

func parseModes(text string) ([]string, error) {
	parts := splitList(text)
	if len(parts) == 0 {
		return nil, fmt.Errorf("-modes must contain at least one value")
	}
	modes := make([]string, 0, len(parts))
	for _, part := range parts {
		switch part {
		case "scan", "indexed":
			modes = append(modes, part)
		default:
			return nil, fmt.Errorf("invalid -modes value: %s", part)
		}
	}
	return modes, nil
}

func splitList(text string) []string {
	raw := strings.Split(text, ",")
	values := make([]string, 0, len(raw))
	for _, value := range raw {
		value = strings.TrimSpace(value)
		if value != "" {
			values = append(values, value)
		}
	}
	return values
}

func absPath(path string) (string, error) {
	if path == "" {
		return "", fmt.Errorf("empty path")
	}
	absolute, err := filepath.Abs(filepath.FromSlash(path))
	if err != nil {
		return "", err
	}
	return absolute, nil
}

func sameStrings(lhs []string, rhs []string) bool {
	if len(lhs) != len(rhs) {
		return false
	}
	for index := range lhs {
		if lhs[index] != rhs[index] {
			return false
		}
	}
	return true
}

func formatFloat(value float64) string {
	return strconv.FormatFloat(value, 'f', 6, 64)
}
