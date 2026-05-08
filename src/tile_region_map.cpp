#include "tile_region_map.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace {

constexpr double pickup_heat_weight = 0.5;
constexpr double dropoff_density_weight = 0.3;
constexpr double free_driver_density_weight = 0.2;
constexpr double kilometers_per_lat_degree = 111.32;
constexpr RegionId missing_region_id = -1;

struct TileFeature {
  TileGridStatsEntry entry;
  int row;
  int col;
  double pickup_heat;
  double dropoff_density;
  double free_driver_density;
};

struct CandidateRegionEdge {
  std::size_t lhs;
  std::size_t rhs;
  double similarity;
};

struct RegionAggregate {
  std::size_t tile_count;
  std::size_t pickup_count;
  std::size_t dropoff_count;
  std::size_t available_driver_count;
  double hotspot_score_total;
  double cold_score_total;
  TileId min_tile_id;
  int min_row;
  int max_row;
  int min_col;
  int max_col;
};

double normalized_density(std::size_t value, std::size_t max_value) {
  if (max_value == 0) {
    return 0.0;
  }
  return static_cast<double>(value) / static_cast<double>(max_value);
}

double behavior_similarity(const TileFeature &lhs, const TileFeature &rhs) {
  const double diff =
      pickup_heat_weight * std::abs(lhs.pickup_heat - rhs.pickup_heat) +
      dropoff_density_weight *
          std::abs(lhs.dropoff_density - rhs.dropoff_density) +
      free_driver_density_weight *
          std::abs(lhs.free_driver_density - rhs.free_driver_density);
  return std::max(0.0, std::min(1.0, 1.0 - diff));
}

RegionAggregate aggregate_for_tile(const TileFeature &tile) {
  RegionAggregate aggregate;
  aggregate.tile_count = 1;
  aggregate.pickup_count = tile.entry.pickup_count;
  aggregate.dropoff_count = tile.entry.dropoff_count;
  aggregate.available_driver_count = tile.entry.available_driver_count;
  aggregate.hotspot_score_total = tile.entry.hotspot_score;
  aggregate.cold_score_total = tile.entry.cold_score;
  aggregate.min_tile_id = tile.entry.tile_id;
  aggregate.min_row = tile.row;
  aggregate.max_row = tile.row;
  aggregate.min_col = tile.col;
  aggregate.max_col = tile.col;
  return aggregate;
}

RegionAggregate merge_aggregates(const RegionAggregate &lhs,
                                 const RegionAggregate &rhs) {
  RegionAggregate merged;
  merged.tile_count = lhs.tile_count + rhs.tile_count;
  merged.pickup_count = lhs.pickup_count + rhs.pickup_count;
  merged.dropoff_count = lhs.dropoff_count + rhs.dropoff_count;
  merged.available_driver_count =
      lhs.available_driver_count + rhs.available_driver_count;
  merged.hotspot_score_total =
      lhs.hotspot_score_total + rhs.hotspot_score_total;
  merged.cold_score_total = lhs.cold_score_total + rhs.cold_score_total;
  merged.min_tile_id = std::min(lhs.min_tile_id, rhs.min_tile_id);
  merged.min_row = std::min(lhs.min_row, rhs.min_row);
  merged.max_row = std::max(lhs.max_row, rhs.max_row);
  merged.min_col = std::min(lhs.min_col, rhs.min_col);
  merged.max_col = std::max(lhs.max_col, rhs.max_col);
  return merged;
}

bool aggregate_fits(const RegionAggregate &aggregate,
                    const TileRegionMapOptions &options) {
  if (aggregate.tile_count > options.max_tiles) {
    return false;
  }
  const int width = aggregate.max_col - aggregate.min_col + 1;
  const int height = aggregate.max_row - aggregate.min_row + 1;
  if (width > options.max_bbox_width || height > options.max_bbox_height) {
    return false;
  }
  const int shorter = std::max(1, std::min(width, height));
  const int longer = std::max(width, height);
  const double aspect_ratio =
      static_cast<double>(longer) / static_cast<double>(shorter);
  return aspect_ratio <= options.max_aspect_ratio;
}

double degrees_to_radians(double value) { return value * 3.14159265358979323846 / 180.0; }

double approx_region_center_lat(const RegionAggregate &aggregate,
                                const TileRegionMapOptions &options) {
  const double lat_step =
      (options.max_lat - options.min_lat) / static_cast<double>(options.grid_cols);
  const double center_row =
      (static_cast<double>(aggregate.min_row) +
       static_cast<double>(aggregate.max_row) + 1.0) /
      2.0;
  return options.min_lat + center_row * lat_step;
}

