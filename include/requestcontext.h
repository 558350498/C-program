#pragma once

#include "taxi_system.h"
#include <utility>

class RequestContext {
private:
    std::pair<int, int> id; // <customer_id, taxi_id>
    Point s_location;
    Point f_location;
public:
    virtual ~RequestContext() = default;
    int customer_id() {}
    int taxi_id() {} 
    Point start_location() {}
    Point end_location() {}
};