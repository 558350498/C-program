#include "taxi_system.h"

#include "kd_tree_spatial_index.h"
#include "nearest_free_taxi_strategy.h"
#include <iostream>
#include <sstream>
#include <utility>

namespace {

void log_error(const char *operation, const std::string &message) {
  std::cerr << '[' << operation << "] " << message << '\n';
}

void log_info(const char *operation, const std::string &message) {
  std::clog << '[' << operation << "] " << message << '\n';
}

std::string taxi_status_message(int id, TaxiStatus status) {
  std::ostringstream stream;
  stream << "taxi_id=" << id << " status=" << to_string(status);
  return stream.str();
}

} // namespace

TaxiSystem::TaxiSystem(std::unique_ptr<ISpatialIndex> spatial_index,
                       std::unique_ptr<IDispatchStrategy> dispatch_strategy)
    : spatial_index_(std::move(spatial_index)),
      dispatch_strategy_(std::move(dispatch_strategy)), next_taxi_id_(0) {
  if (!spatial_index_) {
    spatial_index_ = std::make_unique<KdTreeSpatialIndex>();
  }
  if (!dispatch_strategy_) {
    dispatch_strategy_ = std::make_unique<NearestFreeTaxiStrategy>();
  }
}

TaxiSystem::~TaxiSystem() = default;

int TaxiSystem::create_taxi() {
  const int id = next_taxi_id_;
  ++next_taxi_id_;

  if (!register_taxi(id)) {
    log_error("TaxiSystem::create_taxi",
              "failed to register generated taxi_id=" + std::to_string(id));
    return -1;
  }

  log_info("TaxiSystem::create_taxi",
           "created taxi_id=" + std::to_string(id) + " status=offline");
  return id;
}

bool TaxiSystem::register_taxi(int id) {
  if (id < 0) {
    log_error("TaxiSystem::register_taxi",
              "rejected invalid taxi_id=" + std::to_string(id));
    return false;
  }

  if (taxi_map_.count(id)) {
    log_error("TaxiSystem::register_taxi",
              "duplicate taxi_id=" + std::to_string(id));
    return false;
  }

  taxi_map_.emplace(id, Taxi(id, 0.0, 0.0, TaxiStatus::offline));
  if (id >= next_taxi_id_) {
    next_taxi_id_ = id + 1;
  }

  log_info("TaxiSystem::register_taxi",
           "registered taxi_id=" + std::to_string(id) + " status=offline");
  return true;
}

bool TaxiSystem::set_taxi_online(int id, double x, double y) {
  Taxi *taxi = find_taxi(id);
  if (!taxi) {
    log_error("TaxiSystem::set_taxi_online",
              "taxi_id=" + std::to_string(id) + " not found");
    return false;
  }

  if (taxi->status != TaxiStatus::offline) {
    log_error("TaxiSystem::set_taxi_online",
              "taxi_id=" + std::to_string(id) +
                  " must be offline before going online");
    return false;
  }

  const double old_x = taxi->x;
  const double old_y = taxi->y;
  taxi->x = x;
  taxi->y = y;
  taxi->status = TaxiStatus::free;

  if (!upsert_into_spatial_index(*taxi, "TaxiSystem::set_taxi_online")) {
    taxi->x = old_x;
    taxi->y = old_y;
    taxi->status = TaxiStatus::offline;
    rebuild_spatial_index("TaxiSystem::set_taxi_online");
    return false;
  }

  std::ostringstream stream;
  stream << "taxi_id=" << id << " is online at (" << x << ", " << y << ")";
  log_info("TaxiSystem::set_taxi_online", stream.str());
  return true;
}

bool TaxiSystem::set_taxi_offline(int id) {
  Taxi *taxi = find_taxi(id);
  if (!taxi) {
    log_error("TaxiSystem::set_taxi_offline",
              "taxi_id=" + std::to_string(id) + " not found");
    return false;
  }

  if (taxi->status == TaxiStatus::offline) {
    return true;
  }

  if (taxi->status == TaxiStatus::occupy) {
    log_error("TaxiSystem::set_taxi_offline",
              "taxi_id=" + std::to_string(id) +
                  " is occupied and cannot go offline");
    return false;
  }

  if (taxi->status == TaxiStatus::free &&
      !remove_from_spatial_index(*taxi, "TaxiSystem::set_taxi_offline")) {
    return false;
  }

  taxi->status = TaxiStatus::offline;
  log_info("TaxiSystem::set_taxi_offline", taxi_status_message(id, taxi->status));
  return true;
}