double approx_region_width_km(const RegionAggregate &aggregate,
                              const TileRegionMapOptions &options) {
  const int cols = aggregate.max_col - aggregate.min_col + 1;
  const double lon_step =
      (options.max_lon - options.min_lon) / static_cast<double>(options.grid_cols);
  const double center_lat = approx_region_center_lat(aggregate, options);
  const double km_per_lon_degree =
      kilometers_per_lat_degree * std::cos(degrees_to_radians(center_lat));
  return static_cast<double>(cols) * lon_step * km_per_lon_degree;
}

double approx_region_height_km(const RegionAggregate &aggregate,
                               const TileRegionMapOptions &options) {
  const int rows = aggregate.max_row - aggregate.min_row + 1;
  const double lat_step =
      (options.max_lat - options.min_lat) / static_cast<double>(options.grid_cols);
  return static_cast<double>(rows) * lat_step * kilometers_per_lat_degree;
}

std::size_t find_root(std::vector<std::size_t> &parents, std::size_t node) {
  if (parents[node] != node) {
    parents[node] = find_root(parents, parents[node]);
  }
  return parents[node];
}

std::size_t find_root_const(const std::vector<std::size_t> &parents,
                            std::size_t node) {
  while (parents[node] != node) {
    node = parents[node];
  }
  return node;
}

void union_roots(std::vector<std::size_t> &parents,
                 std::vector<RegionAggregate> &aggregates,
                 std::size_t lhs_root, std::size_t rhs_root,
                 const RegionAggregate &merged) {
  if (aggregates[rhs_root].min_tile_id < aggregates[lhs_root].min_tile_id) {
    std::swap(lhs_root, rhs_root);
  }
  parents[rhs_root] = lhs_root;
  aggregates[lhs_root] = merged;
}

TileRegionStatsEntry to_stats_entry(RegionId region_id,
                                    const RegionAggregate &aggregate,
                                    const TileRegionMapOptions &options) {
  TileRegionStatsEntry entry;
  entry.region_id = region_id;
  entry.tile_count = aggregate.tile_count;
  entry.pickup_count = aggregate.pickup_count;
  entry.dropoff_count = aggregate.dropoff_count;
  entry.available_driver_count = aggregate.available_driver_count;
  entry.avg_hotspot_score =
      aggregate.tile_count == 0
          ? 0.0
          : aggregate.hotspot_score_total /
                static_cast<double>(aggregate.tile_count);
  entry.avg_cold_score =
      aggregate.tile_count == 0
          ? 1.0
          : aggregate.cold_score_total / static_cast<double>(aggregate.tile_count);
  entry.min_tile_id = aggregate.min_tile_id;
  entry.min_row = aggregate.min_row;
  entry.max_row = aggregate.max_row;
  entry.min_col = aggregate.min_col;
  entry.max_col = aggregate.max_col;
  entry.approx_width_km = approx_region_width_km(aggregate, options);
  entry.approx_height_km = approx_region_height_km(aggregate, options);
  entry.approx_diagonal_km =
      std::sqrt(entry.approx_width_km * entry.approx_width_km +
                entry.approx_height_km * entry.approx_height_km);
  entry.approx_area_km2 = entry.approx_width_km * entry.approx_height_km;
  return entry;
}

} // namespace

TileRegionMapOptions::TileRegionMapOptions()
    : grid_cols(100), similarity_threshold(0.75), max_tiles(25),
      max_bbox_width(5), max_bbox_height(5), max_aspect_ratio(3.0),
      min_lon(-75.0), max_lon(-72.0), min_lat(40.0), max_lat(42.0) {}

TileRegionMapEntry::TileRegionMapEntry()
    : tile_id(invalid_tile_id), region_id(missing_region_id), row(-1),
      col(-1) {}

TileRegionMapEntry::TileRegionMapEntry(TileId tile_id_value,
                                       RegionId region_id_value,
                                       int row_value, int col_value)
    : tile_id(tile_id_value), region_id(region_id_value), row(row_value),
      col(col_value) {}

TileRegionStatsEntry::TileRegionStatsEntry()
    : region_id(missing_region_id), tile_count(0), pickup_count(0),
      dropoff_count(0), available_driver_count(0), avg_hotspot_score(0.0),
      avg_cold_score(1.0), min_tile_id(invalid_tile_id), min_row(-1),
      max_row(-1), min_col(-1), max_col(-1), approx_width_km(0.0),
      approx_height_km(0.0), approx_diagonal_km(0.0), approx_area_km2(0.0) {}

