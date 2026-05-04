#include "requestcontext.h"

RequestContext::RequestContext(int request_id, int customer_id,
                               const Point &start_location,
                               const Point &end_location)
    : request_id_(request_id), customer_id_(customer_id), taxi_id_(std::nullopt),
      status_(RequestStatus::pending), start_location_(start_location),
      end_location_(end_location) {}

int RequestContext::request_id() const { return request_id_; }

int RequestContext::customer_id() const { return customer_id_; }

std::optional<int> RequestContext::taxi_id() const { return taxi_id_; }

RequestStatus RequestContext::status() const { return status_; }

const Point &RequestContext::start_location() const { return start_location_; }

const Point &RequestContext::end_location() const { return end_location_; }

bool RequestContext::assign_taxi(int taxi_id) {
  if (taxi_id < 0 || status_ != RequestStatus::pending || taxi_id_.has_value()) {
    return false;
  }

  taxi_id_ = taxi_id;
  status_ = RequestStatus::dispatched;
  return true;
}

bool RequestContext::start_trip() {
  if (status_ != RequestStatus::dispatched || !taxi_id_.has_value()) {
    return false;
  }

  status_ = RequestStatus::serving;
  return true;
}

bool RequestContext::complete_request() {
  if ((status_ != RequestStatus::dispatched &&
       status_ != RequestStatus::serving) ||
      !taxi_id_.has_value()) {
    return false;
  }

  taxi_id_.reset();
  status_ = RequestStatus::completed;
  return true;
}

bool RequestContext::cancel_request() {
  if (status_ == RequestStatus::completed ||
      status_ == RequestStatus::canceled) {
    return false;
  }

  taxi_id_.reset();
  status_ = RequestStatus::canceled;
  return true;
}

const char *to_string(RequestStatus status) {
  switch (status) {
  case RequestStatus::pending:
    return "pending";
  case RequestStatus::dispatched:
    return "dispatched";
  case RequestStatus::serving:
    return "serving";
  case RequestStatus::completed:
    return "completed";
  case RequestStatus::canceled:
    return "canceled";
  default:
    return "unknown";
  }
}
