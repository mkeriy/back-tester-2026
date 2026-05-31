#pragma once

#include "common/BasicTypes.hpp"

#include <string>
#include <utility>
#include <vector>

namespace cmf
{

class OrderAPI; // forward declaration

// Base class for user-defined trading strategies.
// Override the on_* callbacks in Python (or C++).
// BacktestEngine sets _api before calling run().
class Strategy
{
  public:
    virtual ~Strategy() = default;

    // Called when the order book changes for an instrument.
    // bids/asks: top-N levels as (price_float, quantity) pairs,
    // bids sorted best (highest) first, asks sorted best (lowest) first.
    virtual void on_book_update(uint32_t instrument_id, NanoTime timestamp_ns,
                                const std::vector<std::pair<double, int>>& bids,
                                const std::vector<std::pair<double, int>>& asks)
    {
        (void)instrument_id;
        (void)timestamp_ns;
        (void)bids;
        (void)asks;
    }

    // Called on every aggressing or resting fill event (Trade / Fill action).
    virtual void on_trade(uint32_t instrument_id, NanoTime timestamp_ns,
                          double price, int size, Side side)
    {
        (void)instrument_id;
        (void)timestamp_ns;
        (void)price;
        (void)size;
        (void)side;
    }

    // Called when one of the strategy's own orders is filled.
    virtual void on_fill(OrderId order_id, uint32_t instrument_id,
                         NanoTime timestamp_ns, double price, int size, Side side)
    {
        (void)order_id;
        (void)instrument_id;
        (void)timestamp_ns;
        (void)price;
        (void)size;
        (void)side;
    }

    // Called when one of the strategy's own orders is rejected.
    virtual void on_reject(OrderId order_id, uint32_t instrument_id,
                           const std::string& reason)
    {
        (void)order_id;
        (void)instrument_id;
        (void)reason;
    }

    // Wired by BacktestEngine before run(); do not set manually.
    OrderAPI* _api{nullptr};
};

} // namespace cmf
