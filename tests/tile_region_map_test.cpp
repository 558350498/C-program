#include "tile_region_map.h"

#include <string>
#include <tuple>
#include <vector>

#define REQUIRE(condition)                                                      \
  do {                                                                         \
    if (!(condition)) {                                                        \
      return 1;                                                                \
    }                                                                          \
  } while (false)

void add_counts(TileGridStats &stats, TileId tile_id, std::size_t pickups,
                std::size_t dropoffs, std::size_t drivers) {
  for (std::size_t index = 0; index < pickups; ++index) {
    stats.add_pickup(tile_id);
  }
  for (std::size_t index = 0; index < dropoffs; ++index) {
    stats.add_dropoff(tile_id);
  }
  for (std::size_t index = 0; index < drivers; ++index) {
    stats.add_available_driver(tile_id);
  }
}

TileGridStats make_stats(
    const std::vector<std::tuple<TileId, std::size_t, std::size_t,
                                 std::size_t>> &rows) {
  TileGridStats stats;
  for (const auto &[tile_id, pickups, dropoffs, drivers] : rows) {
    add_counts(stats, tile_id, pickups, dropoffs, drivers);
  }
  stats.finalize_scores();
  return stats;
}

int main() {
  {
    const TileGridStats stats =
        make_stats({{0, 2, 1, 1}, {1, 2, 1, 1}, {101, 2, 1, 1}});
    const TileRegionMap map = build_tile_region_map(stats);
    REQUIRE(map.region_for_tile(0) == map.region_for_tile(1));
    REQUIRE(map.region_for_tile(1) == map.region_for_tile(101));
    REQUIRE(map.tile_entries().size() == 3);
    REQUIRE(map.region_entries().size() == 1);
    REQUIRE(map.region_entries()[0].tile_count == 3);
    REQUIRE(map.region_entries()[0].pickup_count == 6);
    REQUIRE(map.region_entries()[0].approx_width_km > 0.0);
    REQUIRE(map.region_entries()[0].approx_height_km > 0.0);
    REQUIRE(map.region_entries()[0].approx_diagonal_km > 0.0);
    REQUIRE(map.region_entries()[0].approx_area_km2 > 0.0);
  }

  {
    const TileGridStats stats = make_stats({{0, 2, 1, 1}, {101, 2, 1, 1}});
    const TileRegionMap map = build_tile_region_map(stats);
    REQUIRE(map.region_for_tile(0) != map.region_for_tile(101));
    REQUIRE(map.region_entries().size() == 2);
  }

  {
    const TileGridStats stats = make_stats({{0, 10, 1, 1}, {1, 0, 1, 1}});
    const TileRegionMap map = build_tile_region_map(stats);
    REQUIRE(map.region_for_tile(0) != map.region_for_tile(1));
  }

  {
    const TileGridStats stats =
        make_stats({{0, 2, 1, 1}, {1, 2, 1, 1}, {2, 2, 1, 1}});
    TileRegionMapOptions options;
    options.max_tiles = 2;
    const TileRegionMap map = build_tile_region_map(stats, options);
    REQUIRE(map.region_for_tile(0) == map.region_for_tile(1));
    REQUIRE(map.region_for_tile(0) != map.region_for_tile(2));
  }

  {
    const TileGridStats stats =
        make_stats({{0, 2, 1, 1}, {1, 2, 1, 1}, {2, 2, 1, 1}});
    TileRegionMapOptions options;
    options.max_bbox_width = 2;
    options.max_aspect_ratio = 10.0;
    const TileRegionMap map = build_tile_region_map(stats, options);
    REQUIRE(map.region_for_tile(0) == map.region_for_tile(1));
    REQUIRE(map.region_for_tile(0) != map.region_for_tile(2));
  }

  {
    const TileGridStats stats = make_stats({{0, 2, 1, 1}, {1, 2, 1, 1}});
    TileRegionMapOptions options;
    options.max_aspect_ratio = 1.5;
    const TileRegionMap map = build_tile_region_map(stats, options);
    REQUIRE(map.region_for_tile(0) != map.region_for_tile(1));
  }

  {
    const TileGridStats stats =
        make_stats({{0, 2, 1, 1}, {1, 2, 1, 1}, {10000, 2, 1, 1}});
    const TileRegionMap map = build_tile_region_map(stats);
    REQUIRE(map.region_for_tile(10000) == -1);
    REQUIRE(map.tile_entries().size() == 2);
    REQUIRE(map.tile_entries()[0].tile_id == 0);
    REQUIRE(map.tile_entries()[1].tile_id == 1);
  }

  {
    const TileGridStats stats = make_stats({{0, 2, 1, 1}, {1, 2, 1, 1}});
    const TileRegionMap map = build_tile_region_map(stats);
    const std::string map_csv = format_tile_region_map_csv(map);
    REQUIRE(map_csv.find("tile_id,region_id\n") == 0);
    REQUIRE(map_csv.find("0,0\n") != std::string::npos);
    REQUIRE(map_csv.find("1,0\n") != std::string::npos);

    const std::string stats_csv = format_tile_region_stats_csv(map);
    REQUIRE(stats_csv.find("region_id,tile_count,pickup_count,dropoff_count,"
                           "available_driver_count,avg_hotspot_score,"
                           "avg_cold_score,min_tile_id,min_row,max_row,"
                           "min_col,max_col,approx_width_km,"
                           "approx_height_km,approx_diagonal_km,"
                           "approx_area_km2") == 0);
    REQUIRE(stats_csv.find("0,2,4,2,2,1.000000,0.000000,0,0,0,0,1") !=
            std::string::npos);
  }

  return 0;
}