bool TaxiSystem::update_taxi_position(int id, double x, double y) {
  Taxi *taxi = find_taxi(id);
  if (!taxi) {
    log_error("TaxiSystem::update_taxi_position",
              "taxi_id=" + std::to_string(id) + " not found");
    return false;
  }

  if (taxi->status == TaxiStatus::offline) {
    log_error("TaxiSystem::update_taxi_position",
              "taxi_id=" + std::to_string(id) +
                  " is offline and cannot move");
    return false;
  }

  const double old_x = taxi->x;
  const double old_y = taxi->y;
  taxi->x = x;
  taxi->y = y;

  if (taxi->status == TaxiStatus::free &&
      !upsert_into_spatial_index(*taxi, "TaxiSystem::update_taxi_position")) {
    taxi->x = old_x;
    taxi->y = old_y;
    rebuild_spatial_index("TaxiSystem::update_taxi_position");
    return false;
  }

  return true;
}

bool TaxiSystem::update_taxi_status(int id, TaxiStatus status) {
  Taxi *taxi = find_taxi(id);
  if (!taxi) {
    log_error("TaxiSystem::update_taxi_status",
              "taxi_id=" + std::to_string(id) + " not found");
    return false;
  }

  if (taxi->status == status) {
    return true;
  }

  if (taxi->status == TaxiStatus::offline && status == TaxiStatus::free) {
    log_error("TaxiSystem::update_taxi_status",
              "taxi_id=" + std::to_string(id) +
                  " must use set_taxi_online to become free");
    return false;
  }

  if (status == TaxiStatus::offline) {
    return set_taxi_offline(id);
  }

  if (taxi->status == TaxiStatus::occupy && status == TaxiStatus::free) {
    log_error("TaxiSystem::update_taxi_status",
              "taxi_id=" + std::to_string(id) +
                  " is occupied; use complete_trip or cancel_request");
    return false;
  }

  if (taxi->status == TaxiStatus::free &&
      !remove_from_spatial_index(*taxi, "TaxiSystem::update_taxi_status")) {
    return false;
  }

  const TaxiStatus previous_status = taxi->status;
  taxi->status = status;

  if (status == TaxiStatus::free &&
      !upsert_into_spatial_index(*taxi, "TaxiSystem::update_taxi_status")) {
    taxi->status = previous_status;
    rebuild_spatial_index("TaxiSystem::update_taxi_status");
    return false;
  }

  log_info("TaxiSystem::update_taxi_status",
           taxi_status_message(id, taxi->status));
  return true;
}

bool TaxiSystem::apply_assignment(IRequestContext &request,
                                  const Assignment &assignment) {
  if (assignment.request_id != request.request_id()) {
    log_error("TaxiSystem::apply_assignment",
              "assignment request_id=" + std::to_string(assignment.request_id) +
                  " does not match request_id=" +
                  std::to_string(request.request_id()));
    return false;
  }

  if (request.status() != RequestStatus::pending) {
    log_error("TaxiSystem::apply_assignment",
              "request_id=" + std::to_string(request.request_id()) +
                  " is not pending, status=" + to_string(request.status()));
    return false;
  }

  const int taxi_id = assignment.taxi_id;
  const Taxi *selected_taxi = find_taxi(taxi_id);
  if (!selected_taxi || selected_taxi->status != TaxiStatus::free) {
    log_error("TaxiSystem::apply_assignment",
              "taxi_id=" + std::to_string(taxi_id) +
                  " is not available for request_id=" +
                  std::to_string(request.request_id()));
    return false;
  }

  if (!update_taxi_status(taxi_id, TaxiStatus::occupy)) {
    log_error("TaxiSystem::apply_assignment",
              "failed to occupy taxi_id=" + std::to_string(taxi_id) +
                  " for request_id=" + std::to_string(request.request_id()));
    return false;
  }

  if (!request.assign_taxi(taxi_id)) {
    log_error("TaxiSystem::apply_assignment",
              "request_id=" + std::to_string(request.request_id()) +
                  " failed to bind taxi_id=" + std::to_string(taxi_id));
    release_occupied_taxi(taxi_id, "TaxiSystem::apply_assignment");
    return false;
  }

  std::ostringstream stream;
  stream << "request_id=" << request.request_id()
         << " assigned taxi_id=" << taxi_id
         << " pickup_cost=" << assignment.pickup_cost;
  log_info("TaxiSystem::apply_assignment", stream.str());
  return true;
}

