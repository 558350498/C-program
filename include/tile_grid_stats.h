#pragma once

#include "dispatch_batch.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

struct TileGridStatsEntry {
  TileId tile_id;
  std::size_t pickup_count;
  std::size_t dropoff_count;
  std::size_t available_driver_count;
  double hotspot_score;
  double cold_score;

  TileGridStatsEntry();
  explicit TileGridStatsEntry(TileId tile_id_value);
};

struct RequestTileFeatures {
  double pickup_hotspot_score;
  double dropoff_hotspot_score;
  double cold_dropoff_score;

  RequestTileFeatures();
  RequestTileFeatures(double pickup_hotspot_score_value,
                      double dropoff_hotspot_score_value,
                      double cold_dropoff_score_value);
};

class TileGridStats {
public:
  TileGridStats();

  void add_pickup(TileId tile_id);
  void add_dropoff(TileId tile_id);
  void add_available_driver(TileId tile_id);
  void finalize_scores();

  const TileGridStatsEntry *find(TileId tile_id) const;
  std::size_t pickup_count(TileId tile_id) const;
  std::size_t dropoff_count(TileId tile_id) const;
  std::size_t available_driver_count(TileId tile_id) const;
  double hotspot_score(TileId tile_id) const;
  double cold_score(TileId tile_id) const;
  RequestTileFeatures request_tile_features(
      const PassengerRequest &request) const;
  std::vector<TileGridStatsEntry> entries() const;
  std::size_t max_pickup_count() const;

private:
  std::unordered_map<TileId, TileGridStatsEntry> entries_by_tile_;
  std::size_t max_pickup_count_;

  TileGridStatsEntry &entry_for(TileId tile_id);
};

TileGridStats
build_tile_grid_stats(const std::vector<PassengerRequest> &requests,
                      const std::vector<DriverSnapshot> &drivers);

std::string format_tile_grid_stats_csv(const TileGridStats &stats);
