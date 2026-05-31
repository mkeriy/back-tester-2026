from __future__ import annotations

from typing import TYPE_CHECKING

import pandas as pd

if TYPE_CHECKING:
    from backtester_cpp import BacktestEngine  # type: ignore[import]


class BacktestResult:
    """Holds all outputs from a completed backtest run as pandas DataFrames."""

    def __init__(self, engine: "BacktestEngine") -> None:
        pnl_pts = engine.pnl_series()
        self.pnl_series: pd.Series = pd.Series(
            [p.pnl for p in pnl_pts],
            index=[p.ts for p in pnl_pts],
            name="pnl",
            dtype=float,
        )
        self.pnl_series.index.name = "timestamp_ns"

        fills = engine.fills()
        self.fills_df: pd.DataFrame = (
            pd.DataFrame(
                {
                    "timestamp_ns": [f.timestamp_ns for f in fills],
                    "order_id": [f.order_id for f in fills],
                    "instrument_id": [f.instrument_id for f in fills],
                    "side": [f.side.name for f in fills],
                    "price": [f.price for f in fills],
                    "size": [f.size for f in fills],
                    "realized_pnl": [f.realized_pnl for f in fills],
                }
            )
            if fills
            else pd.DataFrame(
                columns=[
                    "timestamp_ns",
                    "order_id",
                    "instrument_id",
                    "side",
                    "price",
                    "size",
                    "realized_pnl",
                ]
            )
        )

        records = engine.order_log()
        self.order_log_df: pd.DataFrame = (
            pd.DataFrame(
                {
                    "order_id": [r.order.order_id for r in records],
                    "trading_engine_id": [r.order.trading_engine_id for r in records],
                    "instrument_id": [r.order.instrument_id for r in records],
                    "side": [r.order.side.name for r in records],
                    "price": [r.order.price_as_float() for r in records],
                    "size": [r.order.size for r in records],
                    "order_type": [r.order.order_type.name for r in records],
                    "tif": [r.order.tif.name for r in records],
                    "status": [r.order.status.name for r in records],
                    "sent_at": [r.order.sent_at for r in records],
                    "filled_at": [r.order.filled_at for r in records],
                    "filled_price": [r.order.filled_price for r in records],
                    "filled_size": [r.order.filled_size for r in records],
                    "reject_reason": [r.reject_reason for r in records],
                }
            )
            if records
            else pd.DataFrame(
                columns=[
                    "order_id",
                    "trading_engine_id",
                    "instrument_id",
                    "side",
                    "price",
                    "size",
                    "order_type",
                    "tif",
                    "status",
                    "sent_at",
                    "filled_at",
                    "filled_price",
                    "filled_size",
                    "reject_reason",
                ]
            )
        )

        total = engine.fills()
        self.summary: dict = {
            "total_pnl": self.pnl_series.iloc[-1] if len(self.pnl_series) else 0.0,
            "n_fills": len(self.fills_df),
            "n_orders": len(self.order_log_df),
            "realized_pnl": self.fills_df["realized_pnl"].sum() if len(self.fills_df) else 0.0,
        }

    # ------------------------------------------------------------------
    # Convenience plots
    # ------------------------------------------------------------------

    def plot_pnl(self, title: str = "Cumulative PnL") -> None:
        import matplotlib.pyplot as plt

        fig, ax = plt.subplots(figsize=(12, 4))
        ts_sec = self.pnl_series.index / 1e9
        ax.plot(ts_sec, self.pnl_series.values)
        ax.set_xlabel("Time (seconds since epoch)")
        ax.set_ylabel("PnL")
        ax.set_title(title)
        ax.grid(True)
        plt.tight_layout()
        plt.show()

    def plot_fills(self, instrument_id: int | None = None) -> None:
        import matplotlib.pyplot as plt

        df = self.fills_df
        if instrument_id is not None:
            df = df[df["instrument_id"] == instrument_id]

        fig, ax = plt.subplots(figsize=(12, 4))
        buys = df[df["side"] == "BUY"]
        sells = df[df["side"] == "SELL"]
        ax.scatter(buys["timestamp_ns"] / 1e9, buys["price"], marker="^",
                   color="green", label="Buy fill", zorder=5)
        ax.scatter(sells["timestamp_ns"] / 1e9, sells["price"], marker="v",
                   color="red", label="Sell fill", zorder=5)
        ax.set_xlabel("Time (seconds since epoch)")
        ax.set_ylabel("Fill price")
        ax.set_title("Order fills" + (f" — instrument {instrument_id}" if instrument_id else ""))
        ax.legend()
        ax.grid(True)
        plt.tight_layout()
        plt.show()

    def __repr__(self) -> str:
        return (
            f"BacktestResult(total_pnl={self.summary['total_pnl']:.4f}, "
            f"n_fills={self.summary['n_fills']}, "
            f"n_orders={self.summary['n_orders']})"
        )
