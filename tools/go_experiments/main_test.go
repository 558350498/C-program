package main

import (
	"math"
	"testing"
)

func TestPricingFactorLinearAndClamp(t *testing.T) {
	params := pricingParams{
		pickupHotWeight:    0.15,
		coldDropoffPenalty: 0.20,
		hotDropoffDiscount: 0.10,
		priceFloor:         0.8,
		priceCap:           1.2,
		mode:               "linear",
	}
	factor := pricingFactor(0.5, 0.25, 0.75, params)
	want := 1.0 + 0.15*0.5 + 0.20*0.75 - 0.10*0.25
	if math.Abs(factor-want) > 0.000001 {
		t.Fatalf("factor = %f, want %f", factor, want)
	}
	if got := pricingFactor(1, 0, 1, params); got != 1.2 {
		t.Fatalf("capped factor = %f, want 1.2", got)
	}
	params.priceFloor = 1.1
	params.hotDropoffDiscount = 1.0
	if got := pricingFactor(0, 1, 0, params); got != 1.1 {
		t.Fatalf("floored factor = %f, want 1.1", got)
	}
}

func TestSummarizePricingNetRevenue(t *testing.T) {
	requests := []requestRow{
		{pickupLon: -73.99, pickupLat: 40.75, dropoffLon: -73.98, dropoffLat: 40.76, pickupTile: 1, dropoffTile: 2},
	}
	table := hotspotSideTable{
		pickupHeatByTile: map[int]int{1: 2, 2: 1},
		maxPickupHeat:    2,
	}
	params := pricingParams{
		farePerKm:              2,
		pickupCostPerKm:        0.5,
		kmPerDegree:            100,
		secondsPerDistanceUnit: 1000,
		pickupHotWeight:        0.10,
		coldDropoffPenalty:     0.20,
		hotDropoffDiscount:     0.10,
		priceFloor:             0.8,
		priceCap:               1.8,
		mode:                   "linear",
	}

	summary := summarizePricing(requests, table, params, 0.5, 200, 3)
	if summary.mode != "linear" {
		t.Fatalf("mode = %q, want linear", summary.mode)
	}
	if summary.avgPriceFactor <= 1 {
		t.Fatalf("avg factor = %f, want > 1", summary.avgPriceFactor)
	}
	wantPickupCost := 200.0 / 1000.0 * 100.0 * 0.5
	if math.Abs((summary.completedRevenue-wantPickupCost)-summary.netRevenue) > 0.000001 {
		t.Fatalf("net revenue mismatch: %+v", summary)
	}
	if math.Abs((summary.netRevenue-3)-summary.netDelta) > 0.000001 {
		t.Fatalf("net delta mismatch: %+v", summary)
	}
}
