package main

import (
	"encoding/csv"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
)

type options struct {
	inputPath              string
	minCompletionRate      float64
	maxRowsPerSample       int
	includeAllCombinations bool
}

type experimentRow struct {
	values map[string]string
}

func main() {
	var opts options
	flag.StringVar(&opts.inputPath, "input", "../../build-local/perf-sweeps/summary.csv", "combined experiment summary CSV")
	flag.Float64Var(&opts.minCompletionRate, "min-completion-rate", 0.98, "minimum completion rate for recommendation")
	flag.IntVar(&opts.maxRowsPerSample, "max-rows-per-sample", 12, "maximum rows to print per sample limit when showing all combinations")
	flag.BoolVar(&opts.includeAllCombinations, "all", true, "print compact rows before recommendations")
	flag.Parse()

	if err := run(opts); err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}

func run(opts options) error {
	if opts.minCompletionRate < 0 || opts.minCompletionRate > 1 {
		return fmt.Errorf("-min-completion-rate must be between 0 and 1")
	}
	if opts.maxRowsPerSample <= 0 {
		return fmt.Errorf("-max-rows-per-sample must be positive")
	}

	rows, err := loadRows(opts.inputPath)
	if err != nil {
		return err
	}
	if len(rows) == 0 {
		return fmt.Errorf("no rows in %s", opts.inputPath)
	}

	sortRows(rows)
	groups := groupBySampleLimit(rows)

	if opts.includeAllCombinations {
		printCompactRows(groups, opts.maxRowsPerSample)
		fmt.Println()
	}
	printRecommendations(groups, opts.minCompletionRate)
	if hasColumn(groups, "tile_grid_cols") {
		if err := printRegionScaleSummary(opts.inputPath, groups); err != nil {
			return err
		}
	}
	return nil
}

func loadRows(path string) ([]experimentRow, error) {
	file, err := os.Open(path)
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
		return nil, nil
	}

	header := records[0]
	rows := make([]experimentRow, 0, len(records)-1)
	for _, record := range records[1:] {
		values := make(map[string]string, len(header))
		for index, name := range header {
			if index < len(record) {
				values[strings.TrimSpace(name)] = strings.TrimSpace(record[index])
			}
		}
		rows = append(rows, experimentRow{values: values})
	}
	return rows, nil
}

func sortRows(rows []experimentRow) {
	sort.Slice(rows, func(i, j int) bool {
		if rows[i].text("sample_limit") != rows[j].text("sample_limit") {
			return rows[i].intValue("sample_limit") < rows[j].intValue("sample_limit")
		}
		if rows[i].text("tile_grid_cols") != rows[j].text("tile_grid_cols") {
			return rows[i].intValue("tile_grid_cols") < rows[j].intValue("tile_grid_cols")
		}
		if rows[i].text("candidate_generation") != rows[j].text("candidate_generation") {
			return rows[i].text("candidate_generation") < rows[j].text("candidate_generation")
		}
		if rows[i].floatValue("radius") != rows[j].floatValue("radius") {
			return rows[i].floatValue("radius") < rows[j].floatValue("radius")
		}
		return kSortValue(rows[i].text("k")) < kSortValue(rows[j].text("k"))
	})
}

func groupBySampleLimit(rows []experimentRow) map[string][]experimentRow {
	groups := map[string][]experimentRow{}
	for _, row := range rows {
		groups[groupKey(row)] = append(groups[groupKey(row)], row)
	}
	return groups
}

func printCompactRows(groups map[string][]experimentRow, maxRows int) {
	limits := sortedKeys(groups)
	includeZoneFixed := hasColumn(groups, "zone_fixed_completed_revenue")
	for _, limit := range limits {
		fmt.Printf("%s compact rows\n", groupLabel(limit))
		header := "mode,radius,k,completion,candidate_edges,avg_pickup,replay_ms,hot_completion,cold_completion,hot_coverage,cold_coverage,opportunity_avg"
		if includeZoneFixed {
			header += ",zone_fixed_completed_revenue,zone_fixed_net_delta,zone_fixed_avg_factor"
		}
		fmt.Println(header)
		rows := groups[limit]
		for index, row := range rows {
			if index >= maxRows {
				fmt.Printf("... %d more rows\n", len(rows)-index)
				break
			}
			fields := []string{
				row.text("candidate_generation"),
				row.text("radius"),
				row.text("k"),
				row.text("completion_rate"),
				row.text("candidate_edges"),
				row.text("avg_pickup_cost"),
				row.text("replay_ms"),
				row.text("hot_dropoff_completion_rate"),
				row.text("cold_dropoff_completion_rate"),
				row.text("hot_dropoff_candidate_coverage_rate"),
				row.text("cold_dropoff_candidate_coverage_rate"),
				row.text("opportunity_adjustment_avg"),
			}
			if includeZoneFixed {
				fields = append(fields,
					row.text("zone_fixed_completed_revenue"),
					row.text("zone_fixed_net_delta"),
					row.text("zone_fixed_avg_factor"),
				)
			}
			fmt.Println(strings.Join(fields, ","))
		}
		fmt.Println()
	}
}

