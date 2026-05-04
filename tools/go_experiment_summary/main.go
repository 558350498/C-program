package main

import (
	"encoding/csv"
	"flag"
	"fmt"
	"os"
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
		limit := row.text("sample_limit")
		groups[limit] = append(groups[limit], row)
	}
	return groups
}

func printCompactRows(groups map[string][]experimentRow, maxRows int) {
	limits := sortedKeys(groups)
	for _, limit := range limits {
		fmt.Printf("sample_limit=%s compact rows\n", limit)
		fmt.Println("mode,radius,k,completion,candidate_edges,avg_pickup,replay_ms,hot_completion,cold_completion,hot_coverage,cold_coverage,opportunity_avg")
		rows := groups[limit]
		for index, row := range rows {
			if index >= maxRows {
				fmt.Printf("... %d more rows\n", len(rows)-index)
				break
			}
			fmt.Printf("%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n",
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
			)
		}
		fmt.Println()
	}
}

func printRecommendations(groups map[string][]experimentRow, minCompletionRate float64) {
	fmt.Printf("recommendations min_completion_rate=%.4f\n", minCompletionRate)
	fmt.Println("sample_limit,mode,radius,k,completion,candidate_edges,avg_pickup,replay_ms,hot_completion,cold_completion,hot_coverage,cold_coverage,opportunity_avg")
	for _, limit := range sortedKeys(groups) {
		best, ok := chooseRecommendation(groups[limit], minCompletionRate)
		if !ok {
			fmt.Printf("%s,no row meets completion threshold,,,,,,,,,,,\n", limit)
			continue
		}
		fmt.Printf("%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n",
			limit,
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
		left, leftErr := strconv.Atoi(keys[i])
		right, rightErr := strconv.Atoi(keys[j])
		if leftErr == nil && rightErr == nil {
			return left < right
		}
		return keys[i] < keys[j]
	})
	return keys
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
