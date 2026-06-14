#include "tile_grid_stats.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <queue>
#include <sstream>
#include <unordered_set>

TileGridStatsEntry::TileGridStatsEntry()
    : tile_id(invalid_tile_id), pickup_count(0), dropoff_count(0),
      available_driver_count(0), hotspot_score(0.0), cold_score(1.0) {}

TileGridStatsEntry::TileGridStatsEntry(TileId tile_id_value)
    : tile_id(tile_id_value), pickup_count(0), dropoff_count(0),
      available_driver_count(0), hotspot_score(0.0), cold_score(1.0) {}

RequestTileFeatures::RequestTileFeatures()
    : pickup_hotspot_score(0.0), dropoff_hotspot_score(0.0),
      cold_dropoff_score(1.0) {}

RequestTileFeatures::RequestTileFeatures(
    double pickup_hotspot_score_value, double dropoff_hotspot_score_value,
    double cold_dropoff_score_value)
    : pickup_hotspot_score(pickup_hotspot_score_value),
      dropoff_hotspot_score(dropoff_hotspot_score_value),
      cold_dropoff_score(cold_dropoff_score_value) {}

CellSmoothingOptions::CellSmoothingOptions()
    : neighbor_rings(0), neighbor_weight(0.0), parent_grid_cols(0),
      parent_weight(0.0) {}

TileGridStats::TileGridStats()
    : entries_by_tile_(), max_pickup_count_(0) {}

void TileGridStats::add_pickup(TileId tile_id) {
  if (tile_id == invalid_tile_id) {
    return;
  }
  TileGridStatsEntry &entry = entry_for(tile_id);
  ++entry.pickup_count;
  max_pickup_count_ = std::max(max_pickup_count_, entry.pickup_count);
}

void TileGridStats::add_dropoff(TileId tile_id) {
  if (tile_id == invalid_tile_id) {
    return;
  }
  ++entry_for(tile_id).dropoff_count;
}

void TileGridStats::add_available_driver(TileId tile_id) {
  if (tile_id == invalid_tile_id) {
    return;
  }
  ++entry_for(tile_id).available_driver_count;
}

void TileGridStats::finalize_scores() {
  for (auto &[tile_id, entry] : entries_by_tile_) {
    (void)tile_id;
    if (max_pickup_count_ == 0) {
      entry.hotspot_score = 0.0;
    } else {
      entry.hotspot_score =
          static_cast<double>(entry.pickup_count) /
          static_cast<double>(max_pickup_count_);
    }
    entry.cold_score = 1.0 - entry.hotspot_score;
  }
}

const TileGridStatsEntry *TileGridStats::find(TileId tile_id) const {
  const auto entry_it = entries_by_tile_.find(tile_id);
  if (entry_it == entries_by_tile_.end()) {
    return nullptr;
  }
  return &entry_it->second;
}

std::size_t TileGridStats::pickup_count(TileId tile_id) const {
  const TileGridStatsEntry *entry = find(tile_id);
  return entry ? entry->pickup_count : 0;
}

std::size_t TileGridStats::dropoff_count(TileId tile_id) const {
  const TileGridStatsEntry *entry = find(tile_id);
  return entry ? entry->dropoff_count : 0;
}

std::size_t TileGridStats::available_driver_count(TileId tile_id) const {
  const TileGridStatsEntry *entry = find(tile_id);
  return entry ? entry->available_driver_count : 0;
}

double TileGridStats::hotspot_score(TileId tile_id) const {
  const TileGridStatsEntry *entry = find(tile_id);
  return entry ? entry->hotspot_score : 0.0;
}

double TileGridStats::cold_score(TileId tile_id) const {
  const TileGridStatsEntry *entry = find(tile_id);
  return entry ? entry->cold_score : 1.0;
}

RequestTileFeatures
TileGridStats::request_tile_features(const PassengerRequest &request) const {
  const double pickup_hotspot = hotspot_score(request.pickup_tile);
  const double dropoff_hotspot = hotspot_score(request.dropoff_tile);
  return RequestTileFeatures(pickup_hotspot, dropoff_hotspot,
                             1.0 - dropoff_hotspot);
}

std::vector<TileGridStatsEntry> TileGridStats::entries() const {
  std::vector<TileGridStatsEntry> result;
  result.reserve(entries_by_tile_.size());
  for (const auto &[tile_id, entry] : entries_by_tile_) {
    (void)tile_id;
    result.push_back(entry);
  }
  std::sort(result.begin(), result.end(),
            [](const TileGridStatsEntry &lhs,
               const TileGridStatsEntry &rhs) {
              return lhs.tile_id < rhs.tile_id;
            });
  return result;
}

std::size_t TileGridStats::max_pickup_count() const {
  return max_pickup_count_;
}

TileGridStatsEntry &TileGridStats::entry_for(TileId tile_id) {
  const auto [entry_it, inserted] =
      entries_by_tile_.emplace(tile_id, TileGridStatsEntry(tile_id));
  (void)inserted;
  return entry_it->second;
}

