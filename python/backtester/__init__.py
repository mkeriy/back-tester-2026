"""
backtester — Python interface to the CMF C++ backtesting engine.

Quick start::

    from backtester import Backtest, BacktestConfig
    from backtester_cpp import Strategy, Side, TimeInForce

    class MyStrategy(Strategy):
        def on_book_update(self, instrument_id, timestamp_ns, bids, asks):
            ...  # inspect book, call self.api.send_limit_order(...)

    result = Backtest().run(MyStrategy(), "data/")
    print(result.pnl_series)
    print(result.fills_df)
"""

from .backtester_cpp import (  # type: ignore[import]  # noqa: F401
    BacktestConfig,
    BacktestEngine,
    FillRecord,
    OrderAPI,
    OrderRecord,
    OrderStats,
    OrderStatus,
    OrderType,
    PnlPoint,
    ProgressInfo,
    Side,
    Strategy,
    TimeInForce,
)

from .results import BacktestResult  # noqa: F401
from .runner import Backtest  # noqa: F401

__all__ = [
    "Backtest",
    "BacktestConfig",
    "BacktestEngine",
    "BacktestResult",
    "FillRecord",
    "OrderAPI",
    "OrderRecord",
    "OrderStats",
    "OrderStatus",
    "OrderType",
    "PnlPoint",
    "ProgressInfo",
    "Side",
    "Strategy",
    "TimeInForce",
]
