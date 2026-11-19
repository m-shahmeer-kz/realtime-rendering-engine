"""Measure the engine on the real data and save numbers, charts and screenshots.

  python scripts/benchmark.py        ->  reports/benchmarks.json, reports/figures/*.png

Renders use OSMesa (Mesa's software OpenGL on the CPU), so frame times here are
worst-case numbers; a real GPU is much faster.
"""
import json
import os
import platform
import statistics as st
import sys
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
import fastchart as fc  # noqa: E402

import matplotlib  # noqa: E402

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from PIL import Image  # noqa: E402

FIG = ROOT / "reports" / "figures"
FIG.mkdir(parents=True, exist_ok=True)
INK, GREY, LIGHT = "#111111", "#8a8a8f", "#d9d9dc"
plt.rcParams.update({"figure.dpi": 130, "font.size": 9, "axes.spines.top": False, "axes.spines.right": False,
                     "axes.edgecolor": GREY, "axes.titleweight": "bold", "axes.titlesize": 11})
W, H = 1280, 720


def random_views(s, n, rng, min_span=3600.0):
    full = s.t_end - s.t0
    out = []
    for _ in range(n):
        span = 10 ** rng.uniform(np.log10(min_span), np.log10(full))
        a = rng.uniform(s.t0, s.t_end - span)
        out.append((a, a + span))
    return out


