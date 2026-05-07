#include "tile_grid_stats.h"

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

  const TileGridStats empty_stats =
      build_tile_grid_stats(std::vector<PassengerRequest>(),
                            std::vector<DriverSnapshot>());
  REQUIRE(empty_stats.max_pickup_count() == 0);
  REQUIRE(empty_stats.hotspot_score(10) == 0.0);
  REQUIRE(empty_stats.cold_score(10) == 1.0);
  REQUIRE(empty_stats.entries().empty());

  return 0;
}
