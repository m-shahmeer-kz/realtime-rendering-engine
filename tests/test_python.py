import numpy as np
import pandas as pd
import pytest

import fastchart as fc


def make_df(n=50_000, seed=0, step=60):
    rng = np.random.default_rng(seed)
    close = np.round(30000 + np.cumsum(rng.normal(0, 5, n)), 2)
    open_ = np.r_[close[0], close[:-1]]
    high = np.maximum(open_, close) + np.round(rng.uniform(0, 3, n), 2)
    low = np.minimum(open_, close) - np.round(rng.uniform(0, 3, n), 2)
    return pd.DataFrame({"timestamp": 1_600_000_000 + step * np.arange(n), "open": open_, "high": high,
                         "low": low, "close": close, "volume": rng.uniform(0, 2, n)})


def test_choose_scale_picks_cents():
    scale, exact, err = fc.choose_scale(np.array([1.25, 100.1, 7.0]))
    assert scale == 100 and exact == 1.0 and err < 1e-9


def test_from_dataframe_is_lossless_and_smaller():
    df = make_df()
    s, rep = fc.from_dataframe(df)
    assert len(s) == len(df) and s.step == 60 and s.scale == 100 and s.has_ohlc
    assert np.array_equal(s.close() / 100, df["close"].to_numpy())
    assert s.memory_bytes() < 0.5 * fc.pandas_bytes(df)


def test_irregular_timestamps_are_rejected():
    df = make_df(100)
    df.loc[50, "timestamp"] += 1
    with pytest.raises(ValueError):
        fc.from_dataframe(df)


def test_pyramid_matches_scan():
    s, _ = fc.from_dataframe(make_df())
    rng = np.random.default_rng(1)
    for _ in range(500):
        a = int(rng.integers(0, len(s) - 1)); b = int(rng.integers(a + 1, len(s) + 1))
        assert s.range_minmax(a, b)[:2] == s.range_minmax_scan(a, b)[:2]


def test_m4_is_small_and_keeps_extremes():
    s, _ = fc.from_dataframe(make_df())
    pts = fc.m4(s, s.t0, s.t_end, 500)
    allp = fc.all_points(s, s.t0, s.t_end, 500)
    assert len(pts) <= 4 * 500 + 2 < len(allp)
    assert pts[:, 1].min() == allp[:, 1].min() and pts[:, 1].max() == allp[:, 1].max()
    assert (np.diff(pts[:, 0]) >= 0).all()


def test_candles_use_real_ohlc():
    df = make_df(1000)
    s, _ = fc.from_dataframe(df)
    c = fc.candles(s, s.t0, s.t0 + 60 * 10, 80, 8)  # 10 slots -> 1 row per candle
    assert np.allclose(c[0, 2:], df.loc[0, ["open", "high", "low", "close"]].to_numpy(float))


def test_save_load_roundtrip(tmp_path):
    s, _ = fc.from_dataframe(make_df(5000))
    p = tmp_path / "x.fcbin"
    s.save(str(p))
    t = fc.Series.load(str(p))
    assert len(t) == len(s) and np.array_equal(t.close(), s.close()) and t.has_ohlc


def test_headless_render_matches_all_points():
    s, _ = fc.from_dataframe(make_df(200_000))
    try:
        off = fc.Offscreen(320, 180)
    except RuntimeError as e:  # no OSMesa on this machine
        pytest.skip(str(e))
    a, _, va = off.render(s, s.t0, s.t_end, "m4")
    b, _, vb = off.render(s, s.t0, s.t_end, "all")
    assert a.shape == (180, 320, 4) and va < vb / 50
    diff = (np.abs(a.astype(int) - b.astype(int)).sum(-1) > 0).mean()
    assert diff < 0.002  # fewer than 0.2% of pixels differ
