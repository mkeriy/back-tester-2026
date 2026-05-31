#include "python/BacktestEngine.hpp"

#include "common/BlockingQueue.hpp"
#include "ingestion/FeatherDataParser.hpp"
#include "ingestion/FlatMerger.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <regex>
#include <thread>

namespace cmf
{

// ---------------------------------------------------------------------------
// BacktestEngine
// ---------------------------------------------------------------------------

BacktestEngine::BacktestEngine(BacktestConfig config)
    : config_(std::move(config)), api_(config_.trading_engine_id)
{
}

void BacktestEngine::reset()
{
    api_            = OrderAPI(config_.trading_engine_id);
    books_.clear();
    order_to_instrument_.clear();
    positions_.clear();
    avg_cost_.clear();
    last_mid_.clear();
    realized_pnl_ = 0.0;
    fills_.clear();
    order_log_.clear();
    pnl_series_.clear();
}

// ---------------------------------------------------------------------------
// File discovery
// ---------------------------------------------------------------------------

static std::string date_from_path(const std::filesystem::path& p)
{
    // Extract YYYYMMDD from any component that matches \d{8}
    std::regex re(R"(\b(\d{8})\b)");
    for (const auto& part : p)
    {
        std::string s = part.string();
        std::smatch m;
        if (std::regex_search(s, m, re))
            return m[1].str();
    }
    return {};
}

std::vector<std::filesystem::path> BacktestEngine::find_feather_files(
    const std::filesystem::path&                              data_path,
    const std::optional<std::pair<std::string, std::string>>& date_range)
{
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(data_path))
        return files;

    // Convert "YYYY-MM-DD" → "YYYYMMDD" for comparison
    auto strip_dashes = [](std::string s) -> std::string
    {
        s.erase(std::remove(s.begin(), s.end(), '-'), s.end());
        return s;
    };

    std::string lo, hi;
    if (date_range)
    {
        lo = strip_dashes(date_range->first);
        hi = strip_dashes(date_range->second);
    }

