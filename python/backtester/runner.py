from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from backtester_cpp import Strategy  # type: ignore[import]

from .backtester_cpp import BacktestConfig, BacktestEngine  # type: ignore[import]

from .results import BacktestResult


class Backtest:
    """High-level backtest runner.

    Usage::

        result = Backtest().run(strategy, "data/", date_range=("2026-04-01", "2026-04-30"))
        result.plot_pnl()
    """

    def __init__(self, config: BacktestConfig | None = None) -> None:
        self._config = config or BacktestConfig()

    def run(
        self,
        strategy: "Strategy",
        data_path: str | Path,
        date_range: tuple[str, str] | None = None,
    ) -> BacktestResult:
        """Run the backtest and return structured results.

        Parameters
        ----------
        strategy:
            A Strategy subclass instance with overridden callbacks.
        data_path:
            Directory containing ``.mbo.json.feather`` data files.
        date_range:
            Optional ``(start, end)`` in ``"YYYY-MM-DD"`` format to restrict
            which files are loaded based on date segments in their paths.
        """
        engine = BacktestEngine(self._config)
        engine.run(str(data_path), strategy, date_range)
        return BacktestResult(engine)
