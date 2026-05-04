#include "dispatch_replay.h"
#include "dispatch_replay_io.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct KValue {
  std::size_t value;
  std::string label;
};

struct CliOptions {
  std::string requests_path = "data/normalized/requests.csv";
  std::string drivers_path = "data/normalized/drivers.csv";
  TimeSeconds start_time = 0;
  TimeSeconds end_time = 0;
  bool end_time_set = false;
  TimeSeconds batch_interval_seconds = 30;
  TimeSeconds trip_duration_seconds = 600;
  double seconds_per_distance_unit = 100000.0;
  std::vector<double> radii = {0.03};
  std::vector<KValue> k_values = {{1, "1"},   {2, "2"},   {3, "3"},
                                  {5, "5"},   {8, "8"},   {10, "10"},
                                  {20, "20"}, {50, "50"}, {0, "unlimited"}};
  bool same_tile_only = false;
};

void print_usage(const char *program) {
  std::cout
      << "Usage: " << program << " [options]\n"
      << "\n"
      << "Options:\n"
      << "  --requests PATH                 normalized requests.csv path\n"
      << "  --drivers PATH                  normalized drivers.csv path\n"
      << "  --start-time SECONDS            replay start time, default 0\n"
      << "  --end-time SECONDS              replay end time, default auto\n"
      << "  --batch-interval SECONDS        batch interval, default 30\n"
      << "  --trip-duration SECONDS         fixed trip duration, default 600\n"
      << "  --radii LIST                    comma-separated radii, default 0.03\n"
      << "  --k-values LIST                 comma-separated k values, 0 or unlimited means no top-k limit\n"
      << "  --seconds-per-distance-unit N   pickup cost scale, default 100000\n"
      << "  --same-tile-only                only generate same-tile candidates\n"
      << "  --help                          show this help\n";
}

std::string require_value(int &index, int argc, char **argv) {
  if (index + 1 >= argc) {
    throw std::runtime_error(std::string("missing value for ") + argv[index]);
  }
  ++index;
  return argv[index];
}

std::vector<std::string> split_list(const std::string &text) {
  std::vector<std::string> values;
  std::stringstream stream(text);
  std::string value;
  while (std::getline(stream, value, ',')) {
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), [](unsigned char ch) {
                  return !std::isspace(ch);
                }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
                             [](unsigned char ch) {
                               return !std::isspace(ch);
                             })
                    .base(),
                value.end());
    if (!value.empty()) {
      values.push_back(value);
    }
  }
  return values;
}

TimeSeconds parse_time_value(const std::string &text,
                             const std::string &name) {
  std::size_t parsed = 0;
  const long long value = std::stoll(text, &parsed);
  if (parsed != text.size()) {
    throw std::runtime_error("invalid " + name + ": " + text);
  }
  return value;
}

double parse_double_value(const std::string &text, const std::string &name) {
  std::size_t parsed = 0;
  const double value = std::stod(text, &parsed);
  if (parsed != text.size()) {
    throw std::runtime_error("invalid " + name + ": " + text);
  }
  return value;
}

std::size_t parse_size_value(const std::string &text,
                             const std::string &name) {
  std::size_t parsed = 0;
  const unsigned long long value = std::stoull(text, &parsed);
  if (parsed != text.size() ||
      value > static_cast<unsigned long long>(
                  std::numeric_limits<std::size_t>::max())) {
    throw std::runtime_error("invalid " + name + ": " + text);
  }
  return static_cast<std::size_t>(value);
}

std::vector<double> parse_radii(const std::string &text) {
  std::vector<double> values;
  for (const auto &item : split_list(text)) {
    const double value = parse_double_value(item, "--radii");
    if (value < 0.0) {
      throw std::runtime_error("--radii values must be non-negative");
    }
    values.push_back(value);
  }
  if (values.empty()) {
    throw std::runtime_error("--radii must contain at least one value");
  }
  return values;
}

std::vector<KValue> parse_k_values(const std::string &text) {
  std::vector<KValue> values;
  for (const auto &item : split_list(text)) {
    if (item == "unlimited" || item == "none") {
      values.push_back(KValue{0, "unlimited"});
      continue;
    }
    const std::size_t value = parse_size_value(item, "--k-values");
    values.push_back(KValue{value, value == 0 ? "unlimited" : item});
  }
  if (values.empty()) {
    throw std::runtime_error("--k-values must contain at least one value");
  }
  return values;
}

