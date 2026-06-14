#include "dispatch_replay_io.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {

using CsvRow = std::unordered_map<std::string, std::string>;

std::string trim(const std::string &value) {
  std::size_t begin = 0;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }

  std::size_t end = value.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }

  return value.substr(begin, end - begin);
}

std::string normalize_header(std::string value) {
  value = trim(value);
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   if (ch == ' ') {
                     return '_';
                   }
                   return static_cast<char>(std::tolower(ch));
                 });
  return value;
}

std::vector<std::string> split_csv_line(const std::string &line) {
  std::vector<std::string> fields;
  std::string current;
  bool in_quotes = false;

  for (std::size_t index = 0; index < line.size(); ++index) {
    const char ch = line[index];
    if (ch == '"') {
      if (in_quotes && index + 1 < line.size() && line[index + 1] == '"') {
        current.push_back('"');
        ++index;
      } else {
        in_quotes = !in_quotes;
      }
      continue;
    }

    if (ch == ',' && !in_quotes) {
      fields.push_back(trim(current));
      current.clear();
      continue;
    }

    current.push_back(ch);
  }

  fields.push_back(trim(current));
  return fields;
}

int parse_int(const CsvRow &row, const std::string &name) {
  const auto it = row.find(name);
  if (it == row.end() || it->second.empty()) {
    throw std::runtime_error("missing " + name);
  }

  std::size_t parsed = 0;
  const int value = std::stoi(it->second, &parsed);
  if (parsed != it->second.size()) {
    throw std::runtime_error("invalid integer " + name);
  }
  return value;
}

TimeSeconds parse_time_seconds(const CsvRow &row, const std::string &name) {
  const auto it = row.find(name);
  if (it == row.end() || it->second.empty()) {
    throw std::runtime_error("missing " + name);
  }

  std::size_t parsed = 0;
  const long long value = std::stoll(it->second, &parsed);
  if (parsed != it->second.size()) {
    throw std::runtime_error("invalid time " + name);
  }
  return value;
}

double parse_double(const CsvRow &row, const std::string &name) {
  const auto it = row.find(name);
  if (it == row.end() || it->second.empty()) {
    throw std::runtime_error("missing " + name);
  }

  std::size_t parsed = 0;
  const double value = std::stod(it->second, &parsed);
  if (parsed != it->second.size()) {
    throw std::runtime_error("invalid double " + name);
  }
  return value;
}

bool has_value(const CsvRow &row, const std::string &name) {
  const auto it = row.find(name);
  return it != row.end() && !it->second.empty();
}

double parse_first_available_double(const CsvRow &row,
                                    const std::vector<std::string> &names) {
  for (const auto &name : names) {
    if (has_value(row, name)) {
      return parse_double(row, name);
    }
  }
  throw std::runtime_error("missing route cost column");
}

TaxiStatus parse_status(const CsvRow &row) {
  const auto it = row.find("status");
  if (it == row.end() || it->second.empty()) {
    return TaxiStatus::free;
  }

  std::string value = normalize_header(it->second);
  if (value == "free") {
    return TaxiStatus::free;
  }
  if (value == "occupy" || value == "occupied") {
    return TaxiStatus::occupy;
  }
  if (value == "offline") {
    return TaxiStatus::offline;
  }

  throw std::runtime_error("invalid status");
}

CsvRow make_row(const std::vector<std::string> &header,
                const std::vector<std::string> &fields) {
  CsvRow row;
  for (std::size_t index = 0; index < header.size(); ++index) {
    if (index < fields.size()) {
      row.emplace(header[index], fields[index]);
    } else {
      row.emplace(header[index], "");
    }
  }
  return row;
}

std::string row_error(std::size_t row_number, const std::string &message) {
  std::ostringstream stream;
  stream << "row " << row_number << ": " << message;
  return stream.str();
}

std::vector<std::string> read_header(std::ifstream &file,
                                     std::vector<std::string> &errors) {
  std::string line;
  if (!std::getline(file, line)) {
    errors.push_back("missing header");
    return {};
  }

  std::vector<std::string> header = split_csv_line(line);
  for (auto &name : header) {
    name = normalize_header(name);
  }
  return header;
}

} // namespace

