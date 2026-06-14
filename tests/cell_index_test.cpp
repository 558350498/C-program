#include "cell_index.h"

#include <algorithm>
#include <vector>

#define REQUIRE(condition)                                                      \
  do {                                                                         \
    if (!(condition)) {                                                        \
      return 1;                                                                \
    }                                                                          \
  } while (false)

int main() {
  SimpleTileCellIndex index(10, 0.0, 10.0, 0.0, 10.0);

  REQUIRE(index.grid_cols() == 10);
  REQUIRE(index.encode(0.0, 0.0) == 0);
  REQUIRE(index.encode(9.99, 9.99) == 99);
  REQUIRE(index.encode(10.0, 10.0) == 99);
  REQUIRE(index.encode(-1.0, -1.0) == 0);

  REQUIRE(index.row(42) == 4);
  REQUIRE(index.col(42) == 2);
  REQUIRE(index.is_valid(99));
  REQUIRE(!index.is_valid(100));

  const std::vector<TileId> center_neighbors = index.neighbors(55);
  REQUIRE(center_neighbors.size() == 4);
  REQUIRE(std::find(center_neighbors.begin(), center_neighbors.end(), 45) !=
          center_neighbors.end());
  REQUIRE(std::find(center_neighbors.begin(), center_neighbors.end(), 54) !=
          center_neighbors.end());
  REQUIRE(std::find(center_neighbors.begin(), center_neighbors.end(), 56) !=
          center_neighbors.end());
  REQUIRE(std::find(center_neighbors.begin(), center_neighbors.end(), 65) !=
          center_neighbors.end());

  const std::vector<TileId> corner_neighbors = index.neighbors(0);
  REQUIRE(corner_neighbors.size() == 2);
  REQUIRE(std::find(corner_neighbors.begin(), corner_neighbors.end(), 1) !=
          corner_neighbors.end());
  REQUIRE(std::find(corner_neighbors.begin(), corner_neighbors.end(), 10) !=
          corner_neighbors.end());

  const CellBounds bounds = index.boundary(42);
  REQUIRE(bounds.min_lon == 2.0);
  REQUIRE(bounds.max_lon == 3.0);
  REQUIRE(bounds.min_lat == 4.0);
  REQUIRE(bounds.max_lat == 5.0);

  REQUIRE(index.parent(42, 5) == 11);
  REQUIRE(index.parent(42, 0) == invalid_tile_id);
  REQUIRE(index.neighbors(invalid_tile_id).empty());

  return 0;
}
