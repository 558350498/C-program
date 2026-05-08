#pragma once

#include "tile_grid_stats.h"

#include <cstddef>
#include <string>
#include <vector>

using RegionId = int;

struct TileRegionMapOptions {
  int grid_cols;
  double similarity_threshold;
  std::size_t max_tiles;
  int max_bbox_width;
  int max_bbox_height;
  double max_aspect_ratio;
  double min_lon;
  double max_lon;
  double min_lat;
  double max_lat;

  TileRegionMapOptions();
};

struct TileRegionMapEntry {
  TileId tile_id;
  RegionId region_id;
  int row;
  int col;

  TileRegionMapEntry();
  TileRegionMapEntry(TileId tile_id_value, RegionId region_id_value,
                     int row_value, int col_value);
};

struct TileRegionStatsEntry {
  RegionId region_id;
  std::size_t tile_count;
  std::size_t pickup_count;
  std::size_t dropoff_count;
  std::size_t available_driver_count;
  double avg_hotspot_score;
  double avg_cold_score;
  TileId min_tile_id;
  int min_row;
  int max_row;
  int min_col;
  int max_col;
  double approx_width_km;
  double approx_height_km;
  double approx_diagonal_km;
  double approx_area_km2;

  TileRegionStatsEntry();
};

class TileRegionMap {
public:
  TileRegionMap();
  TileRegionMap(std::vector<TileRegionMapEntry> tile_entries,
                std::vector<TileRegionStatsEntry> region_entries);

  RegionId region_for_tile(TileId tile_id) const;
  const std::vector<TileRegionMapEntry> &tile_entries() const;
  const std::vector<TileRegionStatsEntry> &region_entries() const;

private:
  std::vector<TileRegionMapEntry> tile_entries_;
  std::vector<TileRegionStatsEntry> region_entries_;
};

bool is_valid_region_tile(TileId tile_id,
                          const TileRegionMapOptions &options);
int tile_region_row(TileId tile_id, const TileRegionMapOptions &options);
int tile_region_col(TileId tile_id, const TileRegionMapOptions &options);

TileRegionMap
build_tile_region_map(const TileGridStats &tile_stats,
                      TileRegionMapOptions options = TileRegionMapOptions());

std::string format_tile_region_map_csv(const TileRegionMap &region_map);
std::string format_tile_region_stats_csv(const TileRegionMap &region_map);
