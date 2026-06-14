#include "tile_grid_stats.h"

#include <cmath>

#define REQUIRE(condition)                                                      \
  do {                                                                         \
    if (!(condition)) {                                                        \
      return 1;                                                                \
    }                                                                          \
  } while (false)

int main() {
  std::vector<PassengerRequest> requests;
  requests.emplace_back(101, 1001, 0, Point(0.0, 0.0, 101),
                        Point(1.0, 1.0, 101), 10, 20);
  requests.emplace_back(102, 1002, 0, Point(0.0, 0.0, 102),
                        Point(1.0, 1.0, 102), 10, 30);
  requests.emplace_back(103, 1003, 0, Point(0.0, 0.0, 103),
                        Point(1.0, 1.0, 103), 40, 20);
  requests.emplace_back(104, 1004, 0, Point(0.0, 0.0, 104),
                        Point(1.0, 1.0, 104), invalid_tile_id,
                        invalid_tile_id);

  std::vector<DriverSnapshot> drivers;
  drivers.emplace_back(1, Point(0.0, 0.0, 1), 10, TaxiStatus::free, 0);
  drivers.emplace_back(2, Point(0.0, 0.0, 2), 10, TaxiStatus::occupy, 0);
  drivers.emplace_back(3, Point(0.0, 0.0, 3), 20, TaxiStatus::free, 0);
  drivers.emplace_back(4, Point(0.0, 0.0, 4), invalid_tile_id,
                       TaxiStatus::free, 0);

  const TileGridStats stats = build_tile_grid_stats(requests, drivers);
  REQUIRE(stats.max_pickup_count() == 2);
  REQUIRE(stats.pickup_count(10) == 2);
  REQUIRE(stats.pickup_count(40) == 1);
  REQUIRE(stats.pickup_count(invalid_tile_id) == 0);
  REQUIRE(stats.dropoff_count(20) == 2);
  REQUIRE(stats.dropoff_count(30) == 1);
  REQUIRE(stats.dropoff_count(invalid_tile_id) == 0);
  REQUIRE(stats.available_driver_count(10) == 1);
  REQUIRE(stats.available_driver_count(20) == 1);
  REQUIRE(stats.available_driver_count(invalid_tile_id) == 0);

  REQUIRE(stats.hotspot_score(10) == 1.0);
  REQUIRE(stats.hotspot_score(40) == 0.5);
  REQUIRE(stats.hotspot_score(999) == 0.0);
  REQUIRE(stats.cold_score(10) == 0.0);
  REQUIRE(stats.cold_score(40) == 0.5);
  REQUIRE(stats.cold_score(999) == 1.0);

  const RequestTileFeatures features =
      stats.request_tile_features(requests[0]);
  REQUIRE(features.pickup_hotspot_score == 1.0);
  REQUIRE(features.dropoff_hotspot_score == 0.0);
  REQUIRE(features.cold_dropoff_score == 1.0);

  const auto entries = stats.entries();
  REQUIRE(entries.size() == 4);
  REQUIRE(entries[0].tile_id == 10);
  REQUIRE(entries[1].tile_id == 20);
  REQUIRE(entries[2].tile_id == 30);
  REQUIRE(entries[3].tile_id == 40);

  const std::string csv = format_tile_grid_stats_csv(stats);
  REQUIRE(csv.find("tile_id,pickup_count,dropoff_count,"
                   "available_driver_count,hotspot_score,cold_score") == 0);
  REQUIRE(csv.find("10,2,0,1,1.000000,0.000000") != std::string::npos);
  REQUIRE(csv.find("20,0,2,1,0.000000,1.000000") != std::string::npos);

  SimpleTileCellIndex cell_index(10, 0.0, 10.0, 0.0, 10.0);
  std::vector<PassengerRequest> cell_requests;
  cell_requests.emplace_back(201, 2001, 0, Point(1.0, 1.0, 201),
                             Point(9.0, 9.0, 201), 999, 999);
  cell_requests.emplace_back(202, 2002, 0, Point(1.2, 1.1, 202),
                             Point(8.0, 8.0, 202), 999, 999);
  std::vector<DriverSnapshot> cell_drivers;
  cell_drivers.emplace_back(7, Point(1.5, 1.5, 7), 999, TaxiStatus::free, 0);
  const TileId pickup_cell = cell_index.encode(1.0, 1.0);
  const TileId dropoff_cell = cell_index.encode(9.0, 9.0);
  const TileGridStats cell_stats =
      build_cell_grid_stats(cell_requests, cell_drivers, cell_index);
  REQUIRE(pickup_cell != 999);
  REQUIRE(cell_stats.pickup_count(pickup_cell) == 2);
  REQUIRE(cell_stats.dropoff_count(dropoff_cell) == 1);
  REQUIRE(cell_stats.available_driver_count(pickup_cell) == 1);
  REQUIRE(cell_stats.hotspot_score(pickup_cell) == 1.0);

  encode_replay_tiles_with_cell_index(cell_requests, cell_drivers,
                                      cell_index);
  REQUIRE(cell_requests[0].pickup_tile == pickup_cell);
  REQUIRE(cell_requests[0].dropoff_tile == dropoff_cell);
  REQUIRE(cell_drivers[0].current_tile == pickup_cell);

  TileGridStats smoothing_stats;
  smoothing_stats.add_pickup(4);
  smoothing_stats.add_pickup(4);
  smoothing_stats.add_pickup(5);
  smoothing_stats.finalize_scores();
  SimpleTileCellIndex smoothing_index(3, 0.0, 3.0, 0.0, 3.0);

  CellSmoothingOptions neighbor_options;
  neighbor_options.neighbor_rings = 1;
  neighbor_options.neighbor_weight = 0.5;
  const auto neighbor_scores =
      build_smoothed_hotspot_scores(smoothing_stats, smoothing_index,
                                    neighbor_options);
  REQUIRE(neighbor_scores.find(4) != neighbor_scores.end());
  REQUIRE(std::abs(neighbor_scores.at(4) - (1.25 / 3.0)) < 0.000001);

  CellSmoothingOptions parent_options;
  parent_options.parent_grid_cols = 1;
  parent_options.parent_weight = 1.0;
  const auto parent_scores =
      build_smoothed_hotspot_scores(smoothing_stats, smoothing_index,
                                    parent_options);
  REQUIRE(parent_scores.find(4) != parent_scores.end());
  REQUIRE(std::abs(parent_scores.at(4) - 0.875) < 0.000001);

  const TileGridStats empty_stats =
      build_tile_grid_stats(std::vector<PassengerRequest>(),
                            std::vector<DriverSnapshot>());
  REQUIRE(empty_stats.max_pickup_count() == 0);
  REQUIRE(empty_stats.hotspot_score(10) == 0.0);
  REQUIRE(empty_stats.cold_score(10) == 1.0);
  REQUIRE(empty_stats.entries().empty());

  return 0;
}