TileRegionMap::TileRegionMap() : tile_entries_(), region_entries_() {}

TileRegionMap::TileRegionMap(
    std::vector<TileRegionMapEntry> tile_entries,
    std::vector<TileRegionStatsEntry> region_entries)
    : tile_entries_(std::move(tile_entries)),
      region_entries_(std::move(region_entries)) {}

RegionId TileRegionMap::region_for_tile(TileId tile_id) const {
  const auto entry_it =
      std::lower_bound(tile_entries_.begin(), tile_entries_.end(), tile_id,
                       [](const TileRegionMapEntry &entry, TileId value) {
                         return entry.tile_id < value;
                       });
  if (entry_it == tile_entries_.end() || entry_it->tile_id != tile_id) {
    return missing_region_id;
  }
  return entry_it->region_id;
}

const std::vector<TileRegionMapEntry> &TileRegionMap::tile_entries() const {
  return tile_entries_;
}

const std::vector<TileRegionStatsEntry> &
TileRegionMap::region_entries() const {
  return region_entries_;
}

bool is_valid_region_tile(TileId tile_id,
                          const TileRegionMapOptions &options) {
  if (options.grid_cols <= 0) {
    return false;
  }
  const int max_tile_count = options.grid_cols * options.grid_cols;
  return tile_id >= 0 && tile_id < max_tile_count;
}

int tile_region_row(TileId tile_id, const TileRegionMapOptions &options) {
  return tile_id / options.grid_cols;
}

int tile_region_col(TileId tile_id, const TileRegionMapOptions &options) {
  return tile_id % options.grid_cols;
}