PassengerRequestCsvLoadResult
load_passenger_requests_csv(const std::string &path) {
  PassengerRequestCsvLoadResult result;
  std::ifstream file(path);
  if (!file) {
    result.errors.push_back("cannot open " + path);
    return result;
  }

  const std::vector<std::string> header = read_header(file, result.errors);
  if (header.empty()) {
    return result;
  }

  std::string line;
  std::size_t row_number = 1;
  while (std::getline(file, line)) {
    ++row_number;
    if (trim(line).empty()) {
      continue;
    }

    try {
      const CsvRow row = make_row(header, split_csv_line(line));
      const int request_id = parse_int(row, "request_id");
      const int customer_id = parse_int(row, "customer_id");
      const TimeSeconds request_time =
          parse_time_seconds(row, "request_time");
      const double pickup_x = parse_double(row, "pickup_x");
      const double pickup_y = parse_double(row, "pickup_y");
      const double dropoff_x = parse_double(row, "dropoff_x");
      const double dropoff_y = parse_double(row, "dropoff_y");
      const TileId pickup_tile = parse_int(row, "pickup_tile");
      const TileId dropoff_tile = parse_int(row, "dropoff_tile");

      result.requests.emplace_back(
          request_id, customer_id, request_time,
          Point(pickup_x, pickup_y, request_id),
          Point(dropoff_x, dropoff_y, request_id), pickup_tile, dropoff_tile);
    } catch (const std::exception &error) {
      result.errors.push_back(row_error(row_number, error.what()));
    }
  }

  return result;
}

DriverSnapshotCsvLoadResult load_driver_snapshots_csv(const std::string &path) {
  DriverSnapshotCsvLoadResult result;
  std::ifstream file(path);
  if (!file) {
    result.errors.push_back("cannot open " + path);
    return result;
  }

  const std::vector<std::string> header = read_header(file, result.errors);
  if (header.empty()) {
    return result;
  }

  std::string line;
  std::size_t row_number = 1;
  while (std::getline(file, line)) {
    ++row_number;
    if (trim(line).empty()) {
      continue;
    }

    try {
      const CsvRow row = make_row(header, split_csv_line(line));
      const int taxi_id = parse_int(row, "taxi_id");
      const double x = parse_double(row, "x");
      const double y = parse_double(row, "y");
      const TileId tile = parse_int(row, "tile");
      const TimeSeconds available_time =
          parse_time_seconds(row, "available_time");
      const TaxiStatus status = parse_status(row);

      result.drivers.emplace_back(taxi_id, Point(x, y, taxi_id), tile, status,
                                  available_time);
    } catch (const std::exception &error) {
      result.errors.push_back(row_error(row_number, error.what()));
    }
  }

  return result;
}

RouteDispatchCostCsvLoadResult
load_route_dispatch_costs_csv(const std::string &path) {
  RouteDispatchCostCsvLoadResult result;
  std::ifstream file(path);
  if (!file) {
    result.errors.push_back("cannot open " + path);
    return result;
  }

  const std::vector<std::string> header = read_header(file, result.errors);
  if (header.empty()) {
    return result;
  }

  std::string line;
  std::size_t row_number = 1;
  while (std::getline(file, line)) {
    ++row_number;
    if (trim(line).empty()) {
      continue;
    }

    try {
      const CsvRow row = make_row(header, split_csv_line(line));
      if (has_value(row, "leg_type") &&
          normalize_header(row.at("leg_type")) != "dispatch_to_pickup") {
        ++result.skipped_rows;
        continue;
      }
      if (has_value(row, "route_status") &&
          normalize_header(row.at("route_status")) != "routed") {
        ++result.skipped_rows;
        continue;
      }

      const int taxi_id = parse_int(row, "taxi_id");
      const int request_id = parse_int(row, "request_id");
      const double raw_cost = parse_first_available_double(
          row, {"route_cost", "route_duration_s", "duration_s",
                "dispatch_cost"});
      if (!std::isfinite(raw_cost) || raw_cost < 0.0 ||
          raw_cost >
              static_cast<double>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("invalid route cost value");
      }

      result.model.cost_by_edge[route_dispatch_cost_key(taxi_id,
                                                        request_id)] =
          static_cast<int>(std::llround(raw_cost));
      if (has_value(row, "start_lon") && has_value(row, "start_lat") &&
          has_value(row, "end_lon") && has_value(row, "end_lat")) {
        result.model.cost_by_route_pair[route_pair_key(
            parse_double(row, "start_lon"), parse_double(row, "start_lat"),
            parse_double(row, "end_lon"), parse_double(row, "end_lat"))] =
            static_cast<int>(std::llround(raw_cost));
      }
      ++result.loaded_rows;
    } catch (const std::exception &error) {
      result.errors.push_back(row_error(row_number, error.what()));
    }
  }

  result.model.enabled = !result.model.cost_by_edge.empty() ||
                         !result.model.cost_by_route_pair.empty();
  return result;
}
