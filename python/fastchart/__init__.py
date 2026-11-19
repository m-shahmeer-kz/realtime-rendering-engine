"""fastchart: a C++ engine that draws millions of market data points in real time.

Python side: load data with pandas, convert it to the engine's compact format, and
compute or render views (headless OpenGL through OSMesa).
"""
from __future__ import annotations

import numpy as np
import pandas as pd

from ._fastchart import Offscreen, Series, all_points, candles, frame_stats, m4  # noqa: F401

__all__ = ["Series", "Offscreen", "m4", "all_points", "candles", "frame_stats",
           "from_dataframe", "load_bitstamp", "choose_scale", "pandas_bytes"]
__version__ = "1.0.0"


def choose_scale(values: np.ndarray, candidates=(1, 10, 100, 1000, 10000, 100000, 1000000), min_exact=0.99999):
    """Smallest power-of-ten scale that stores (almost) every price exactly as an int32.

    Returns (scale, share of values stored exactly, largest rounding error in price units).
    """
    v = np.asarray(values, dtype=np.float64).ravel()
    for s in candidates:
        x = v * s
        if np.abs(x).max() >= 2**31 - 1:
            break
        err = np.abs(x - np.round(x))
        exact = float((err < 1e-6).mean())
        if exact >= min_exact:
            return s, exact, float(err.max() / s)
    raise ValueError("prices cannot be stored as 32-bit fixed point; scale them first")


def from_dataframe(df: pd.DataFrame, time: str = "timestamp", close: str = "close", open: str | None = "open",
                   high: str | None = "high", low: str | None = "low", volume: str | None = "volume"):
    """Convert a regular OHLC(V) DataFrame (one row every N seconds) into an engine Series.

    Returns (series, report) where report says how the data was stored.
    """
    t = df[time].to_numpy()
    if np.issubdtype(t.dtype, np.datetime64):
        t = t.astype("datetime64[s]").astype(np.int64)
    t = t.astype(np.int64)
    if len(t) < 2:
        raise ValueError("need at least 2 rows")
    steps = np.diff(t)
    if not (steps == steps[0]).all() or steps[0] <= 0:
        raise ValueError("timestamps must be sorted and evenly spaced (fill missing rows first)")
    cols = [c for c in (open, high, low, close) if c is not None and c in df]
    scale, exact, max_err = choose_scale(df[cols].to_numpy())
    fixed = {c: np.round(df[c].to_numpy(np.float64) * scale).astype(np.int32) for c in cols}
    ohlc = all(c is not None and c in df for c in (open, high, low))
    s = Series(int(t[0]), int(steps[0]), int(scale), fixed[close],
               fixed[open] if ohlc else None, fixed[high] if ohlc else None, fixed[low] if ohlc else None,
               df[volume].to_numpy(np.float32) if volume and volume in df else None)
    report = {"rows": int(len(df)), "step_seconds": int(steps[0]), "scale": int(scale),
              "prices_exact_share": exact, "max_price_error": max_err}
    return s, report


def load_bitstamp(path: str) -> pd.DataFrame:
    """Read the Bitstamp BTC/USD 1-minute CSV (timestamp, open, high, low, close, volume)."""
    return pd.read_csv(path)


def pandas_bytes(df: pd.DataFrame) -> int:
    """Memory pandas uses for the same table (including the index)."""
    return int(df.memory_usage(deep=True, index=True).sum())
