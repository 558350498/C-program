#pragma once

#include "dispatch_batch.h"

#include <string>
#include <vector>

struct PassengerRequestCsvLoadResult {
  std::vector<PassengerRequest> requests;
  std::vector<std::string> errors;

  bool ok() const { return errors.empty(); }
};

struct DriverSnapshotCsvLoadResult {
  std::vector<DriverSnapshot> drivers;
  std::vector<std::string> errors;

  bool ok() const { return errors.empty(); }
};

PassengerRequestCsvLoadResult
load_passenger_requests_csv(const std::string &path);

DriverSnapshotCsvLoadResult load_driver_snapshots_csv(const std::string &path);