func printRecommendations(groups map[string][]experimentRow, minCompletionRate float64) {
	includeZoneFixed := hasColumn(groups, "zone_fixed_completed_revenue")
	includeTileGrid := hasColumn(groups, "tile_grid_cols")
	fmt.Printf("recommendations min_completion_rate=%.4f\n", minCompletionRate)
	header := "sample_limit,mode,radius,k,completion,candidate_edges,avg_pickup,replay_ms,hot_completion,cold_completion,hot_coverage,cold_coverage,opportunity_avg"
	if includeTileGrid {
		header = "sample_limit,tile_grid_cols,mode,radius,k,completion,candidate_edges,avg_pickup,replay_ms,hot_completion,cold_completion,hot_coverage,cold_coverage,opportunity_avg"
	}
	if includeZoneFixed {
		header += ",zone_fixed_completed_revenue,zone_fixed_net_delta,zone_fixed_avg_factor"
	}
	fmt.Println(header)
	for _, limit := range sortedKeys(groups) {
		best, ok := chooseRecommendation(groups[limit], minCompletionRate)
		if !ok {
			fields := []string{groupSampleLimit(limit)}
			if includeTileGrid {
				fields = append(fields, groupTileGridCols(limit))
			}
			fields = append(fields, "no row meets completion threshold")
			for len(fields) < strings.Count(header, ",")+1 {
				fields = append(fields, "")
			}
			fmt.Println(strings.Join(fields, ","))
			continue
		}
		fields := []string{
			groupSampleLimit(limit),
		}
		if includeTileGrid {
			fields = append(fields, groupTileGridCols(limit))
		}
		fields = append(fields,
			best.text("candidate_generation"),
			best.text("radius"),
			best.text("k"),
			best.text("completion_rate"),
			best.text("candidate_edges"),
			best.text("avg_pickup_cost"),
			best.text("replay_ms"),
			best.text("hot_dropoff_completion_rate"),
			best.text("cold_dropoff_completion_rate"),
			best.text("hot_dropoff_candidate_coverage_rate"),
			best.text("cold_dropoff_candidate_coverage_rate"),
			best.text("opportunity_adjustment_avg"),
		)
		if includeZoneFixed {
			fields = append(fields,
				best.text("zone_fixed_completed_revenue"),
				best.text("zone_fixed_net_delta"),
				best.text("zone_fixed_avg_factor"),
			)
		}
		fmt.Println(strings.Join(fields, ","))
	}
}

func chooseRecommendation(rows []experimentRow, minCompletionRate float64) (experimentRow, bool) {
	filtered := make([]experimentRow, 0, len(rows))
	for _, row := range rows {
		if row.floatValue("completion_rate") >= minCompletionRate {
			filtered = append(filtered, row)
		}
	}
	if len(filtered) == 0 {
		return experimentRow{}, false
	}

	sort.Slice(filtered, func(i, j int) bool {
		if filtered[i].floatValue("candidate_edges") != filtered[j].floatValue("candidate_edges") {
			return filtered[i].floatValue("candidate_edges") < filtered[j].floatValue("candidate_edges")
		}
		if filtered[i].floatValue("replay_ms") != filtered[j].floatValue("replay_ms") {
			return filtered[i].floatValue("replay_ms") < filtered[j].floatValue("replay_ms")
		}
		return filtered[i].floatValue("avg_pickup_cost") < filtered[j].floatValue("avg_pickup_cost")
	})
	return filtered[0], true
}

func sortedKeys(groups map[string][]experimentRow) []string {
	keys := make([]string, 0, len(groups))
	for key := range groups {
		keys = append(keys, key)
	}
	sort.Slice(keys, func(i, j int) bool {
		leftLimit, leftLimitErr := strconv.Atoi(groupSampleLimit(keys[i]))
		rightLimit, rightLimitErr := strconv.Atoi(groupSampleLimit(keys[j]))
		if leftLimitErr == nil && rightLimitErr == nil && leftLimit != rightLimit {
			return leftLimit < rightLimit
		}
		leftGrid, leftGridErr := strconv.Atoi(groupTileGridCols(keys[i]))
		rightGrid, rightGridErr := strconv.Atoi(groupTileGridCols(keys[j]))
		if leftGridErr == nil && rightGridErr == nil && leftGrid != rightGrid {
			return leftGrid < rightGrid
		}
		return keys[i] < keys[j]
	})
	return keys
}

func groupKey(row experimentRow) string {
	limit := row.text("sample_limit")
	grid := row.text("tile_grid_cols")
	if grid == "" {
		return limit
	}
	return limit + "|" + grid
}

func groupSampleLimit(key string) string {
	parts := strings.SplitN(key, "|", 2)
	return parts[0]
}

func groupTileGridCols(key string) string {
	parts := strings.SplitN(key, "|", 2)
	if len(parts) < 2 {
		return ""
	}
	return parts[1]
}

func groupLabel(key string) string {
	limit := groupSampleLimit(key)
	grid := groupTileGridCols(key)
	if grid == "" {
		return fmt.Sprintf("sample_limit=%s", limit)
	}
	return fmt.Sprintf("sample_limit=%s tile_grid_cols=%s", limit, grid)
}

