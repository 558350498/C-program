#pragma once

#include "taxi_domain.h"

#include <optional>

enum class RequestStatus { pending, dispatched, serving, completed, canceled };

class IRequestContext {
public:
    virtual ~IRequestContext() = default;

    virtual int request_id() const = 0;
    virtual int customer_id() const = 0;
    virtual std::optional<int> taxi_id() const = 0;
    virtual RequestStatus status() const = 0;
    virtual const Point &start_location() const = 0;
    virtual const Point &end_location() const = 0;

    virtual bool assign_taxi(int taxi_id) = 0;
    virtual bool start_trip() = 0;
    virtual bool complete_request() = 0;
    virtual bool cancel_request() = 0;
};

class RequestContext final : public IRequestContext {
private:
    int request_id_;
    int customer_id_;
    std::optional<int> taxi_id_;
    RequestStatus status_;
    Point start_location_;
    Point end_location_;

public:
    RequestContext(int request_id, int customer_id, const Point &start_location,
                   const Point &end_location);

    int request_id() const override;
    int customer_id() const override;
    std::optional<int> taxi_id() const override;
    RequestStatus status() const override;
    const Point &start_location() const override;
    const Point &end_location() const override;

    bool assign_taxi(int taxi_id) override;
    bool start_trip() override;
    bool complete_request() override;
    bool cancel_request() override;
};

const char *to_string(RequestStatus status);
