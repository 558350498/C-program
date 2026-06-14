#pragma once

#include "dispatch_batch.h"

#include <vector>

struct CellBounds {
  double min_lon;
  double min_lat;
  double max_lon;
  double max_lat;

  CellBounds();
  CellBounds(double min_lon_value, double min_lat_value,
             double max_lon_value, double max_lat_value);
};

class CellIndex {
public:
  virtual ~CellIndex() = default;

  virtual TileId encode(double lon, double lat) const = 0;
  virtual std::vector<TileId> neighbors(TileId cell_id) const = 0;
  virtual CellBounds boundary(TileId cell_id) const = 0;
  virtual TileId parent(TileId cell_id, int parent_grid_cols) const = 0;
};

class SimpleTileCellIndex : public CellIndex {
public:
  SimpleTileCellIndex();
  explicit SimpleTileCellIndex(int grid_cols);
  SimpleTileCellIndex(int grid_cols, double min_lon, double max_lon,
                      double min_lat, double max_lat);

  TileId encode(double lon, double lat) const override;
  std::vector<TileId> neighbors(TileId cell_id) const override;
  CellBounds boundary(TileId cell_id) const override;
  TileId parent(TileId cell_id, int parent_grid_cols) const override;

  int grid_cols() const;
  bool is_valid(TileId cell_id) const;
  int row(TileId cell_id) const;
  int col(TileId cell_id) const;

private:
  int grid_cols_;
  double min_lon_;
  double max_lon_;
  double min_lat_;
  double max_lat_;
};
