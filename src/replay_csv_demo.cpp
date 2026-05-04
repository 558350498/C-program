#include "dispatch_replay.h"
#include "dispatch_replay_io.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct CliOptions {
  std::string requests_path = "data/normalized/requests.csv";
  std::string drivers_path = "data/normalized/drivers.csv";
  TimeSeconds start_time = 0;
  TimeSeconds end_time = 0;
  bool end_time_set = false;
  TimeSeconds batch_interval_seconds = 30;
  TimeSeconds trip_duration_seconds = 600;
  double radius = 0.03;
  double seconds_per_distance_unit = 100000.0;
  std::size_t max_edges_per_request = 0;
  bool same_tile_only = false;
  bool use_indexed_candidate_edges = false;
  bool include_batch_logs = false;
  std::string batch_log_csv_path;
  std::string request_outcome_csv_path;
  bool taxi_system_logging_enabled = false;
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
      << "  --radius VALUE                  candidate radius, default 0.03\n"
      << "  --seconds-per-distance-unit N   pickup cost scale, default 100000\n"
      << "  --max-edges-per-request N       candidate top-k per request, default 0\n"
      << "  --same-tile-only                only generate same-tile candidates\n"
      << "  --indexed-candidates            generate candidates with KD-Tree index\n"
      << "  --batch-logs                    include per-batch logs in output\n"
      << "  --batch-log-csv PATH            write per-batch logs as CSV\n"
      << "  --request-outcome-csv PATH      write per-request replay outcomes as CSV\n"
      << "  --taxi-logs                     enable TaxiSystem state logs\n"
      << "  --help                          show this help\n";
}

std::string require_value(int &index, int argc, char **argv) {
  if (index + 1 >= argc) {
    throw std::runtime_error(std::string("missing value for ") + argv[index]);
  }
  ++index;
  return argv[index];
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
    } else if (arg == "--radius") {
      options.radius = parse_double_value(require_value(index, argc, argv), arg);
    } else if (arg == "--seconds-per-distance-unit") {
      options.seconds_per_distance_unit =
          parse_double_value(require_value(index, argc, argv), arg);
    } else if (arg == "--max-edges-per-request") {
      options.max_edges_per_request =
          parse_size_value(require_value(index, argc, argv), arg);
    } else if (arg == "--same-tile-only") {
      options.same_tile_only = true;
    } else if (arg == "--indexed-candidates") {
      options.use_indexed_candidate_edges = true;
    } else if (arg == "--batch-logs") {
      options.include_batch_logs = true;
    } else if (arg == "--batch-log-csv") {
      options.batch_log_csv_path = require_value(index, argc, argv);
    } else if (arg == "--request-outcome-csv") {
      options.request_outcome_csv_path = require_value(index, argc, argv);
    } else if (arg == "--taxi-logs") {
      options.taxi_system_logging_enabled = true;
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
  if (options.radius < 0.0) {
    throw std::runtime_error("--radius must be non-negative");
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

void write_batch_log_csv(const DispatchReplayReport &report,
                         const std::string &path_text) {
  if (path_text.empty()) {
    return;
  }

  const std::filesystem::path output_path(path_text);
  const std::filesystem::path parent_path = output_path.parent_path();
  if (!parent_path.empty()) {
    std::filesystem::create_directories(parent_path);
  }

  std::ofstream output(output_path);
  if (!output) {
    throw std::runtime_error("failed to open batch log CSV: " + path_text);
  }

  output << format_dispatch_replay_batch_logs_csv(report);
  if (!output) {
    throw std::runtime_error("failed to write batch log CSV: " + path_text);
  }
}

void write_request_outcome_csv(const DispatchReplayReport &report,
                               const std::string &path_text) {
  if (path_text.empty()) {
    return;
  }

  const std::filesystem::path output_path(path_text);
  const std::filesystem::path parent_path = output_path.parent_path();
  if (!parent_path.empty()) {
    std::filesystem::create_directories(parent_path);
  }

  std::ofstream output(output_path);
  if (!output) {
    throw std::runtime_error("failed to open request outcome CSV: " +
                             path_text);
  }

  output << format_dispatch_replay_request_outcomes_csv(report);
  if (!output) {
    throw std::runtime_error("failed to write request outcome CSV: " +
                             path_text);
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

    CandidateEdgeOptions candidate_options(
        cli_options.radius, cli_options.seconds_per_distance_unit,
        cli_options.max_edges_per_request, cli_options.same_tile_only);
    DispatchReplayOptions replay_options(
        cli_options.start_time, end_time, cli_options.batch_interval_seconds,
        cli_options.trip_duration_seconds, candidate_options,
        cli_options.use_indexed_candidate_edges,
        cli_options.taxi_system_logging_enabled);

    std::cout << "Replay CSV demo\n"
              << "requests=" << cli_options.requests_path
              << " loaded=" << request_result.requests.size() << '\n'
              << "drivers=" << cli_options.drivers_path
              << " loaded=" << driver_result.drivers.size() << '\n'
              << "options start_time=" << replay_options.start_time
              << " end_time=" << replay_options.end_time
              << " batch_interval_seconds="
              << replay_options.batch_interval_seconds
              << " trip_duration_seconds="
              << replay_options.trip_duration_seconds
              << " radius=" << candidate_options.radius
              << " seconds_per_distance_unit="
              << candidate_options.seconds_per_distance_unit
              << " max_edges_per_request="
              << candidate_options.max_edges_per_request
              << " same_tile_only="
              << (candidate_options.same_tile_only ? "true" : "false")
              << " candidate_generation="
              << (replay_options.use_indexed_candidate_edges ? "indexed"
                                                             : "scan")
              << '\n';

    DispatchReplaySimulator simulator;
    const DispatchReplayReport report = simulator.run_report(
        driver_result.drivers, request_result.requests, replay_options);
    write_batch_log_csv(report, cli_options.batch_log_csv_path);
    write_request_outcome_csv(report, cli_options.request_outcome_csv_path);
    std::cout << format_dispatch_replay_report(report,
                                               cli_options.include_batch_logs);
    if (!cli_options.batch_log_csv_path.empty()) {
      std::cout << "batch_log_csv=" << cli_options.batch_log_csv_path << '\n';
    }
    if (!cli_options.request_outcome_csv_path.empty()) {
      std::cout << "request_outcome_csv="
                << cli_options.request_outcome_csv_path << '\n';
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "error: " << error.what() << '\n';
    std::cerr << "try --help for usage\n";
    return 1;
  }
}
