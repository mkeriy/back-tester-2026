#pragma once

#include "common/BasicTypes.hpp"
#include "common/MarketDataEvent.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cmf
{

enum class OrderStatus : uint8_t
{
    Active,
    Filled,
    Cancelled,
    Rejected,
};

struct Order
{
    OrderId         order_id{};
    uint16_t        trading_engine_id{};
    uint32_t        instrument_id{};
    Side            side{Side::None};
    ScaledPrice     price{UNDEF_PRICE}; // UNDEF_PRICE for market orders
    int             size{};
    OrderType       order_type{OrderType::None};
    TimeInForce     tif{TimeInForce::None};
    OrderStatus     status{OrderStatus::Active};
    NanoTime        sent_at{};
    NanoTime        filled_at{};
    double          filled_price{};
    int             filled_size{};
    std::string     reject_reason;
};

// Mock order placement API (Group 1 substitute for backtesting).
// Fill simulation is driven by BacktestEngine via try_fill().
class OrderAPI
{
  public:
    explicit OrderAPI(uint16_t trading_engine_id = 1);

    OrderId send_limit_order(uint32_t instrument_id, Side side, double price_float,
                             int size,
                             TimeInForce tif = TimeInForce::GoodTillCancel);
    OrderId send_market_order(uint32_t instrument_id, Side side, int size);
    bool    cancel_order(OrderId order_id);

    [[nodiscard]] const Order*       get_order(OrderId order_id) const;
    [[nodiscard]] std::vector<Order> open_orders() const;
    [[nodiscard]] const std::unordered_map<OrderId, Order>& all_orders() const
    {
        return orders_;
    }

    struct FillResult
    {
        OrderId order_id;
        double  fill_price;
        int     fill_size;
    };

    struct RejectResult
    {
        OrderId     order_id;
        std::string reason;
    };

    struct TryFillResult
    {
        std::vector<FillResult>   fills;
        std::vector<RejectResult> rejects;
    };

    // Called by BacktestEngine after routing each market event.
    // Checks all active orders for this instrument against current book prices.
    TryFillResult try_fill(uint32_t instrument_id,
                           const std::optional<ScaledPrice>& best_bid,
                           const std::optional<ScaledPrice>& best_ask, NanoTime ts);

    struct Stats
    {
        int sent{};
        int cancelled{};
        int filled{};
        int rejected{};
    };

    [[nodiscard]] Stats total_stats() const { return total_; }
    [[nodiscard]] Stats stats_by_instrument(uint32_t instrument_id) const;

  private:
    uint16_t                                     engine_id_;
    OrderId                                      next_id_{1};
    std::unordered_map<OrderId, Order>           orders_;
    std::unordered_map<uint32_t, Stats>          per_instrument_;
    Stats                                        total_{};
};

} // namespace cmf
