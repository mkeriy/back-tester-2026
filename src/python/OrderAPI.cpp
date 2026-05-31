#include "python/OrderAPI.hpp"

#include <algorithm>
#include <stdexcept>

namespace cmf
{

OrderAPI::OrderAPI(uint16_t trading_engine_id) : engine_id_(trading_engine_id) {}

OrderId OrderAPI::send_limit_order(uint32_t instrument_id, Side side,
                                    double price_float, int size, TimeInForce tif)
{
    OrderId id = next_id_++;
    Order&  o  = orders_[id];
    o.order_id          = id;
    o.trading_engine_id = engine_id_;
    o.instrument_id     = instrument_id;
    o.side              = side;
    o.price             = static_cast<ScaledPrice>(price_float * 1e9);
    o.size              = size;
    o.order_type        = OrderType::Limit;
    o.tif               = tif;
    o.status            = OrderStatus::Active;
    total_.sent++;
    per_instrument_[instrument_id].sent++;
    return id;
}

OrderId OrderAPI::send_market_order(uint32_t instrument_id, Side side, int size)
{
    OrderId id = next_id_++;
    Order&  o  = orders_[id];
    o.order_id          = id;
    o.trading_engine_id = engine_id_;
    o.instrument_id     = instrument_id;
    o.side              = side;
    o.price             = UNDEF_PRICE;
    o.size              = size;
    o.order_type        = OrderType::Market;
    o.tif               = TimeInForce::FillAndKill;
    o.status            = OrderStatus::Active;
    total_.sent++;
    per_instrument_[instrument_id].sent++;
    return id;
}

bool OrderAPI::cancel_order(OrderId order_id)
{
    auto it = orders_.find(order_id);
    if (it == orders_.end() || it->second.status != OrderStatus::Active)
        return false;
    it->second.status = OrderStatus::Cancelled;
    total_.cancelled++;
    per_instrument_[it->second.instrument_id].cancelled++;
    return true;
}

const Order* OrderAPI::get_order(OrderId order_id) const
{
    auto it = orders_.find(order_id);
    return (it != orders_.end()) ? &it->second : nullptr;
}

std::vector<Order> OrderAPI::open_orders() const
{
    std::vector<Order> result;
    for (const auto& [id, o] : orders_)
        if (o.status == OrderStatus::Active)
            result.push_back(o);
    return result;
}

OrderAPI::TryFillResult OrderAPI::try_fill(uint32_t instrument_id,
                                            const std::optional<ScaledPrice>& best_bid,
                                            const std::optional<ScaledPrice>& best_ask,
                                            NanoTime ts)
{
    TryFillResult result;
    for (auto& [id, o] : orders_)
    {
        if (o.status != OrderStatus::Active || o.instrument_id != instrument_id)
            continue;

        bool   fill       = false;
        double fill_price = 0.0;

        if (o.order_type == OrderType::Market)
        {
            if (o.side == Side::Buy && best_ask)
            {
                fill_price = static_cast<double>(*best_ask) * 1e-9;
                fill       = true;
            }
            else if (o.side == Side::Sell && best_bid)
            {
                fill_price = static_cast<double>(*best_bid) * 1e-9;
                fill       = true;
            }
            else
            {
                o.status        = OrderStatus::Rejected;
                o.reject_reason = "no liquidity for market order";
                result.rejects.push_back({id, o.reject_reason});
                total_.rejected++;
                per_instrument_[instrument_id].rejected++;
                continue;
            }
        }
        else if (o.order_type == OrderType::Limit)
        {
            if (o.side == Side::Buy && best_ask && o.price >= *best_ask)
            {
                fill_price = static_cast<double>(*best_ask) * 1e-9;
                fill       = true;
            }
            else if (o.side == Side::Sell && best_bid && o.price <= *best_bid)
            {
                fill_price = static_cast<double>(*best_bid) * 1e-9;
                fill       = true;
            }
        }

        if (fill)
        {
            o.status       = OrderStatus::Filled;
            o.filled_at    = ts;
            o.filled_price = fill_price;
            o.filled_size  = o.size;
            result.fills.push_back({id, fill_price, o.size});
            total_.filled++;
            per_instrument_[instrument_id].filled++;
        }
    }
    return result;
}

OrderAPI::Stats OrderAPI::stats_by_instrument(uint32_t instrument_id) const
{
    auto it = per_instrument_.find(instrument_id);
    return (it != per_instrument_.end()) ? it->second : Stats{};
}

} // namespace cmf
