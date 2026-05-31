#include "lob/LimitOrderBook.hpp"
#include "common/enums.hpp"
#include "common/MarketDataEvent.hpp"

#include <catch2/catch_all.hpp>

using namespace cmf;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static MarketDataEvent make_ev(action::Action act, side::Side s,
                               double price, double qty, OrderId id,
                               uint8_t flags = 0) {
    MarketDataEvent ev{};
    ev.action   = act;
    ev.side     = s;
    ev.price    = price;
    ev.qty      = qty;
    ev.order_id = id;
    ev.flags    = flags;
    return ev;
}

static MarketDataEvent add_ev(side::Side s, double p, double q, OrderId id) {
    return make_ev(action::Action::Add, s, p, q, id);
}

static MarketDataEvent cancel_ev(side::Side s, double p, double q, OrderId id) {
    return make_ev(action::Action::Cancel, s, p, q, id);
}

static MarketDataEvent modify_ev(side::Side s, double p, double q, OrderId id) {
    return make_ev(action::Action::Modify, s, p, q, id);
}

// ---------------------------------------------------------------------------
// Price scaling
// ---------------------------------------------------------------------------

TEST_CASE("scale_price round-trip", "[lob][scale]") {
    REQUIRE(LimitOrderBook::scale_price(1.162900000) == 1162900000LL);
    REQUIRE(LimitOrderBook::scale_price(0.0) == 0LL);
    REQUIRE(LimitOrderBook::unscale_price(1162900000LL) == Catch::Approx(1.1629).epsilon(1e-9));
    REQUIRE(LimitOrderBook::unscale_price(0LL) == 0.0);
}

// ---------------------------------------------------------------------------
// Empty book
// ---------------------------------------------------------------------------

TEST_CASE("empty book has zero best prices", "[lob]") {
    LimitOrderBook book;
    REQUIRE(book.best_bid_price() == 0);
    REQUIRE(book.best_ask_price() == 0);
    REQUIRE(book.volume_at(LimitOrderBook::scale_price(1.0)) == 0);
}

// ---------------------------------------------------------------------------
// Add orders
// ---------------------------------------------------------------------------

TEST_CASE("add single bid", "[lob][add]") {
    LimitOrderBook book;
    book.apply(add_ev(side::Side::Buy, 1.10, 10, 1));

    REQUIRE(book.best_bid_price() == LimitOrderBook::scale_price(1.10));
    REQUIRE(book.best_ask_price() == 0);
    REQUIRE(book.volume_at(LimitOrderBook::scale_price(1.10)) == 10);
}

TEST_CASE("add single ask", "[lob][add]") {
    LimitOrderBook book;
    book.apply(add_ev(side::Side::Sell, 1.15, 5, 1));

    REQUIRE(book.best_ask_price() == LimitOrderBook::scale_price(1.15));
    REQUIRE(book.best_bid_price() == 0);
    REQUIRE(book.volume_at(LimitOrderBook::scale_price(1.15)) == 5);
}

TEST_CASE("best_bid is the highest bid price", "[lob][add]") {
    LimitOrderBook book;
    book.apply(add_ev(side::Side::Buy, 1.10, 10, 1));
    book.apply(add_ev(side::Side::Buy, 1.12, 20, 2));
    book.apply(add_ev(side::Side::Buy, 1.08, 15, 3));

    REQUIRE(book.best_bid_price() == LimitOrderBook::scale_price(1.12));
}

TEST_CASE("best_ask is the lowest ask price", "[lob][add]") {
    LimitOrderBook book;
    book.apply(add_ev(side::Side::Sell, 1.15, 10, 1));
    book.apply(add_ev(side::Side::Sell, 1.13, 20, 2));
    book.apply(add_ev(side::Side::Sell, 1.17, 15, 3));

    REQUIRE(book.best_ask_price() == LimitOrderBook::scale_price(1.13));
}

