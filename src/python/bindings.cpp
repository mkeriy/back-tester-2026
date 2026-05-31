#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "python/BacktestEngine.hpp"
#include "python/OrderAPI.hpp"
#include "python/Strategy.hpp"

namespace py = pybind11;
using namespace cmf;

// ---------------------------------------------------------------------------
// PyStrategy trampoline — lets Python subclass Strategy
// ---------------------------------------------------------------------------
struct PyStrategy : Strategy
{
    using Strategy::Strategy;

    void on_book_update(uint32_t instrument_id, NanoTime ts,
                        const std::vector<std::pair<double, int>>& bids,
                        const std::vector<std::pair<double, int>>& asks) override
    {
        PYBIND11_OVERRIDE(void, Strategy, on_book_update, instrument_id, ts, bids, asks);
    }

    void on_trade(uint32_t instrument_id, NanoTime ts, double price, int size,
                  Side side) override
    {
        PYBIND11_OVERRIDE(void, Strategy, on_trade, instrument_id, ts, price, size, side);
    }

    void on_fill(OrderId order_id, uint32_t instrument_id, NanoTime ts, double price,
                 int size, Side side) override
    {
        PYBIND11_OVERRIDE(void, Strategy, on_fill, order_id, instrument_id, ts, price,
                          size, side);
    }

