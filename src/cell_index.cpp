#include "cell_index.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double default_min_lon = -74.3;
constexpr double default_max_lon = -73.6;
constexpr double default_min_lat = 40.45;
constexpr double default_max_lat = 40.95;

int clamp_index(double normalized, int grid_cols) {
  if (grid_cols <= 0 || !std::isfinite(normalized)) {
    return -1;
  }
  const int raw = static_cast<int>(std::floor(normalized * grid_cols));
  return std::clamp(raw, 0, grid_cols - 1);
}

} // namespace

CellBounds::CellBounds()
    : min_lon(0.0), min_lat(0.0), max_lon(0.0), max_lat(0.0) {}

CellBounds::CellBounds(double min_lon_value, double min_lat_value,
                       double max_lon_value, double max_lat_value)
    : min_lon(min_lon_value), min_lat(min_lat_value), max_lon(max_lon_value),
      max_lat(max_lat_value) {}

SimpleTileCellIndex::SimpleTileCellIndex()
    : SimpleTileCellIndex(100) {}

SimpleTileCellIndex::SimpleTileCellIndex(int grid_cols)
    : SimpleTileCellIndex(grid_cols, default_min_lon, default_max_lon,
                          default_min_lat, default_max_lat) {}

SimpleTileCellIndex::SimpleTileCellIndex(int grid_cols, double min_lon,
                                         double max_lon, double min_lat,
                                         double max_lat)
    : grid_cols_(grid_cols), min_lon_(min_lon), max_lon_(max_lon),
      min_lat_(min_lat), max_lat_(max_lat) {}

TileId SimpleTileCellIndex::encode(double lon, double lat) const {
  if (grid_cols_ <= 0 || max_lon_ <= min_lon_ || max_lat_ <= min_lat_) {
    return invalid_tile_id;
  }
  const double normalized_lon = (lon - min_lon_) / (max_lon_ - min_lon_);
  const double normalized_lat = (lat - min_lat_) / (max_lat_ - min_lat_);
  const int col_value = clamp_index(normalized_lon, grid_cols_);
  const int row_value = clamp_index(normalized_lat, grid_cols_);
  if (col_value < 0 || row_value < 0) {
    return invalid_tile_id;
  }
  return row_value * grid_cols_ + col_value;
}

std::vector<TileId> SimpleTileCellIndex::neighbors(TileId cell_id) const {
  std::vector<TileId> result;
  if (!is_valid(cell_id)) {
    return result;
  }
  const int row_value = row(cell_id);
  const int col_value = col(cell_id);
  const auto add = [&](int next_row, int next_col) {
    if (next_row < 0 || next_col < 0 || next_row >= grid_cols_ ||
        next_col >= grid_cols_) {
      return;
    }
    result.push_back(next_row * grid_cols_ + next_col);
  };
  add(row_value - 1, col_value);
  add(row_value, col_value - 1);
  add(row_value, col_value + 1);
  add(row_value + 1, col_value);
  return result;
}

CellBounds SimpleTileCellIndex::boundary(TileId cell_id) const {
  if (!is_valid(cell_id) || max_lon_ <= min_lon_ || max_lat_ <= min_lat_) {
    return CellBounds();
  }
  const double cell_width = (max_lon_ - min_lon_) / grid_cols_;
  const double cell_height = (max_lat_ - min_lat_) / grid_cols_;
  const int row_value = row(cell_id);
  const int col_value = col(cell_id);
  const double min_lon = min_lon_ + col_value * cell_width;
  const double min_lat = min_lat_ + row_value * cell_height;
  return CellBounds(min_lon, min_lat, min_lon + cell_width,
                    min_lat + cell_height);
}

TileId SimpleTileCellIndex::parent(TileId cell_id,
                                   int parent_grid_cols) const {
  if (!is_valid(cell_id) || parent_grid_cols <= 0) {
    return invalid_tile_id;
  }
  const CellBounds bounds = boundary(cell_id);
  const double center_lon = (bounds.min_lon + bounds.max_lon) / 2.0;
  const double center_lat = (bounds.min_lat + bounds.max_lat) / 2.0;
  return SimpleTileCellIndex(parent_grid_cols, min_lon_, max_lon_, min_lat_,
                             max_lat_)
      .encode(center_lon, center_lat);
}

int SimpleTileCellIndex::grid_cols() const { return grid_cols_; }

bool SimpleTileCellIndex::is_valid(TileId cell_id) const {
  if (grid_cols_ <= 0) {
    return false;
  }
  const long long max_cells =
      static_cast<long long>(grid_cols_) * static_cast<long long>(grid_cols_);
  return cell_id >= 0 && static_cast<long long>(cell_id) < max_cells;
}

int SimpleTileCellIndex::row(TileId cell_id) const {
  if (!is_valid(cell_id)) {
    return -1;
  }
  return cell_id / grid_cols_;
}

int SimpleTileCellIndex::col(TileId cell_id) const {
  if (!is_valid(cell_id)) {
    return -1;
  }
  return cell_id % grid_cols_;
}