std::optional<int> TaxiSystem::dispatch_nearest(IRequestContext &request,
                                                double radius) {
  if (request.status() != RequestStatus::pending) {
    log_error("TaxiSystem::dispatch_nearest",
              "request_id=" + std::to_string(request.request_id()) +
                  " is not pending, status=" + to_string(request.status()));
    return std::nullopt;
  }

  const int customer_id = request.customer_id();
  const double x = request.start_location().coords[0];
  const double y = request.start_location().coords[1];

  if (radius < 0.0) {
    log_error("TaxiSystem::dispatch_nearest",
              "customer_id=" + std::to_string(customer_id) +
                  " provided negative radius=" + std::to_string(radius));
    return std::nullopt;
  }

  const Point customer_location(x, y, customer_id);
  const std::optional<int> taxi_id = dispatch_strategy_->select_taxi(
      customer_location, radius, taxi_map_, *spatial_index_);

  if (!taxi_id) {
    log_error("TaxiSystem::dispatch_nearest",
              "customer_id=" + std::to_string(customer_id) +
                  " has no available taxi");
    return std::nullopt;
  }

  if (!apply_assignment(request,
                        Assignment(*taxi_id, request.request_id(), 0))) {
    return std::nullopt;
  }

  std::ostringstream stream;
  stream << "request_id=" << request.request_id() << " customer_id="
         << customer_id << " assigned taxi_id=" << *taxi_id
         << " at (" << x << ", " << y << ")";
  log_info("TaxiSystem::dispatch_nearest", stream.str());
  return taxi_id;
}

std::optional<int> TaxiSystem::dispatch_nearest(int customer_id, double x,
                                                double y, double radius) {
  RequestContext request(customer_id, customer_id, Point(x, y, customer_id),
                         Point(x, y, customer_id));
  return dispatch_nearest(request, radius);
}

bool TaxiSystem::start_trip(IRequestContext &request) {
  const std::optional<int> taxi_id = request.taxi_id();
  if (!taxi_id) {
    log_error("TaxiSystem::start_trip",
              "request_id=" + std::to_string(request.request_id()) +
                  " has no assigned taxi");
    return false;
  }

  const Taxi *taxi = find_taxi(*taxi_id);
  if (!taxi || taxi->status != TaxiStatus::occupy) {
    log_error("TaxiSystem::start_trip",
              "taxi_id=" + std::to_string(*taxi_id) +
                  " is not occupied for request_id=" +
                  std::to_string(request.request_id()));
    return false;
  }

  if (!request.start_trip()) {
    log_error("TaxiSystem::start_trip",
              "request_id=" + std::to_string(request.request_id()) +
                  " cannot start from status=" + to_string(request.status()));
    return false;
  }

  log_info("TaxiSystem::start_trip",
           "request_id=" + std::to_string(request.request_id()) +
               " status=" + to_string(request.status()));
  return true;
}

bool TaxiSystem::complete_trip(IRequestContext &request) {
  const std::optional<int> taxi_id = request.taxi_id();
  if (!taxi_id) {
    log_error("TaxiSystem::complete_trip",
              "request_id=" + std::to_string(request.request_id()) +
                  " has no assigned taxi");
    return false;
  }

  const Taxi *taxi = find_taxi(*taxi_id);
  if (!taxi || taxi->status != TaxiStatus::occupy) {
    log_error("TaxiSystem::complete_trip",
              "taxi_id=" + std::to_string(*taxi_id) +
                  " is not occupied for request_id=" +
                  std::to_string(request.request_id()));
    return false;
  }

  if (!request.complete_request()) {
    log_error("TaxiSystem::complete_trip",
              "request_id=" + std::to_string(request.request_id()) +
                  " cannot complete from status=" + to_string(request.status()));
    return false;
  }

  if (!release_occupied_taxi(*taxi_id, "TaxiSystem::complete_trip")) {
    return false;
  }

  log_info("TaxiSystem::complete_trip",
           "request_id=" + std::to_string(request.request_id()) +
               " completed with taxi_id=" + std::to_string(*taxi_id));
  return true;
}