TileRegionMap build_tile_region_map(const TileGridStats &tile_stats,
                                    TileRegionMapOptions options) {
  std::vector<TileGridStatsEntry> entries = tile_stats.entries();
  std::size_t max_dropoff_count = 0;
  std::size_t max_available_driver_count = 0;
  for (const auto &entry : entries) {
    if (!is_valid_region_tile(entry.tile_id, options)) {
      continue;
    }
    max_dropoff_count = std::max(max_dropoff_count, entry.dropoff_count);
    max_available_driver_count =
        std::max(max_available_driver_count, entry.available_driver_count);
  }

  std::vector<TileFeature> tiles;
  tiles.reserve(entries.size());
  std::unordered_map<TileId, std::size_t> index_by_tile;
  for (const auto &entry : entries) {
    if (!is_valid_region_tile(entry.tile_id, options)) {
      continue;
    }
    TileFeature tile;
    tile.entry = entry;
    tile.row = tile_region_row(entry.tile_id, options);
    tile.col = tile_region_col(entry.tile_id, options);
    tile.pickup_heat = entry.hotspot_score;
    tile.dropoff_density =
        normalized_density(entry.dropoff_count, max_dropoff_count);
    tile.free_driver_density = normalized_density(
        entry.available_driver_count, max_available_driver_count);
    index_by_tile.emplace(entry.tile_id, tiles.size());
    tiles.push_back(tile);
  }

  std::vector<CandidateRegionEdge> edges;
  for (std::size_t index = 0; index < tiles.size(); ++index) {
    const TileId right_tile = tiles[index].entry.tile_id + 1;
    const TileId down_tile = tiles[index].entry.tile_id + options.grid_cols;
    const TileId candidates[] = {right_tile, down_tile};
    for (const TileId candidate : candidates) {
      const auto neighbor_it = index_by_tile.find(candidate);
      if (neighbor_it == index_by_tile.end()) {
        continue;
      }
      const TileFeature &neighbor = tiles[neighbor_it->second];
      if (std::abs(neighbor.row - tiles[index].row) +
              std::abs(neighbor.col - tiles[index].col) !=
          1) {
        continue;
      }
      const double similarity = behavior_similarity(tiles[index], neighbor);
      if (similarity >= options.similarity_threshold) {
        edges.push_back(
            CandidateRegionEdge{index, neighbor_it->second, similarity});
      }
    }
  }

  std::sort(edges.begin(), edges.end(),
            [&tiles](const CandidateRegionEdge &lhs,
                     const CandidateRegionEdge &rhs) {
              if (lhs.similarity != rhs.similarity) {
                return lhs.similarity > rhs.similarity;
              }
              const TileId lhs_min =
                  std::min(tiles[lhs.lhs].entry.tile_id,
                           tiles[lhs.rhs].entry.tile_id);
              const TileId rhs_min =
                  std::min(tiles[rhs.lhs].entry.tile_id,
                           tiles[rhs.rhs].entry.tile_id);
              if (lhs_min != rhs_min) {
                return lhs_min < rhs_min;
              }
              const TileId lhs_max =
                  std::max(tiles[lhs.lhs].entry.tile_id,
                           tiles[lhs.rhs].entry.tile_id);
              const TileId rhs_max =
                  std::max(tiles[rhs.lhs].entry.tile_id,
                           tiles[rhs.rhs].entry.tile_id);
              return lhs_max < rhs_max;
            });

  std::vector<std::size_t> parents(tiles.size());
  std::iota(parents.begin(), parents.end(), 0);
  std::vector<RegionAggregate> aggregates;
  aggregates.reserve(tiles.size());
  for (const auto &tile : tiles) {
    aggregates.push_back(aggregate_for_tile(tile));
  }

  for (const auto &edge : edges) {
    const std::size_t lhs_root = find_root(parents, edge.lhs);
    const std::size_t rhs_root = find_root(parents, edge.rhs);
    if (lhs_root == rhs_root) {
      continue;
    }
    const RegionAggregate merged =
        merge_aggregates(aggregates[lhs_root], aggregates[rhs_root]);
    if (!aggregate_fits(merged, options)) {
      continue;
    }
    union_roots(parents, aggregates, lhs_root, rhs_root, merged);
  }

  std::vector<std::size_t> roots;
  for (std::size_t index = 0; index < tiles.size(); ++index) {
    const std::size_t root = find_root(parents, index);
    if (std::find(roots.begin(), roots.end(), root) == roots.end()) {
      roots.push_back(root);
    }
  }
  std::sort(roots.begin(), roots.end(),
            [&aggregates](std::size_t lhs, std::size_t rhs) {
              return aggregates[lhs].min_tile_id < aggregates[rhs].min_tile_id;
            });

  std::unordered_map<std::size_t, RegionId> region_id_by_root;
  std::vector<TileRegionStatsEntry> region_entries;
  region_entries.reserve(roots.size());
  for (std::size_t index = 0; index < roots.size(); ++index) {
    const RegionId region_id = static_cast<RegionId>(index);
    region_id_by_root.emplace(roots[index], region_id);
    region_entries.push_back(
        to_stats_entry(region_id, aggregates[roots[index]], options));
  }

  std::vector<TileRegionMapEntry> tile_entries;
  tile_entries.reserve(tiles.size());
  for (std::size_t index = 0; index < tiles.size(); ++index) {
    const std::size_t root = find_root_const(parents, index);
    const RegionId region_id = region_id_by_root[root];
    tile_entries.emplace_back(tiles[index].entry.tile_id, region_id,
                              tiles[index].row, tiles[index].col);
  }
  std::sort(tile_entries.begin(), tile_entries.end(),
            [](const TileRegionMapEntry &lhs,
               const TileRegionMapEntry &rhs) {
              return lhs.tile_id < rhs.tile_id;
            });

  return TileRegionMap(tile_entries, region_entries);
}

std::string format_tile_region_map_csv(const TileRegionMap &region_map) {
  std::ostringstream stream;
  stream << "tile_id,region_id\n";
  for (const auto &entry : region_map.tile_entries()) {
    stream << entry.tile_id << ',' << entry.region_id << '\n';
  }
  return stream.str();
}

std::string format_tile_region_stats_csv(const TileRegionMap &region_map) {
  std::ostringstream stream;
  stream << "region_id,tile_count,pickup_count,dropoff_count,"
            "available_driver_count,avg_hotspot_score,avg_cold_score,"
            "min_tile_id,min_row,max_row,min_col,max_col,"
            "approx_width_km,approx_height_km,approx_diagonal_km,"
            "approx_area_km2\n";
  stream << std::fixed << std::setprecision(6);
  for (const auto &entry : region_map.region_entries()) {
    stream << entry.region_id << ',' << entry.tile_count << ','
           << entry.pickup_count << ',' << entry.dropoff_count << ','
           << entry.available_driver_count << ',' << entry.avg_hotspot_score
           << ',' << entry.avg_cold_score << ',' << entry.min_tile_id << ','
           << entry.min_row << ',' << entry.max_row << ',' << entry.min_col
           << ',' << entry.max_col << ',' << entry.approx_width_km << ','
           << entry.approx_height_km << ',' << entry.approx_diagonal_km << ','
           << entry.approx_area_km2 << '\n';
  }
  return stream.str();
}
