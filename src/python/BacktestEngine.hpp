#pragma once

#include "order_book/AbseilOrderBook.hpp"
#include "python/OrderAPI.hpp"
#include "python/Strategy.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cmf
{

struct ProgressInfo
{
    double      pct_done{};          // 0–100; 0 when total is unknown
    NanoTime    last_timestamp_ns{}; // ts_recv of most-recent processed event
    double      current_pnl{};       // realized + unrealized PnL
    OrderAPI::Stats total_stats{};
    std::unordered_map<uint32_t, OrderAPI::Stats> by_instrument{};
};

struct BacktestConfig
{
    std::size_t  book_levels       = 10;   // depth passed to on_book_update
    double       progress_interval = 30.0; // seconds between progress callbacks
    uint16_t     trading_engine_id = 1;
    std::function<void(const ProgressInfo&)> progress_callback;
};

struct FillRecord
{
    OrderId     order_id{};
    uint32_t    instrument_id{};
    NanoTime    timestamp_ns{};
    Side        side{Side::None};
    double      price{};
    int         size{};
    double      realized_pnl{};
};

struct OrderRecord
{
    Order       order{};
    std::string reject_reason{};
};

struct PnlPoint
{
    NanoTime ts{};
    double   pnl{};
};

class BacktestEngine
{
  public:
    explicit BacktestEngine(BacktestConfig config = {});

    // Run the backtest.  Blocks until all data in data_path is processed.
    // date_range: optional ("YYYY-MM-DD", "YYYY-MM-DD") filter applied to
    // directory names that contain a YYYYMMDD date segment (e.g. XEUR-20260409-…).
    void run(const std::filesystem::path&                        data_path,
             Strategy&                                           strategy,
             std::optional<std::pair<std::string, std::string>> date_range = {});

    [[nodiscard]] const std::vector<FillRecord>&  fills()      const { return fills_; }
    [[nodiscard]] const std::vector<OrderRecord>& order_log()  const { return order_log_; }
    [[nodiscard]] const std::vector<PnlPoint>&    pnl_series() const { return pnl_series_; }

  private:
    BacktestConfig config_;

    // Per-run state (reset at start of every run())
    OrderAPI api_;
    std::unordered_map<uint32_t, OrderAPI::Stats>     instrument_stats_snapshot_;

    // Order book per instrument (owns the state so we can query bid/ask/levels)
    std::unordered_map<uint32_t, AbseilOrderBook>  books_;
    std::unordered_map<uint64_t, uint32_t>         order_to_instrument_;

    // Position/PnL tracking
    std::unordered_map<uint32_t, double> positions_; // signed quantity
    std::unordered_map<uint32_t, double> avg_cost_;
    std::unordered_map<uint32_t, double> last_mid_;
    double                               realized_pnl_{};

    // Accumulated results
    std::vector<FillRecord>  fills_;
    std::vector<OrderRecord> order_log_;
    std::vector<PnlPoint>    pnl_series_;

    void reset();
    void handle_event(const MarketDataEvent& e, Strategy& strategy);
    void update_position(uint32_t instrument_id, Side side, double price, int size,
                         double& realized_pnl_this_fill);
    [[nodiscard]] double mark_to_market() const;

    static std::vector<std::filesystem::path> find_feather_files(
        const std::filesystem::path&                              data_path,
        const std::optional<std::pair<std::string, std::string>>& date_range);
};

} // namespace cmf