bool TaxiSystem::cancel_request(IRequestContext &request) {
  const std::optional<int> taxi_id = request.taxi_id();
  if (!request.cancel_request()) {
    log_error("TaxiSystem::cancel_request",
              "request_id=" + std::to_string(request.request_id()) +
                  " cannot cancel from status=" + to_string(request.status()));
    return false;
  }

  if (taxi_id) {
    const Taxi *taxi = find_taxi(*taxi_id);
    if (taxi && taxi->status == TaxiStatus::occupy &&
        !release_occupied_taxi(*taxi_id, "TaxiSystem::cancel_request")) {
      return false;
    }
  }

  log_info("TaxiSystem::cancel_request",
           "request_id=" + std::to_string(request.request_id()) +
               " status=" + to_string(request.status()));
  return true;
}

Taxi *TaxiSystem::find_taxi(int id) {
  const auto it = taxi_map_.find(id);
  if (it == taxi_map_.end()) {
    return nullptr;
  }
  return &it->second;
}

std::vector<Point> TaxiSystem::collect_free_taxi_points() const {
  std::vector<Point> points;
  points.reserve(taxi_map_.size());
  for (const auto &[id, taxi] : taxi_map_) {
    (void)id;
    if (taxi.status == TaxiStatus::free) {
      points.push_back(make_point(taxi));
    }
  }
  return points;
}

bool TaxiSystem::rebuild_spatial_index(const char *operation) {
  const std::vector<Point> free_taxis = collect_free_taxi_points();
  spatial_index_->rebuild(free_taxis);
  if (spatial_index_->size() != free_taxis.size()) {
    std::ostringstream stream;
    stream << "rebuild verification failed expected=" << free_taxis.size()
           << " actual=" << spatial_index_->size();
    log_error(operation, stream.str());
    return false;
  }

  std::ostringstream stream;
  stream << "rebuilt spatial index free_taxi_count=" << free_taxis.size();
  log_info(operation, stream.str());
  return true;
}

bool TaxiSystem::remove_from_spatial_index(const Taxi &taxi,
                                           const char *operation) {
  if (spatial_index_->erase(taxi.id)) {
    return true;
  }

  log_error(operation,
            "taxi_id=" + std::to_string(taxi.id) +
                " missing from spatial index, triggering rebuild");
  if (!rebuild_spatial_index(operation)) {
    return false;
  }

  if (!spatial_index_->erase(taxi.id)) {
    log_error(operation, "taxi_id=" + std::to_string(taxi.id) +
                             " still missing after rebuild");
    return false;
  }
  return true;
}

bool TaxiSystem::upsert_into_spatial_index(const Taxi &taxi,
                                           const char *operation) {
  if (spatial_index_->upsert(make_point(taxi))) {
    return true;
  }

  log_error(operation,
            "failed to sync taxi_id=" + std::to_string(taxi.id) +
                " into spatial index, triggering rebuild");
  return rebuild_spatial_index(operation);
}

bool TaxiSystem::release_occupied_taxi(int id, const char *operation) {
  Taxi *taxi = find_taxi(id);
  if (!taxi) {
    log_error(operation, "taxi_id=" + std::to_string(id) + " not found");
    return false;
  }

  if (taxi->status != TaxiStatus::occupy) {
    log_error(operation,
              "taxi_id=" + std::to_string(id) +
                  " is not occupied, status=" + to_string(taxi->status));
    return false;
  }

  taxi->status = TaxiStatus::free;
  if (!upsert_into_spatial_index(*taxi, operation)) {
    taxi->status = TaxiStatus::occupy;
    return false;
  }

  log_info(operation, taxi_status_message(id, taxi->status));
  return true;
}
