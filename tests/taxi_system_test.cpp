#include "kd_tree_spatial_index.h"
#include "nearest_free_taxi_strategy.h"
#include "taxi_system.h"

#include <cassert>
#include <memory>
#include <optional>
#include <unordered_map>

namespace {

class RejectAssignRequest : public IRequestContext {
public:
  RejectAssignRequest(int request_id, int customer_id, const Point &start)
      : request_id_(request_id), customer_id_(customer_id), start_(start),
        end_(start) {}

  int request_id() const override { return request_id_; }
  int customer_id() const override { return customer_id_; }
  std::optional<int> taxi_id() const override { return std::nullopt; }
  RequestStatus status() const override { return RequestStatus::pending; }
  const Point &start_location() const override { return start_; }
  const Point &end_location() const override { return end_; }
  bool assign_taxi(int taxi_id) override {
    (void)taxi_id;
    return false;
  }
  bool start_trip() override { return false; }
  bool complete_request() override { return false; }
  bool cancel_request() override { return false; }

private:
  int request_id_;
  int customer_id_;
  Point start_;
  Point end_;
};

class TerminalCancelRequest : public IRequestContext {
public:
  TerminalCancelRequest(int request_id, int customer_id, int taxi_id)
      : request_id_(request_id), customer_id_(customer_id), taxi_id_(taxi_id),
        start_(Point(0.0, 0.0, customer_id)),
        end_(Point(1.0, 1.0, customer_id)) {}

  int request_id() const override { return request_id_; }
  int customer_id() const override { return customer_id_; }
  std::optional<int> taxi_id() const override { return taxi_id_; }
  RequestStatus status() const override { return RequestStatus::completed; }
  const Point &start_location() const override { return start_; }
  const Point &end_location() const override { return end_; }
  bool assign_taxi(int taxi_id) override {
    (void)taxi_id;
    return false;
  }
  bool start_trip() override { return false; }
  bool complete_request() override { return false; }
  bool cancel_request() override { return false; }

private:
  int request_id_;
  int customer_id_;
  int taxi_id_;
  Point start_;
  Point end_;
};

class FixedDispatchStrategy : public IDispatchStrategy {
public:
  explicit FixedDispatchStrategy(int taxi_id) : taxi_id_(taxi_id) {}

  std::optional<int>
  select_taxi(const Point &customer_location, double radius,
              const std::unordered_map<int, Taxi> &taxi_map,
              ISpatialIndex &spatial_index) override {
    (void)customer_location;
    (void)radius;
    (void)taxi_map;
    (void)spatial_index;
    return taxi_id_;
  }

private:
  int taxi_id_;
};

} // namespace