def main() -> None:
    rng = np.random.default_rng(0)
    csv = ROOT / "data" / "btcusd_bitstamp_1min_2012-2025.csv.gz"
    df = fc.load_bitstamp(str(csv))
    s, rep = fc.from_dataframe(df)
    res = {"machine": f"{platform.processor() or platform.machine()}, {os.cpu_count()} CPU cores, no GPU (Mesa llvmpipe)",
           "data": {"rows": len(s), "first": int(s.t0), "last": int(s.t_end - s.step), **rep}}

    # 1. memory
    pb, eb = fc.pandas_bytes(df), s.memory_bytes()
    res["memory"] = {"pandas_bytes": pb, "engine_bytes": eb, "pyramid_bytes": s.pyramid_bytes(),
                     "saving": 1 - eb / pb, "bytes_per_row_pandas": pb / len(s), "bytes_per_row_engine": eb / len(s)}

    # 2. building a view on the CPU (what happens on every zoom / pan), 1920 px wide
    views = [(s.t0, s.t_end)] + random_views(s, 400, rng)
    stats = [fc.frame_stats(s, a, b, 1920) for a, b in views]
    line = [x for x in stats if not x["candles"]]
    cpu = [x["cpu_ms"] for x in stats]
    res["view_build_1920px"] = {
        "views": len(stats), "median_ms": st.median(cpu), "p95_ms": float(np.percentile(cpu, 95)), "max_ms": max(cpu),
        "full_range_ms": stats[0]["cpu_ms"], "full_range_vertices": stats[0]["vertices"],
        "full_range_samples": stats[0]["samples"], "median_vertices_line_mode": st.median([x["vertices"] for x in line]),
    }

    # 3. rendering (headless OpenGL 3.3, 1280x720): M4 vs every point, and pixel accuracy
    off = fc.Offscreen(W, H)
    off.render(s, s.t0, s.t_end, "m4")  # warm up shader JIT
    off.render(s, s.t0, s.t_end, "all")
    rviews = [(s.t0, s.t_end)] + random_views(s, 29, rng, min_span=86400 * 2)
    rows = []
    for a, b in rviews:
        t = time.perf_counter(); img_m, ms_m, v_m = off.render(s, a, b, "m4"); tm = (time.perf_counter() - t) * 1000
        t = time.perf_counter(); img_a, ms_a, v_a = off.render(s, a, b, "all"); ta = (time.perf_counter() - t) * 1000
        lit = (np.abs(img_a[..., :3].astype(int) - img_a[0, 0, :3].astype(int)).sum(-1) > 60)
        diff = np.abs(img_m.astype(int) - img_a.astype(int)).sum(-1) > 0
        rows.append({"samples": v_a, "m4_vertices": v_m, "m4_ms": tm, "all_ms": ta,
                     "diff_pixels": int(diff.sum()), "line_pixels": int(lit.sum())})
    diffs = [r["diff_pixels"] for r in rows]
    res["render_1280x720"] = {
        "views": len(rows), "m4_median_ms": st.median(r["m4_ms"] for r in rows),
        "all_points_median_ms": st.median(r["all_ms"] for r in rows),
        "full_range_m4_ms": rows[0]["m4_ms"], "full_range_all_ms": rows[0]["all_ms"],
        "speedup_median": st.median(r["all_ms"] / r["m4_ms"] for r in rows),
        "diff_pixels_median": st.median(diffs), "diff_pixels_max": max(diffs),
        "diff_share_of_image_max": max(diffs) / (W * H),
        "diff_share_of_line_pixels_median": st.median(r["diff_pixels"] / max(1, r["line_pixels"]) for r in rows),
    }

    # 4. baseline: matplotlib drawing all 6.8M points (its default path simplification is on)
    x = np.arange(len(s), dtype=np.float64)
    y = s.close() / s.scale
    mpl = []
    for _ in range(3):
        fig = plt.figure(figsize=(W / 100, H / 100), dpi=100)
        ax = fig.add_axes([0, 0, 1, 1])
        ax.plot(x, y, lw=0.6, color="k")
        t = time.perf_counter(); fig.canvas.draw(); mpl.append((time.perf_counter() - t) * 1000)
        plt.close(fig)
    t = time.perf_counter(); fc.frame_stats(s, s.t0, s.t_end, W); off.render(s, s.t0, s.t_end, "auto")
    ours = (time.perf_counter() - t) * 1000
    res["matplotlib_full_range"] = {"matplotlib_ms": st.median(mpl), "fastchart_ms": ours,
                                    "speedup": st.median(mpl) / ours}

    (ROOT / "reports" / "benchmarks.json").write_text(json.dumps(res, indent=2))
    print(json.dumps(res, indent=2))

    # ---------- charts ----------
    fig, ax = plt.subplots(figsize=(7, 2.6))
    ax.barh(["pandas DataFrame", "fastchart"], [pb / 1e6, eb / 1e6], color=[LIGHT, INK])
    for i, v in enumerate([pb / 1e6, eb / 1e6]):
        ax.text(v + 4, i, f"{v:.0f} MB", va="center")
    ax.set_xlabel("Memory for 6.85 million OHLCV rows (MB)")
    ax.set_title(f"{res['memory']['saving']:.0%} less memory than pandas", loc="left")
    ax.set_xlim(0, pb / 1e6 * 1.18)
    fig.tight_layout(); fig.savefig(FIG / "memory.png", facecolor="white"); plt.close(fig)

    fig, ax = plt.subplots(figsize=(7, 3.2))
    labels = ["matplotlib\n(all points)", "OpenGL,\nevery point", "fastchart\n(M4)"]
    vals = [res["matplotlib_full_range"]["matplotlib_ms"], res["render_1280x720"]["full_range_all_ms"],
            res["matplotlib_full_range"]["fastchart_ms"]]
    ax.bar(labels, vals, color=[LIGHT, GREY, INK])
    ax.set_yscale("log")
    ax.set_ylim(top=max(vals) * 4)
    for i, v in enumerate(vals):
        ax.text(i, v * 1.15, f"{v:,.0f} ms" if v >= 10 else f"{v:.1f} ms", ha="center")
    ax.set_ylabel("Time to draw the full 13 years (ms, log scale)")
    ax.set_title("Drawing all 6.85M points once, 1280×720, CPU only", loc="left")
    fig.tight_layout(); fig.savefig(FIG / "render_time.png", facecolor="white"); plt.close(fig)

    fig, ax = plt.subplots(figsize=(7, 3.2))
    ax.scatter([x["samples"] for x in line], [x["vertices"] for x in line], s=8, color=INK, label="points sent to the GPU")
    lim = [min(x["samples"] for x in line), len(s)]
    ax.plot(lim, lim, color=GREY, ls="--", lw=1, label="drawing every point")
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlabel("Prices in the view"); ax.set_ylabel("Points drawn")
    ax.set_title("The work stays flat (~4 per pixel column) however much data is in view", loc="left")
    ax.legend(frameon=False)
    fig.tight_layout(); fig.savefig(FIG / "points_drawn.png", facecolor="white"); plt.close(fig)

    # ---------- screenshots rendered by the engine itself ----------
    shots = [("full", s.t0, s.t_end), ("crash_2021", 1621382400 + 12 * 3600, 1621382400 + 15 * 3600)]
    for name, a, b in shots:
        img, _, _ = off.render(s, a, b, "auto", True)
        Image.fromarray(img[..., :3]).save(FIG / f"engine_{name}.png")
    print("saved charts to", FIG)


if __name__ == "__main__":
    main()