CliOptions parse_cli(int argc, char **argv) {
  CliOptions options;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      std::exit(0);
    }
    if (arg == "--requests") {
      options.requests_path = require_value(index, argc, argv);
    } else if (arg == "--drivers") {
      options.drivers_path = require_value(index, argc, argv);
    } else if (arg == "--start-time") {
      options.start_time =
          parse_time_value(require_value(index, argc, argv), arg);
    } else if (arg == "--end-time") {
      options.end_time = parse_time_value(require_value(index, argc, argv), arg);
      options.end_time_set = true;
    } else if (arg == "--batch-interval") {
      options.batch_interval_seconds =
          parse_time_value(require_value(index, argc, argv), arg);
    } else if (arg == "--trip-duration") {
      options.trip_duration_seconds =
          parse_time_value(require_value(index, argc, argv), arg);
    } else if (arg == "--radii") {
      options.radii = parse_radii(require_value(index, argc, argv));
    } else if (arg == "--k-values") {
      options.k_values = parse_k_values(require_value(index, argc, argv));
    } else if (arg == "--seconds-per-distance-unit") {
      options.seconds_per_distance_unit =
          parse_double_value(require_value(index, argc, argv), arg);
    } else if (arg == "--same-tile-only") {
      options.same_tile_only = true;
    } else {
      throw std::runtime_error("unknown option: " + arg);
    }
  }

  if (options.batch_interval_seconds <= 0) {
    throw std::runtime_error("--batch-interval must be positive");
  }
  if (options.trip_duration_seconds < 0) {
    throw std::runtime_error("--trip-duration must be non-negative");
  }
  if (options.seconds_per_distance_unit <= 0.0) {
    throw std::runtime_error("--seconds-per-distance-unit must be positive");
  }
  return options;
}

void print_errors(const std::vector<std::string> &errors,
                  const std::string &label) {
  if (errors.empty()) {
    return;
  }

  std::cerr << label << " warnings=" << errors.size() << '\n';
  const std::size_t limit = std::min<std::size_t>(errors.size(), 10);
  for (std::size_t index = 0; index < limit; ++index) {
    std::cerr << "  " << errors[index] << '\n';
  }
  if (errors.size() > limit) {
    std::cerr << "  ... " << (errors.size() - limit) << " more\n";
  }
}

TimeSeconds infer_end_time(const std::vector<PassengerRequest> &requests,
                           const CliOptions &options) {
  TimeSeconds max_request_time = options.start_time;
  for (const auto &request : requests) {
    max_request_time = std::max(max_request_time, request.request_time);
  }
  return max_request_time + options.batch_interval_seconds +
         options.trip_duration_seconds;
}

void print_csv_header() {
  std::cout << "radius,k,requests,drivers,batch_runs,assigned,completed,"
               "unserved,assignment_rate,completion_rate,candidate_edges,"
               "requests_with_edges,requests_without_edges,"
               "unique_requests_without_edges,greedy_assigned,"
               "mcmf_assigned,greedy_cost,mcmf_cost,applied_pickup_cost,"
               "avg_pickup_cost,assignment_wait_time,avg_assignment_wait\n";
}

void print_csv_row(double radius, const KValue &k, std::size_t driver_count,
                   const DispatchReplayMetrics &metrics) {
  std::cout << std::fixed << std::setprecision(6) << radius << ',' << k.label
            << ',' << metrics.total_requests << ',' << driver_count << ','
            << metrics.batch_runs << ',' << metrics.assigned_requests << ','
            << metrics.completed_requests << ',' << metrics.unserved_requests
            << ',' << std::setprecision(4) << assignment_rate(metrics) << ','
            << completion_rate(metrics) << ',' << std::setprecision(6)
            << metrics.candidate_edges_total << ','
            << metrics.requests_with_edges_total << ','
            << metrics.requests_without_edges_total << ','
            << metrics.unique_requests_without_edges << ','
            << metrics.greedy_assigned_total << ','
            << metrics.mcmf_assigned_total << ',' << metrics.greedy_cost_total
            << ',' << metrics.mcmf_cost_total << ','
            << metrics.applied_pickup_cost_total << ','
            << std::setprecision(4) << average_applied_pickup_cost(metrics)
            << ',' << metrics.wait_time_total << ','
            << average_assignment_wait_time(metrics)
            << '\n';
}

} // namespace

int main(int argc, char **argv) {
  try {
    const CliOptions cli_options = parse_cli(argc, argv);

    const PassengerRequestCsvLoadResult request_result =
        load_passenger_requests_csv(cli_options.requests_path);
    const DriverSnapshotCsvLoadResult driver_result =
        load_driver_snapshots_csv(cli_options.drivers_path);

    print_errors(request_result.errors, "request CSV");
    print_errors(driver_result.errors, "driver CSV");

    if (request_result.requests.empty()) {
      std::cerr << "no usable requests loaded from " << cli_options.requests_path
                << '\n';
      return 1;
    }
    if (driver_result.drivers.empty()) {
      std::cerr << "no usable drivers loaded from " << cli_options.drivers_path
                << '\n';
      return 1;
    }

    const TimeSeconds end_time =
        cli_options.end_time_set
            ? cli_options.end_time
            : infer_end_time(request_result.requests, cli_options);

    DispatchReplaySimulator simulator;
    print_csv_header();
    for (const double radius : cli_options.radii) {
      for (const auto &k : cli_options.k_values) {
        CandidateEdgeOptions candidate_options(
            radius, cli_options.seconds_per_distance_unit, k.value,
            cli_options.same_tile_only);
        DispatchReplayOptions replay_options(
            cli_options.start_time, end_time,
            cli_options.batch_interval_seconds,
            cli_options.trip_duration_seconds, candidate_options, false);
        const DispatchReplayReport report = simulator.run_report(
            driver_result.drivers, request_result.requests, replay_options);
        print_csv_row(radius, k, driver_result.drivers.size(), report.metrics);
      }
    }

    return 0;
  } catch (const std::exception &error) {
    std::cerr << "error: " << error.what() << '\n';
    std::cerr << "try --help for usage\n";
    return 1;
  }
}