int main() {
  TaxiSystem system;
  assert(system.logging_enabled());
  system.set_logging_enabled(false);
  assert(!system.logging_enabled());
  system.set_logging_enabled(true);

  const int first = system.create_taxi();
  const int second = system.create_taxi();
  assert(first == 0);
  assert(second == 1);

  assert(!system.register_taxi(first));
  assert(!system.register_taxi(-1));
  assert(system.register_taxi(10));

  assert(system.set_taxi_online(first, 0.0, 0.0));
  assert(system.set_taxi_online(second, 5.0, 0.0));
  assert(!system.set_taxi_online(999, 0.0, 0.0));

  RequestContext first_request(100, 100, Point(0.5, 0.0, 100),
                               Point(3.0, 0.0, 100));
  auto assigned = system.dispatch_nearest(first_request, 10.0);
  assert(assigned.has_value());
  assert(*assigned == first);
  assert(first_request.taxi_id().has_value());
  assert(*first_request.taxi_id() == first);
  assert(first_request.status() == RequestStatus::dispatched);
  assert(!system.set_taxi_offline(first));
  assert(!system.update_taxi_status(first, TaxiStatus::free));
  assert(system.start_trip(first_request));
  assert(first_request.status() == RequestStatus::serving);
  assert(system.complete_trip(first_request));
  assert(first_request.status() == RequestStatus::completed);
  assert(!system.complete_trip(first_request));
  assert(!system.cancel_request(first_request));
  assert(!system.start_trip(first_request));

  RequestContext applied_request(108, 108, Point(1.0, 0.0, 108),
                                 Point(4.0, 0.0, 108));
  assert(system.apply_assignment(applied_request, Assignment(first, 108, 12)));
  assert(applied_request.taxi_id().has_value());
  assert(*applied_request.taxi_id() == first);
  assert(applied_request.status() == RequestStatus::dispatched);
  assert(!system.set_taxi_offline(first));
  assert(system.complete_trip(applied_request));

  RequestContext mismatch_request(109, 109, Point(1.0, 0.0, 109),
                                  Point(4.0, 0.0, 109));
  assert(!system.apply_assignment(mismatch_request, Assignment(first, 999, 12)));
  assert(mismatch_request.status() == RequestStatus::pending);
  assert(!mismatch_request.taxi_id().has_value());

  assert(!system.apply_assignment(mismatch_request, Assignment(999, 109, 12)));
  assert(mismatch_request.status() == RequestStatus::pending);
  assert(!mismatch_request.taxi_id().has_value());

  RequestContext occupied_request(110, 110, Point(1.0, 0.0, 110),
                                  Point(4.0, 0.0, 110));
  assert(system.apply_assignment(occupied_request, Assignment(first, 110, 12)));
  RequestContext blocked_request(111, 111, Point(1.0, 0.0, 111),
                                 Point(4.0, 0.0, 111));
  assert(!system.apply_assignment(blocked_request, Assignment(first, 111, 12)));
  assert(blocked_request.status() == RequestStatus::pending);
  assert(!blocked_request.taxi_id().has_value());
  assert(system.complete_trip(occupied_request));

  RequestContext canceled_apply_request(112, 112, Point(1.0, 0.0, 112),
                                        Point(4.0, 0.0, 112));
  assert(canceled_apply_request.cancel_request());
  assert(!system.apply_assignment(canceled_apply_request,
                                  Assignment(first, 112, 12)));
  assert(canceled_apply_request.status() == RequestStatus::canceled);

  RejectAssignRequest bad_apply_request(113, 113, Point(1.0, 0.0, 113));
  assert(!system.apply_assignment(bad_apply_request, Assignment(first, 113, 12)));
  RequestContext after_bad_apply_request(114, 114, Point(1.0, 0.0, 114),
                                         Point(4.0, 0.0, 114));
  assert(system.apply_assignment(after_bad_apply_request,
                                 Assignment(first, 114, 12)));
  assert(system.complete_trip(after_bad_apply_request));

  assert(!system.dispatch_nearest(101, 0.5, 0.0, -1.0).has_value());

  assert(system.update_taxi_position(first, 1.0, 0.0));

  RequestContext canceled_before_dispatch(101, 101, Point(0.6, 0.0, 101),
                                          Point(4.0, 0.0, 101));
  assert(canceled_before_dispatch.cancel_request());
  assert(!system.dispatch_nearest(canceled_before_dispatch, 2.0).has_value());
  assert(canceled_before_dispatch.status() == RequestStatus::canceled);
  assert(!canceled_before_dispatch.taxi_id().has_value());

  RequestContext second_request(102, 102, Point(0.8, 0.0, 102),
                                Point(4.0, 0.0, 102));
  assigned = system.dispatch_nearest(second_request, 2.0);
  assert(assigned.has_value());
  assert(*assigned == first);
  assert(system.cancel_request(second_request));
  assert(second_request.status() == RequestStatus::canceled);

  RejectAssignRequest bad_request(103, 103, Point(0.9, 0.0, 103));
  assert(!system.dispatch_nearest(bad_request, 2.0).has_value());

  RequestContext after_bad_request(104, 104, Point(0.9, 0.0, 104),
                                   Point(5.0, 0.0, 104));
  assigned = system.dispatch_nearest(after_bad_request, 2.0);
  assert(assigned.has_value());
  assert(*assigned == first);
  TerminalCancelRequest terminal_cancel(900, 900, first);
  assert(!system.cancel_request(terminal_cancel));
  assert(!system.set_taxi_offline(first));
  assert(system.complete_trip(after_bad_request));

  assert(system.set_taxi_offline(second));
  assert(!system.update_taxi_position(second, 6.0, 0.0));
  assert(!system.update_taxi_status(second, TaxiStatus::free));

  RequestContext no_taxi_request(105, 105, Point(100.0, 100.0, 105),
                                 Point(101.0, 101.0, 105));
  assert(!system.dispatch_nearest(no_taxi_request, 0.1).has_value());
  assert(no_taxi_request.status() == RequestStatus::pending);
  assert(!no_taxi_request.taxi_id().has_value());

  auto missing_strategy = std::make_unique<FixedDispatchStrategy>(404);
  TaxiSystem missing_strategy_system(std::make_unique<KdTreeSpatialIndex>(),
                                     std::move(missing_strategy));
  const int missing_strategy_taxi = missing_strategy_system.create_taxi();
  assert(missing_strategy_system.set_taxi_online(missing_strategy_taxi, 0.0,
                                                 0.0));
  RequestContext missing_strategy_request(106, 106, Point(0.0, 0.0, 106),
                                          Point(1.0, 1.0, 106));
  assert(!missing_strategy_system.dispatch_nearest(missing_strategy_request,
                                                   5.0)
              .has_value());
  assert(missing_strategy_request.status() == RequestStatus::pending);
  assert(!missing_strategy_request.taxi_id().has_value());

  auto offline_strategy = std::make_unique<FixedDispatchStrategy>(3);
  TaxiSystem offline_strategy_system(std::make_unique<KdTreeSpatialIndex>(),
                                     std::move(offline_strategy));
  assert(offline_strategy_system.register_taxi(3));
  RequestContext offline_strategy_request(107, 107, Point(0.0, 0.0, 107),
                                          Point(1.0, 1.0, 107));
  assert(!offline_strategy_system.dispatch_nearest(offline_strategy_request,
                                                   5.0)
              .has_value());
  assert(offline_strategy_request.status() == RequestStatus::pending);

  KdTreeSpatialIndex stale_index;
  assert(stale_index.upsert(Point(0.15, 0.0, 999)));
  assert(stale_index.upsert(Point(0.0, 0.0, 1)));

  std::unordered_map<int, Taxi> taxi_map;
  taxi_map.emplace(1, Taxi(1, 0.0, 0.0, TaxiStatus::free));

  NearestFreeTaxiStrategy strategy;
  auto picked =
      strategy.select_taxi(Point(0.1, 0.0, 200), 1.0, taxi_map, stale_index);
  assert(picked.has_value());
  assert(*picked == 1);
  assert(!stale_index.erase(999));

  KdTreeSpatialIndex occupied_stale_index;
  assert(occupied_stale_index.upsert(Point(0.0, 0.0, 1)));
  assert(occupied_stale_index.upsert(Point(0.2, 0.0, 2)));

  std::unordered_map<int, Taxi> mixed_status_map;
  mixed_status_map.emplace(1, Taxi(1, 0.0, 0.0, TaxiStatus::occupy));
  mixed_status_map.emplace(2, Taxi(2, 0.2, 0.0, TaxiStatus::free));

  picked = strategy.select_taxi(Point(0.0, 0.0, 201), 1.0, mixed_status_map,
                                occupied_stale_index);
  assert(picked.has_value());
  assert(*picked == 2);
  assert(!occupied_stale_index.erase(1));

  return 0;
}
