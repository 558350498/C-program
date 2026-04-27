#include "requestcontext.h"

#include <cassert>

int main() {
  RequestContext request(1, 10, Point(0.0, 0.0, 10),
                         Point(5.0, 5.0, 10));

  assert(request.request_id() == 1);
  assert(request.customer_id() == 10);
  assert(!request.taxi_id().has_value());
  assert(request.status() == RequestStatus::pending);
  assert(!request.start_trip());
  assert(!request.complete_request());
  assert(!request.assign_taxi(-1));

  assert(request.assign_taxi(7));
  assert(request.taxi_id().has_value());
  assert(*request.taxi_id() == 7);
  assert(request.status() == RequestStatus::dispatched);
  assert(!request.assign_taxi(8));

  assert(request.start_trip());
  assert(request.status() == RequestStatus::serving);
  assert(!request.start_trip());

  assert(request.complete_request());
  assert(request.status() == RequestStatus::completed);
  assert(!request.taxi_id().has_value());
  assert(!request.cancel_request());
  assert(!request.assign_taxi(9));

  RequestContext canceled(2, 20, Point(1.0, 1.0, 20),
                          Point(2.0, 2.0, 20));
  assert(canceled.cancel_request());
  assert(canceled.status() == RequestStatus::canceled);
  assert(!canceled.cancel_request());
  assert(!canceled.assign_taxi(3));
  assert(!canceled.start_trip());
  assert(!canceled.complete_request());

  RequestContext dispatched_cancel(3, 30, Point(3.0, 3.0, 30),
                                   Point(4.0, 4.0, 30));
  assert(dispatched_cancel.assign_taxi(11));
  assert(dispatched_cancel.cancel_request());
  assert(dispatched_cancel.status() == RequestStatus::canceled);
  assert(!dispatched_cancel.taxi_id().has_value());

  assert(to_string(RequestStatus::pending) != nullptr);
  assert(to_string(RequestStatus::dispatched) != nullptr);
  assert(to_string(RequestStatus::serving) != nullptr);
  assert(to_string(RequestStatus::completed) != nullptr);
  assert(to_string(RequestStatus::canceled) != nullptr);

  return 0;
}