type regionScaleStats struct {
	regionCount        int
	avgRegionTileCount float64
	maxRegionDiagKm    float64
	avgRegionDiagKm    float64
	maxRegionAreaKm2   float64
}

func printRegionScaleSummary(inputPath string, groups map[string][]experimentRow) error {
	baseDir := filepath.Dir(filepath.FromSlash(inputPath))
	printedHeader := false
	for _, key := range sortedKeys(groups) {
		limit := groupSampleLimit(key)
		grid := groupTileGridCols(key)
		if limit == "" || grid == "" {
			continue
		}
		path := filepath.Join(baseDir, "normalized", "grid_"+grid, "limit_"+limit, "region_stats.csv")
		stats, ok, err := loadRegionScaleStats(path)
		if err != nil {
			return err
		}
		if !ok {
			continue
		}
		if !printedHeader {
			fmt.Println()
			fmt.Println("region scale summary")
			fmt.Println("sample_limit,tile_grid_cols,region_count,avg_region_tile_count,max_region_diag_km,avg_region_diag_km,max_region_area_km2")
			printedHeader = true
		}
		fmt.Println(strings.Join([]string{
			limit,
			grid,
			strconv.Itoa(stats.regionCount),
			formatFloat(stats.avgRegionTileCount),
			formatFloat(stats.maxRegionDiagKm),
			formatFloat(stats.avgRegionDiagKm),
			formatFloat(stats.maxRegionAreaKm2),
		}, ","))
	}
	return nil
}

func loadRegionScaleStats(path string) (regionScaleStats, bool, error) {
	file, err := os.Open(path)
	if os.IsNotExist(err) {
		return regionScaleStats{}, false, nil
	}
	if err != nil {
		return regionScaleStats{}, false, err
	}
	defer file.Close()

	reader := csv.NewReader(file)
	records, err := reader.ReadAll()
	if err != nil {
		return regionScaleStats{}, false, fmt.Errorf("parse region stats %s: %w", path, err)
	}
	if len(records) < 2 {
		return regionScaleStats{}, true, nil
	}

	index := make(map[string]int, len(records[0]))
	for column, name := range records[0] {
		index[strings.TrimSpace(name)] = column
	}

	var stats regionScaleStats
	var tileCountTotal float64
	var diagTotal float64
	for rowNumber, record := range records[1:] {
		tileCount, err := parseRegionFloat(index, record, "tile_count")
		if err != nil {
			return regionScaleStats{}, false, fmt.Errorf("%s row %d: %w", path, rowNumber+2, err)
		}
		diagKm, err := parseRegionFloat(index, record, "approx_diagonal_km")
		if err != nil {
			return regionScaleStats{}, false, fmt.Errorf("%s row %d: %w", path, rowNumber+2, err)
		}
		areaKm2, err := parseRegionFloat(index, record, "approx_area_km2")
		if err != nil {
			return regionScaleStats{}, false, fmt.Errorf("%s row %d: %w", path, rowNumber+2, err)
		}
		stats.regionCount++
		tileCountTotal += tileCount
		diagTotal += diagKm
		if diagKm > stats.maxRegionDiagKm {
			stats.maxRegionDiagKm = diagKm
		}
		if areaKm2 > stats.maxRegionAreaKm2 {
			stats.maxRegionAreaKm2 = areaKm2
		}
	}

	if stats.regionCount > 0 {
		stats.avgRegionTileCount = tileCountTotal / float64(stats.regionCount)
		stats.avgRegionDiagKm = diagTotal / float64(stats.regionCount)
	}
	return stats, true, nil
}

func parseRegionFloat(index map[string]int, record []string, name string) (float64, error) {
	column, ok := index[name]
	if !ok || column < 0 || column >= len(record) {
		return 0, fmt.Errorf("missing column %s", name)
	}
	value, err := strconv.ParseFloat(strings.TrimSpace(record[column]), 64)
	if err != nil {
		return 0, fmt.Errorf("parse column %s=%q: %w", name, record[column], err)
	}
	return value, nil
}

func hasColumn(groups map[string][]experimentRow, name string) bool {
	for _, rows := range groups {
		for _, row := range rows {
			if _, ok := row.values[name]; ok {
				return true
			}
		}
	}
	return false
}

func (r experimentRow) text(name string) string {
	return r.values[name]
}

func (r experimentRow) floatValue(name string) float64 {
	value, err := strconv.ParseFloat(r.values[name], 64)
	if err != nil {
		return 0
	}
	return value
}

func (r experimentRow) intValue(name string) int {
	value, err := strconv.Atoi(r.values[name])
	if err != nil {
		return 0
	}
	return value
}

func kSortValue(value string) int {
	if value == "unlimited" {
		return 1_000_000_000
	}
	parsed, err := strconv.Atoi(value)
	if err != nil {
		return 0
	}
	return parsed
}

func formatFloat(value float64) string {
	return strconv.FormatFloat(value, 'f', 6, 64)
}
