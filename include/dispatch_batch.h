#pragma once

#include "taxi_domain.h"

#include <utility>
#include <vector>

using TileId = int;
using TimeSeconds = long long;

constexpr TileId invalid_tile_id = -1;

struct PassengerRequest {
  int request_id;
  int customer_id;
  TimeSeconds request_time;
  Point pickup_location;
  Point dropoff_location;
  TileId pickup_tile;
  TileId dropoff_tile;

  PassengerRequest()
      : request_id(-1), customer_id(-1), request_time(0),
        pickup_location(), dropoff_location(), pickup_tile(invalid_tile_id),
        dropoff_tile(invalid_tile_id) {}

  PassengerRequest(int request_id_value, int customer_id_value,
                   TimeSeconds request_time_value, const Point &pickup,
                   const Point &dropoff, TileId pickup_tile_value,
                   TileId dropoff_tile_value)
      : request_id(request_id_value), customer_id(customer_id_value),
        request_time(request_time_value), pickup_location(pickup),
        dropoff_location(dropoff), pickup_tile(pickup_tile_value),
        dropoff_tile(dropoff_tile_value) {}
};

struct TripRecord {
  int trip_id;
  int taxi_id;
  TimeSeconds pickup_time;
  TimeSeconds dropoff_time;
  TileId pickup_tile;
  TileId dropoff_tile;

  TripRecord()
      : trip_id(-1), taxi_id(-1), pickup_time(0), dropoff_time(0),
        pickup_tile(invalid_tile_id), dropoff_tile(invalid_tile_id) {}

  TripRecord(int trip_id_value, int taxi_id_value,
             TimeSeconds pickup_time_value, TimeSeconds dropoff_time_value,
             TileId pickup_tile_value, TileId dropoff_tile_value)
      : trip_id(trip_id_value), taxi_id(taxi_id_value),
        pickup_time(pickup_time_value), dropoff_time(dropoff_time_value),
        pickup_tile(pickup_tile_value), dropoff_tile(dropoff_tile_value) {}
};

struct DriverSnapshot {
  int taxi_id;
  Point location;
  TileId current_tile;
  TaxiStatus status;
  TimeSeconds available_time;

  DriverSnapshot()
      : taxi_id(-1), location(), current_tile(invalid_tile_id),
        status(TaxiStatus::offline), available_time(0) {}

  DriverSnapshot(int taxi_id_value, const Point &location_value,
                 TileId current_tile_value, TaxiStatus status_value,
                 TimeSeconds available_time_value)
      : taxi_id(taxi_id_value), location(location_value),
        current_tile(current_tile_value), status(status_value),
        available_time(available_time_value) {}
};

struct Assignment {
  int taxi_id;
  int request_id;
  int pickup_cost;

  Assignment() : taxi_id(-1), request_id(-1), pickup_cost(0) {}

  Assignment(int taxi_id_value, int request_id_value, int pickup_cost_value)
      : taxi_id(taxi_id_value), request_id(request_id_value),
        pickup_cost(pickup_cost_value) {}
};

struct CandidateEdge {
  int taxi_id;
  int request_id;
  int pickup_cost;

  CandidateEdge() : taxi_id(-1), request_id(-1), pickup_cost(0) {}

  CandidateEdge(int taxi_id_value, int request_id_value,
                int pickup_cost_value)
      : taxi_id(taxi_id_value), request_id(request_id_value),
        pickup_cost(pickup_cost_value) {}
};

struct BatchDispatchInput {
  TimeSeconds batch_time;
  std::vector<DriverSnapshot> drivers;
  std::vector<PassengerRequest> requests;

  BatchDispatchInput() : batch_time(0), drivers(), requests() {}

  BatchDispatchInput(TimeSeconds batch_time_value,
                     std::vector<DriverSnapshot> driver_values,
                     std::vector<PassengerRequest> request_values)
      : batch_time(batch_time_value), drivers(std::move(driver_values)),
        requests(std::move(request_values)) {}
};