TileGridStats
build_tile_grid_stats(const std::vector<PassengerRequest> &requests,
                      const std::vector<DriverSnapshot> &drivers) {
  TileGridStats stats;
  for (const auto &request : requests) {
    stats.add_pickup(request.pickup_tile);
    stats.add_dropoff(request.dropoff_tile);
  }
  for (const auto &driver : drivers) {
    if (driver.status == TaxiStatus::free) {
      stats.add_available_driver(driver.current_tile);
    }
  }
  stats.finalize_scores();
  return stats;
}

TileGridStats
build_cell_grid_stats(const std::vector<PassengerRequest> &requests,
                      const std::vector<DriverSnapshot> &drivers,
                      const CellIndex &cell_index) {
  TileGridStats stats;
  for (const auto &request : requests) {
    stats.add_pickup(cell_index.encode(request.pickup_location.coords[0],
                                       request.pickup_location.coords[1]));
    stats.add_dropoff(cell_index.encode(request.dropoff_location.coords[0],
                                        request.dropoff_location.coords[1]));
  }
  for (const auto &driver : drivers) {
    if (driver.status == TaxiStatus::free) {
      stats.add_available_driver(cell_index.encode(driver.location.coords[0],
                                                   driver.location.coords[1]));
    }
  }
  stats.finalize_scores();
  return stats;
}

void encode_replay_tiles_with_cell_index(
    std::vector<PassengerRequest> &requests,
    std::vector<DriverSnapshot> &drivers, const CellIndex &cell_index) {
  for (auto &request : requests) {
    request.pickup_tile =
        cell_index.encode(request.pickup_location.coords[0],
                          request.pickup_location.coords[1]);
    request.dropoff_tile =
        cell_index.encode(request.dropoff_location.coords[0],
                          request.dropoff_location.coords[1]);
  }
  for (auto &driver : drivers) {
    driver.current_tile =
        cell_index.encode(driver.location.coords[0], driver.location.coords[1]);
  }
}

std::unordered_map<TileId, double> build_smoothed_hotspot_scores(
    const TileGridStats &stats, const CellIndex &cell_index,
    const CellSmoothingOptions &options) {
  std::unordered_map<TileId, double> result;
  const auto entries = stats.entries();
  result.reserve(entries.size());

  std::unordered_map<TileId, double> parent_sum;
  std::unordered_map<TileId, std::size_t> parent_count;
  if (options.parent_grid_cols > 0 && options.parent_weight > 0.0) {
    for (const auto &entry : entries) {
      const TileId parent_id =
          cell_index.parent(entry.tile_id, options.parent_grid_cols);
      if (parent_id == invalid_tile_id) {
        continue;
      }
      parent_sum[parent_id] += entry.hotspot_score;
      ++parent_count[parent_id];
    }
  }

  for (const auto &entry : entries) {
    double weighted_score = entry.hotspot_score;
    double total_weight = 1.0;

    if (options.neighbor_rings > 0 && options.neighbor_weight > 0.0) {
      std::queue<std::pair<TileId, int>> frontier;
      std::unordered_set<TileId> visited;
      visited.insert(entry.tile_id);
      frontier.emplace(entry.tile_id, 0);

      while (!frontier.empty()) {
        const auto [cell_id, depth] = frontier.front();
        frontier.pop();
        if (depth >= options.neighbor_rings) {
          continue;
        }
        for (const TileId neighbor : cell_index.neighbors(cell_id)) {
          if (neighbor == invalid_tile_id ||
              visited.find(neighbor) != visited.end()) {
            continue;
          }
          visited.insert(neighbor);
          const int neighbor_depth = depth + 1;
          const double weight =
              std::pow(options.neighbor_weight, neighbor_depth);
          weighted_score += stats.hotspot_score(neighbor) * weight;
          total_weight += weight;
          frontier.emplace(neighbor, neighbor_depth);
        }
      }
    }

    if (options.parent_grid_cols > 0 && options.parent_weight > 0.0) {
      const TileId parent_id =
          cell_index.parent(entry.tile_id, options.parent_grid_cols);
      const auto sum_it = parent_sum.find(parent_id);
      const auto count_it = parent_count.find(parent_id);
      if (parent_id != invalid_tile_id && sum_it != parent_sum.end() &&
          count_it != parent_count.end() && count_it->second > 0) {
        const double parent_score =
            sum_it->second / static_cast<double>(count_it->second);
        weighted_score += parent_score * options.parent_weight;
        total_weight += options.parent_weight;
      }
    }

    result[entry.tile_id] =
        std::clamp(weighted_score / total_weight, 0.0, 1.0);
  }

  return result;
}

std::string format_tile_grid_stats_csv(const TileGridStats &stats) {
  std::ostringstream stream;
  stream << "tile_id,pickup_count,dropoff_count,available_driver_count,"
            "hotspot_score,cold_score\n";
  stream << std::fixed << std::setprecision(6);
  for (const auto &entry : stats.entries()) {
    stream << entry.tile_id << ',' << entry.pickup_count << ','
           << entry.dropoff_count << ',' << entry.available_driver_count << ','
           << entry.hotspot_score << ',' << entry.cold_score << '\n';
  }
  return stream.str();
}