    void on_reject(OrderId order_id, uint32_t instrument_id,
                   const std::string& reason) override
    {
        PYBIND11_OVERRIDE(void, Strategy, on_reject, order_id, instrument_id, reason);
    }
};

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------
PYBIND11_MODULE(backtester_cpp, m)
{
    m.doc() = "C++ backtesting engine Python bindings";

    // ------------------------------------------------------------------
    // Enums
    // ------------------------------------------------------------------
    py::enum_<Side>(m, "Side")
        .value("BUY",  Side::Buy)
        .value("SELL", Side::Sell)
        .value("NONE", Side::None)
        .export_values();

    py::enum_<OrderType>(m, "OrderType")
        .value("LIMIT",  OrderType::Limit)
        .value("MARKET", OrderType::Market)
        .value("NONE",   OrderType::None)
        .export_values();

    py::enum_<TimeInForce>(m, "TimeInForce")
        .value("GTC",  TimeInForce::GoodTillCancel)
        .value("FAK",  TimeInForce::FillAndKill)
        .value("FOK",  TimeInForce::FillOrKill)
        .value("NONE", TimeInForce::None)
        .export_values();

    py::enum_<OrderStatus>(m, "OrderStatus")
        .value("ACTIVE",    OrderStatus::Active)
        .value("FILLED",    OrderStatus::Filled)
        .value("CANCELLED", OrderStatus::Cancelled)
        .value("REJECTED",  OrderStatus::Rejected)
        .export_values();

    // ------------------------------------------------------------------
    // Order
    // ------------------------------------------------------------------
    py::class_<Order>(m, "Order")
        .def_readonly("order_id",           &Order::order_id)
        .def_readonly("trading_engine_id",  &Order::trading_engine_id)
        .def_readonly("instrument_id",      &Order::instrument_id)
        .def_readonly("side",               &Order::side)
        .def_readonly("price",              &Order::price)
        .def_readonly("size",               &Order::size)
        .def_readonly("order_type",         &Order::order_type)
        .def_readonly("tif",                &Order::tif)
        .def_readonly("status",             &Order::status)
        .def_readonly("sent_at",            &Order::sent_at)
        .def_readonly("filled_at",          &Order::filled_at)
        .def_readonly("filled_price",       &Order::filled_price)
        .def_readonly("filled_size",        &Order::filled_size)
        .def_readonly("reject_reason",      &Order::reject_reason)
        .def("price_as_float", [](const Order& o)
             { return static_cast<double>(o.price) * 1e-9; });

    // ------------------------------------------------------------------
    // OrderAPI::Stats
    // ------------------------------------------------------------------
    py::class_<OrderAPI::Stats>(m, "OrderStats")
        .def_readonly("sent",      &OrderAPI::Stats::sent)
        .def_readonly("cancelled", &OrderAPI::Stats::cancelled)
        .def_readonly("filled",    &OrderAPI::Stats::filled)
        .def_readonly("rejected",  &OrderAPI::Stats::rejected);

    // ------------------------------------------------------------------
    // OrderAPI
    // ------------------------------------------------------------------
    py::class_<OrderAPI>(m, "OrderAPI")
        .def("send_limit_order", &OrderAPI::send_limit_order,
             py::arg("instrument_id"), py::arg("side"), py::arg("price"),
             py::arg("size"), py::arg("tif") = TimeInForce::GoodTillCancel,
             "Submit a limit order. Returns the assigned order_id.")
        .def("send_market_order", &OrderAPI::send_market_order,
             py::arg("instrument_id"), py::arg("side"), py::arg("size"),
             "Submit a market order. Returns the assigned order_id.")
        .def("cancel_order", &OrderAPI::cancel_order, py::arg("order_id"),
             "Cancel an active order. Returns True if cancelled.")
        .def("get_order", &OrderAPI::get_order, py::arg("order_id"),
             py::return_value_policy::reference_internal)
        .def("open_orders",  &OrderAPI::open_orders)
        .def("total_stats",  &OrderAPI::total_stats)
        .def("stats_by_instrument", &OrderAPI::stats_by_instrument,
             py::arg("instrument_id"));

    // ------------------------------------------------------------------
    // Strategy (with PyStrategy trampoline for Python subclassing)
    // ------------------------------------------------------------------
    py::class_<Strategy, PyStrategy>(m, "Strategy")
        .def(py::init<>())
        .def("on_book_update", &Strategy::on_book_update,
             py::arg("instrument_id"), py::arg("timestamp_ns"),
             py::arg("bids"), py::arg("asks"))
        .def("on_trade", &Strategy::on_trade,
             py::arg("instrument_id"), py::arg("timestamp_ns"),
             py::arg("price"), py::arg("size"), py::arg("side"))
        .def("on_fill", &Strategy::on_fill,
             py::arg("order_id"), py::arg("instrument_id"), py::arg("timestamp_ns"),
             py::arg("price"), py::arg("size"), py::arg("side"))
        .def("on_reject", &Strategy::on_reject,
             py::arg("order_id"), py::arg("instrument_id"), py::arg("reason"))
        // Expose OrderAPI through the _api pointer so strategy can call
        // self.api.send_limit_order(...) etc. inside callbacks.
        .def_property_readonly("api",
            [](Strategy& s) -> OrderAPI*
            {
                if (!s._api)
                    throw std::runtime_error("api is not available outside a backtest run");
                return s._api;
            },
            py::return_value_policy::reference);

    // ------------------------------------------------------------------
    // ProgressInfo
    // ------------------------------------------------------------------
    py::class_<ProgressInfo>(m, "ProgressInfo")
        .def_readonly("pct_done",          &ProgressInfo::pct_done)
        .def_readonly("last_timestamp_ns", &ProgressInfo::last_timestamp_ns)
        .def_readonly("current_pnl",       &ProgressInfo::current_pnl)
        .def_readonly("total_stats",       &ProgressInfo::total_stats)
        .def_readonly("by_instrument",     &ProgressInfo::by_instrument);

    // ------------------------------------------------------------------
    // BacktestConfig
    // ------------------------------------------------------------------
    py::class_<BacktestConfig>(m, "BacktestConfig")
        .def(py::init<>())
        .def_readwrite("book_levels",       &BacktestConfig::book_levels)
        .def_readwrite("progress_interval", &BacktestConfig::progress_interval)
        .def_readwrite("trading_engine_id", &BacktestConfig::trading_engine_id)
        .def_property("progress_callback",
            [](const BacktestConfig& c) -> py::object
            {
                // Return None if no callback set
                return c.progress_callback ? py::cast(true) : py::none();
            },
            [](BacktestConfig& c, py::object cb)
            {
                if (cb.is_none())
                {
                    c.progress_callback = nullptr;
                }
                else
                {
                    // Wrap the Python callable so we re-acquire the GIL before
                    // calling it (run() releases the GIL via call_guard).
                    c.progress_callback = [cb](const ProgressInfo& info)
                    {
                        py::gil_scoped_acquire gil;
                        cb(info);
                    };
                }
            });

    // ------------------------------------------------------------------
    // Result record types
    // ------------------------------------------------------------------
    py::class_<FillRecord>(m, "FillRecord")
        .def_readonly("order_id",      &FillRecord::order_id)
        .def_readonly("instrument_id", &FillRecord::instrument_id)
        .def_readonly("timestamp_ns",  &FillRecord::timestamp_ns)
        .def_readonly("side",          &FillRecord::side)
        .def_readonly("price",         &FillRecord::price)
        .def_readonly("size",          &FillRecord::size)
        .def_readonly("realized_pnl",  &FillRecord::realized_pnl);

    py::class_<OrderRecord>(m, "OrderRecord")
        .def_readonly("order",         &OrderRecord::order)
        .def_readonly("reject_reason", &OrderRecord::reject_reason);

    py::class_<PnlPoint>(m, "PnlPoint")
        .def_readonly("ts",  &PnlPoint::ts)
        .def_readonly("pnl", &PnlPoint::pnl);

    // ------------------------------------------------------------------
    // BacktestEngine
    // ------------------------------------------------------------------
    py::class_<BacktestEngine>(m, "BacktestEngine")
        .def(py::init<BacktestConfig>(), py::arg("config") = BacktestConfig{})
        .def("run",
             [](BacktestEngine& engine, const std::string& data_path,
                Strategy& strategy,
                py::object date_range_obj)
             {
                 std::optional<std::pair<std::string, std::string>> date_range;
                 if (!date_range_obj.is_none())
                 {
                     auto t      = date_range_obj.cast<py::tuple>();
                     date_range  = {t[0].cast<std::string>(), t[1].cast<std::string>()};
                 }
                 // Release GIL during the C++ event loop.
                 // Strategy callbacks re-acquire it via PYBIND11_OVERRIDE.
                 // Progress callbacks re-acquire it via the lambda wrapper above.
                 py::gil_scoped_release release;
                 engine.run(data_path, strategy, date_range);
             },
             py::arg("data_path"), py::arg("strategy"),
             py::arg("date_range") = py::none(),
             "Run the backtest. Blocks until all data is processed.")
        .def("fills",      &BacktestEngine::fills,
             py::return_value_policy::reference_internal)
        .def("order_log",  &BacktestEngine::order_log,
             py::return_value_policy::reference_internal)
        .def("pnl_series", &BacktestEngine::pnl_series,
             py::return_value_policy::reference_internal);
}