TEST_CASE("multiple orders at the same price accumulate volume", "[lob][add]") {
    LimitOrderBook book;
    book.apply(add_ev(side::Side::Buy, 1.10, 10, 1));
    book.apply(add_ev(side::Side::Buy, 1.10, 25, 2));
    book.apply(add_ev(side::Side::Buy, 1.10, 15, 3));

    REQUIRE(book.volume_at(LimitOrderBook::scale_price(1.10)) == 50);
    REQUIRE(book.best_bid_price() == LimitOrderBook::scale_price(1.10));
}

// ---------------------------------------------------------------------------
// Cancel orders
// ---------------------------------------------------------------------------

TEST_CASE("cancel the only order removes the level", "[lob][cancel]") {
    LimitOrderBook book;
    book.apply(add_ev(side::Side::Buy, 1.10, 10, 1));
    book.apply(cancel_ev(side::Side::Buy, 1.10, 10, 1));

    REQUIRE(book.best_bid_price() == 0);
    REQUIRE(book.volume_at(LimitOrderBook::scale_price(1.10)) == 0);
}

TEST_CASE("cancel one order from multi-order level reduces volume", "[lob][cancel]") {
    LimitOrderBook book;
    book.apply(add_ev(side::Side::Buy, 1.10, 10, 1));
    book.apply(add_ev(side::Side::Buy, 1.10, 20, 2));
    book.apply(cancel_ev(side::Side::Buy, 1.10, 10, 1));

    REQUIRE(book.volume_at(LimitOrderBook::scale_price(1.10)) == 20);
    REQUIRE(book.best_bid_price() == LimitOrderBook::scale_price(1.10));
}

TEST_CASE("cancel best bid falls back to next level", "[lob][cancel]") {
    LimitOrderBook book;
    book.apply(add_ev(side::Side::Buy, 1.12, 10, 1));
    book.apply(add_ev(side::Side::Buy, 1.10, 10, 2));

    REQUIRE(book.best_bid_price() == LimitOrderBook::scale_price(1.12));
    book.apply(cancel_ev(side::Side::Buy, 1.12, 10, 1));
    REQUIRE(book.best_bid_price() == LimitOrderBook::scale_price(1.10));
}

TEST_CASE("cancel best ask falls back to next level", "[lob][cancel]") {
    LimitOrderBook book;
    book.apply(add_ev(side::Side::Sell, 1.13, 10, 1));
    book.apply(add_ev(side::Side::Sell, 1.15, 10, 2));

    REQUIRE(book.best_ask_price() == LimitOrderBook::scale_price(1.13));
    book.apply(cancel_ev(side::Side::Sell, 1.13, 10, 1));
    REQUIRE(book.best_ask_price() == LimitOrderBook::scale_price(1.15));
}

TEST_CASE("cancel unknown order is a no-op", "[lob][cancel]") {
    LimitOrderBook book;
    book.apply(add_ev(side::Side::Buy, 1.10, 10, 1));
    book.apply(cancel_ev(side::Side::Buy, 1.10, 10, 999));  // unknown id

    REQUIRE(book.best_bid_price() == LimitOrderBook::scale_price(1.10));
    REQUIRE(book.volume_at(LimitOrderBook::scale_price(1.10)) == 10);
}

TEST_CASE("cancel all orders empties the book", "[lob][cancel]") {
    LimitOrderBook book;
    book.apply(add_ev(side::Side::Buy,  1.10, 10, 1));
    book.apply(add_ev(side::Side::Sell, 1.15, 10, 2));
    book.apply(cancel_ev(side::Side::Buy,  1.10, 10, 1));
    book.apply(cancel_ev(side::Side::Sell, 1.15, 10, 2));

    REQUIRE(book.best_bid_price() == 0);
    REQUIRE(book.best_ask_price() == 0);
}

// ---------------------------------------------------------------------------
// Modify orders
// ---------------------------------------------------------------------------

TEST_CASE("modify quantity at same price", "[lob][modify]") {
    LimitOrderBook book;
    book.apply(add_ev(side::Side::Buy, 1.10, 10, 1));
    book.apply(modify_ev(side::Side::Buy, 1.10, 30, 1));

    REQUIRE(book.volume_at(LimitOrderBook::scale_price(1.10)) == 30);
    REQUIRE(book.best_bid_price() == LimitOrderBook::scale_price(1.10));
}