    constexpr std::string_view EXT = FeatherDataParser::filename_ext;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(data_path))
    {
        if (!entry.is_regular_file())
            continue;
        const std::string fname = entry.path().filename().string();
        if (fname.size() < EXT.size() ||
            fname.substr(fname.size() - EXT.size()) != EXT)
            continue;

        if (date_range)
        {
            std::string d = date_from_path(entry.path());
            if (d.empty() || d < lo || d > hi)
                continue;
        }
        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

// ---------------------------------------------------------------------------
// PnL helpers
// ---------------------------------------------------------------------------

double BacktestEngine::mark_to_market() const
{
    double mtm = 0.0;
    for (const auto& [instr, pos] : positions_)
    {
        if (std::abs(pos) < 1e-9)
            continue;
        auto mid_it = last_mid_.find(instr);
        if (mid_it == last_mid_.end())
            continue;
        auto cost_it = avg_cost_.find(instr);
        if (cost_it == avg_cost_.end())
            continue;
        mtm += pos * (mid_it->second - cost_it->second);
    }
    return mtm;
}

void BacktestEngine::update_position(uint32_t instrument_id, Side side, double price,
                                      int size, double& rpnl)
{
    rpnl         = 0.0;
    double  qty  = (side == Side::Buy) ? static_cast<double>(size) : -static_cast<double>(size);
    double& pos  = positions_[instrument_id];
    double& avg  = avg_cost_[instrument_id];

    if (std::abs(pos) < 1e-9)
    {
        // Opening a new position
        pos = qty;
        avg = price;
        return;
    }

    bool same_dir = (pos > 0 && qty > 0) || (pos < 0 && qty < 0);
    if (same_dir)
    {
        // Adding to position — update weighted average cost
        double new_pos = pos + qty;
        avg = (pos * avg + qty * price) / new_pos;
        pos = new_pos;
        return;
    }

    // Reducing or reversing
    double close_qty = std::min(std::abs(qty), std::abs(pos));
    rpnl = close_qty * (price - avg) * (pos > 0 ? 1.0 : -1.0);
    realized_pnl_ += rpnl;

    double new_pos = pos + qty;
    if (std::abs(new_pos) < 1e-9)
    {
        pos = 0.0;
        avg = 0.0;
    }
    else if (std::abs(new_pos) < std::abs(pos))
    {
        // Partial close — avg cost unchanged
        pos = new_pos;
    }
    else
    {
        // Reversal — reset avg to fill price for the new direction
        pos = new_pos;
        avg = price;
    }
}

// ---------------------------------------------------------------------------
// Per-event handler
// ---------------------------------------------------------------------------

void BacktestEngine::handle_event(const MarketDataEvent& e, Strategy& strategy)
{
    // Resolve canonical instrument_id (mirrors SimpleOrderBookRouter logic)
    if (e.instrument_id != 0 && e.order_id != 0)
        order_to_instrument_[e.order_id] = e.instrument_id;

    uint32_t eff_id = (e.instrument_id != 0)
                          ? e.instrument_id
                          : order_to_instrument_[e.order_id];
    if (eff_id == 0)
        return;

    // Update our own order book for this instrument
    books_[eff_id].apply(e);

    // Query current best bid/ask
    auto best_bid = books_[eff_id].best_price(Side::Buy);
    auto best_ask = books_[eff_id].best_price(Side::Sell);

    // Track mid price for MtM
    if (best_bid && best_ask)
        last_mid_[eff_id] =
            (static_cast<double>(*best_bid) + static_cast<double>(*best_ask)) * 0.5e-9;
    else if (best_ask)
        last_mid_[eff_id] = static_cast<double>(*best_ask) * 1e-9;
    else if (best_bid)
        last_mid_[eff_id] = static_cast<double>(*best_bid) * 1e-9;

    // Fire strategy market-data callbacks
    switch (e.action)
    {
    case Action::Add:
    case Action::Modify:
    case Action::Cancel:
    case Action::Clear:
    {
        // side_levels() returns a span into the book's shared internal cache.
        // The cache is repopulated on each call, so we must copy one side
        // to a vector BEFORE requesting the other side.
        std::vector<std::pair<double, int>> bid_vec, ask_vec;
        {
            auto bids = books_[eff_id].side_levels(Side::Buy);
            std::size_t n = std::min(config_.book_levels, bids.size());
            bid_vec.reserve(n);
            for (std::size_t i = 0; i < n; ++i)
                bid_vec.emplace_back(static_cast<double>(bids[i].first) * 1e-9,
                                     static_cast<int>(bids[i].second));
        }
        {
            auto asks = books_[eff_id].side_levels(Side::Sell);
            std::size_t n = std::min(config_.book_levels, asks.size());
            ask_vec.reserve(n);
            for (std::size_t i = 0; i < n; ++i)
                ask_vec.emplace_back(static_cast<double>(asks[i].first) * 1e-9,
                                     static_cast<int>(asks[i].second));
        }

        strategy.on_book_update(eff_id, e.ts_recv, bid_vec, ask_vec);
        break;
    }
    case Action::Trade:
    case Action::Fill:
        if (e.is_price_defined())
            strategy.on_trade(eff_id, e.ts_recv, e.price_as_double(),
                              static_cast<int>(e.size), e.side);
        break;
    default:
        break;
    }

    // Attempt fills on open strategy orders for this instrument
    auto result = api_.try_fill(eff_id, best_bid, best_ask, e.ts_recv);

    for (const auto& fill : result.fills)
    {
        const Order* o = api_.get_order(fill.order_id);
        if (!o)
            continue;
        double rpnl = 0.0;
        update_position(eff_id, o->side, fill.fill_price, fill.fill_size, rpnl);
        fills_.push_back({fill.order_id, eff_id, e.ts_recv, o->side, fill.fill_price,
                          fill.fill_size, rpnl});
        strategy.on_fill(fill.order_id, eff_id, e.ts_recv, fill.fill_price,
                         fill.fill_size, o->side);
    }
    for (const auto& rej : result.rejects)
        strategy.on_reject(rej.order_id, eff_id, rej.reason);
}

// ---------------------------------------------------------------------------
// Main run loop
// ---------------------------------------------------------------------------

void BacktestEngine::run(
    const std::filesystem::path&                        data_path,
    Strategy&                                           strategy,
    std::optional<std::pair<std::string, std::string>> date_range)
{
    reset();

    auto files = find_feather_files(data_path, date_range);
    if (files.empty())
        return;

    const std::size_t N = files.size();
    std::deque<BlockingQueue<MarketDataEvent>> file_queues;
    for (std::size_t i = 0; i < N; ++i)
        file_queues.emplace_back();

    BlockingQueue<MarketDataEvent>               merged_queue;
    FlatMerger<BlockingQueue, BlockingQueue>     merger(file_queues, merged_queue);

    strategy._api = &api_;

    // Producer threads — one per file
    std::vector<std::thread> producers;
    producers.reserve(N);
    for (std::size_t i = 0; i < N; ++i)
    {
        producers.emplace_back([&, i]()
        {
            constexpr std::size_t BATCH = 256;
            std::array<MarketDataEvent, BATCH> batch{};
            std::size_t cnt = 0;

            FeatherDataParser parser{files[i]};
            parser.parse([&](const MarketDataEvent& e)
            {
                batch[cnt++] = e;
                if (cnt >= BATCH)
                {
                    file_queues[i].push_batch(batch.data(), cnt);
                    cnt = 0;
                }
            });
            if (cnt > 0)
                file_queues[i].push_batch(batch.data(), cnt);
            file_queues[i].close();
        });
    }

    // Merger thread
    std::thread merger_thread([&]() { merger.run(); });

    // Dispatcher (main thread) — process events and fire strategy callbacks
    uint64_t event_count  = 0;
    NanoTime last_ts      = 0;
    auto     last_prog    = std::chrono::steady_clock::now();
    constexpr uint64_t PNL_SNAP_EVERY  = 1'000;
    constexpr uint64_t PROG_CHECK_EVERY = 10'000;

    while (merged_queue.pop([&](MarketDataEvent&& e)
    {
        handle_event(e, strategy);
        last_ts = e.ts_recv;
        ++event_count;

        if (event_count % PNL_SNAP_EVERY == 0)
            pnl_series_.push_back({last_ts, realized_pnl_ + mark_to_market()});

        if (config_.progress_callback && event_count % PROG_CHECK_EVERY == 0)
        {
            auto   now     = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - last_prog).count();
            if (elapsed >= config_.progress_interval)
            {
                last_prog = now;
                ProgressInfo info;
                info.pct_done         = 0.0; // total unknown without pre-scan
                info.last_timestamp_ns = last_ts;
                info.current_pnl      = realized_pnl_ + mark_to_market();
                info.total_stats      = api_.total_stats();
                for (const auto& [instr, _] : books_)
                    info.by_instrument[instr] = api_.stats_by_instrument(instr);
                config_.progress_callback(info);
            }
        }
    }))
        ;

    // Final PnL snapshot
    if (last_ts > 0)
        pnl_series_.push_back({last_ts, realized_pnl_ + mark_to_market()});

    // Collect full order log
    for (const auto& [id, o] : api_.all_orders())
        order_log_.push_back({o, o.reject_reason});

    for (auto& t : producers)
        t.join();
    merger_thread.join();

    strategy._api = nullptr;
}

} // namespace cmf