TEST_CASE("modify price moves order to new level", "[lob][modify]") {
    LimitOrderBook book;
    book.apply(add_ev(side::Side::Buy, 1.10, 10, 1));
    book.apply(modify_ev(side::Side::Buy, 1.12, 10, 1));

    REQUIRE(book.volume_at(LimitOrderBook::scale_price(1.10)) == 0);
    REQUIRE(book.volume_at(LimitOrderBook::scale_price(1.12)) == 10);
    REQUIRE(book.best_bid_price() == LimitOrderBook::scale_price(1.12));
}

TEST_CASE("modify price removes old level when it becomes empty", "[lob][modify]") {
    LimitOrderBook book;
    book.apply(add_ev(side::Side::Buy, 1.10, 10, 1));
    book.apply(add_ev(side::Side::Buy, 1.10, 20, 2));
    book.apply(modify_ev(side::Side::Buy, 1.12, 10, 1));

    // Old level still has order 2
    REQUIRE(book.volume_at(LimitOrderBook::scale_price(1.10)) == 20);
    // New level got order 1
    REQUIRE(book.volume_at(LimitOrderBook::scale_price(1.12)) == 10);
    REQUIRE(book.best_bid_price() == LimitOrderBook::scale_price(1.12));
}

TEST_CASE("modify unknown order is a no-op", "[lob][modify]") {
    LimitOrderBook book;
    book.apply(add_ev(side::Side::Buy, 1.10, 10, 1));
    book.apply(modify_ev(side::Side::Buy, 1.12, 10, 999));

    REQUIRE(book.best_bid_price() == LimitOrderBook::scale_price(1.10));
    REQUIRE(book.volume_at(LimitOrderBook::scale_price(1.12)) == 0);
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------

TEST_CASE("clear empties all levels and orders", "[lob][clear]") {
    LimitOrderBook book;
    book.apply(add_ev(side::Side::Buy,  1.10, 10, 1));
    book.apply(add_ev(side::Side::Buy,  1.08, 20, 2));
    book.apply(add_ev(side::Side::Sell, 1.15, 10, 3));
    book.apply(make_ev(action::Action::Clear, side::Side::None, 0.0, 0.0, 0));

    REQUIRE(book.best_bid_price() == 0);
    REQUIRE(book.best_ask_price() == 0);
    REQUIRE(book.volume_at(LimitOrderBook::scale_price(1.10)) == 0);
    REQUIRE(book.volume_at(LimitOrderBook::scale_price(1.15)) == 0);
}

TEST_CASE("can add orders after clear", "[lob][clear]") {
    LimitOrderBook book;
    book.apply(add_ev(side::Side::Buy, 1.10, 10, 1));
    book.apply(make_ev(action::Action::Clear, side::Side::None, 0.0, 0.0, 0));
    book.apply(add_ev(side::Side::Buy, 1.12, 5, 2));

    REQUIRE(book.best_bid_price() == LimitOrderBook::scale_price(1.12));
    REQUIRE(book.volume_at(LimitOrderBook::scale_price(1.12)) == 5);
}

// ---------------------------------------------------------------------------
// Flag filtering
// ---------------------------------------------------------------------------

TEST_CASE("events with should_skip flags are ignored", "[lob][flags]") {
    LimitOrderBook book;
    // F_BAD_TS_RECV = 8
    book.apply(add_ev(side::Side::Buy, 1.10, 10, 1));
    book.apply(make_ev(action::Action::Add, side::Side::Buy, 1.12, 20, 2, flags::F_BAD_TS_RECV));

    REQUIRE(book.best_bid_price() == LimitOrderBook::scale_price(1.10));
    REQUIRE(book.volume_at(LimitOrderBook::scale_price(1.12)) == 0);
}

TEST_CASE("F_MAYBE_BAD_BOOK events are ignored", "[lob][flags]") {
    LimitOrderBook book;
    book.apply(make_ev(action::Action::Add, side::Side::Sell, 1.15, 5, 1, flags::F_MAYBE_BAD_BOOK));

    REQUIRE(book.best_ask_price() == 0);
}

// ---------------------------------------------------------------------------
// AVL tree stress: many levels, best price must always be correct
// ---------------------------------------------------------------------------

TEST_CASE("best bid stays correct after many inserts and cancels", "[lob][avl]") {
    LimitOrderBook book;
    // Add bids at prices 1.00, 1.01, ..., 1.19
    for (int i = 0; i < 20; ++i) {
        double p = 1.00 + i * 0.01;
        book.apply(add_ev(side::Side::Buy, p, 10, static_cast<OrderId>(i + 1)));
    }
    REQUIRE(book.best_bid_price() == LimitOrderBook::scale_price(1.19));

    // Cancel from top down; best_bid should follow
    for (int i = 19; i >= 0; --i) {
        double p = 1.00 + i * 0.01;
        book.apply(cancel_ev(side::Side::Buy, p, 10, static_cast<OrderId>(i + 1)));
        if (i > 0) {
            REQUIRE(book.best_bid_price() ==
                    LimitOrderBook::scale_price(1.00 + (i - 1) * 0.01));
        } else {
            REQUIRE(book.best_bid_price() == 0);
        }
    }
}

TEST_CASE("best ask stays correct after many inserts and cancels", "[lob][avl]") {
    LimitOrderBook book;
    // Add asks at prices 1.20, 1.19, ..., 1.01 (out of order)
    for (int i = 19; i >= 0; --i) {
        double p = 1.00 + i * 0.01;
        book.apply(add_ev(side::Side::Sell, p, 10, static_cast<OrderId>(100 + i)));
    }
    REQUIRE(book.best_ask_price() == LimitOrderBook::scale_price(1.00));

    // Cancel from bottom up
    for (int i = 0; i < 20; ++i) {
        double p = 1.00 + i * 0.01;
        book.apply(cancel_ev(side::Side::Sell, p, 10, static_cast<OrderId>(100 + i)));
        if (i < 19) {
            REQUIRE(book.best_ask_price() ==
                    LimitOrderBook::scale_price(1.00 + (i + 1) * 0.01));
        } else {
            REQUIRE(book.best_ask_price() == 0);
        }
    }
}

TEST_CASE("interleaved bid/ask inserts and cancels maintain correct best", "[lob][avl]") {
    LimitOrderBook book;
    // Build a 5-level book each side
    for (int i = 0; i < 5; ++i) {
        book.apply(add_ev(side::Side::Buy,  1.00 + i * 0.01, 10, static_cast<OrderId>(i + 1)));
        book.apply(add_ev(side::Side::Sell, 1.10 + i * 0.01, 10, static_cast<OrderId>(100 + i)));
    }
    REQUIRE(book.best_bid_price() == LimitOrderBook::scale_price(1.04));
    REQUIRE(book.best_ask_price() == LimitOrderBook::scale_price(1.10));

    // Remove the middle bid level
    book.apply(cancel_ev(side::Side::Buy, 1.02, 10, 3));
    REQUIRE(book.best_bid_price() == LimitOrderBook::scale_price(1.04));
    REQUIRE(book.volume_at(LimitOrderBook::scale_price(1.02)) == 0);

    // Remove the best ask
    book.apply(cancel_ev(side::Side::Sell, 1.10, 10, 100));
    REQUIRE(book.best_ask_price() == LimitOrderBook::scale_price(1.11));
}

// ---------------------------------------------------------------------------
// Realistic book snapshot
// ---------------------------------------------------------------------------

TEST_CASE("realistic order book snapshot", "[lob][snapshot]") {
    LimitOrderBook book;
    OrderId id = 1;

    // --- Build initial bid side: 6 levels, multiple orders per level ---
    // Price   Orders   Qty each   IDs
    // 1.1620   3       100        1,2,3
    // 1.1615   2       200        4,5
    // 1.1610   4        50        6,7,8,9
    // 1.1605   1       500        10
    // 1.1600   3       150        11,12,13
    // 1.1595   2        75        14,15
    struct Level { double price; int orders; int qty; };
    Level bids[] = {
        {1.1620, 3, 100},
        {1.1615, 2, 200},
        {1.1610, 4,  50},
        {1.1605, 1, 500},
        {1.1600, 3, 150},
        {1.1595, 2,  75},
    };
    for (auto& lv : bids)
        for (int i = 0; i < lv.orders; ++i)
            book.apply(add_ev(side::Side::Buy, lv.price, lv.qty, id++));
    // id == 16 here

    // --- Build initial ask side: 6 levels ---
    // Price   Orders   Qty each   IDs
    // 1.1625   2        80        16,17
    // 1.1630   3       120        18,19,20
    // 1.1635   1       300        21
    // 1.1640   4        60        22,23,24,25
    // 1.1645   2       250        26,27
    // 1.1650   3        90        28,29,30
    Level asks[] = {
        {1.1625, 2,  80},
        {1.1630, 3, 120},
        {1.1635, 1, 300},
        {1.1640, 4,  60},
        {1.1645, 2, 250},
        {1.1650, 3,  90},
    };
    for (auto& lv : asks)
        for (int i = 0; i < lv.orders; ++i)
            book.apply(add_ev(side::Side::Sell, lv.price, lv.qty, id++));
    // id == 31 here

    // --- Activity: cancels, modifies, more adds ---

    // Cancel one order from best bid (id=1, price=1.1620, qty=100)
    book.apply(cancel_ev(side::Side::Buy, 1.1620, 100, 1));

    // Modify second order at best bid: increase qty 100→250 (id=2)
    book.apply(modify_ev(side::Side::Buy, 1.1620, 250, 2));

    // New aggressive buyer at 1.1622 — new best bid (id=31)
    const OrderId new_best_bid_id = id;
    book.apply(add_ev(side::Side::Buy, 1.1622, 400, id++));
    // id == 32

    // Cancel entire 1.1605 level — only one order there (id=10)
    book.apply(cancel_ev(side::Side::Buy, 1.1605, 500, 10));

    // New ask tighter than best ask at 1.1623 (id=32)
    book.apply(add_ev(side::Side::Sell, 1.1623, 150, id++));
    // id == 33

    // Modify first order at 1.1630: drop qty 120→40 (id=18)
    book.apply(modify_ev(side::Side::Sell, 1.1630, 40, 18));

    // Flurry of small adds on both sides (bid ids: 33,35,37,...; ask ids: 34,36,38,...)
    const OrderId flurry_bid_start = id;
    for (int i = 0; i < 10; ++i) {
        book.apply(add_ev(side::Side::Buy,  1.1618 - i * 0.0001, 25, id++));  // odd offset
        book.apply(add_ev(side::Side::Sell, 1.1628 + i * 0.0001, 25, id++));  // even offset
    }
    // id == 53 here

    // Cancel the first 5 flurry bids (ids 33,35,37,39,41 at prices 1.1618…1.1614)
    for (int i = 0; i < 5; ++i)
        book.apply(cancel_ev(side::Side::Buy, 1.1618 - i * 0.0001, 25, flurry_bid_start + i * 2));

    (void)new_best_bid_id;

    // Verify best prices
    REQUIRE(book.best_bid_price() == LimitOrderBook::scale_price(1.1622));
    REQUIRE(book.best_ask_price() == LimitOrderBook::scale_price(1.1623));

    std::printf("\n========== Realistic Book Snapshot (10 levels) ==========\n");
    book.print_snapshot(10);
    std::printf("==========================================================\n");
    std::printf("best bid: %.4f  best ask: %.4f  spread: %.4f\n",
        LimitOrderBook::unscale_price(book.best_bid_price()),
        LimitOrderBook::unscale_price(book.best_ask_price()),
        LimitOrderBook::unscale_price(book.best_ask_price() - book.best_bid_price()));
}
