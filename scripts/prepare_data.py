"""Turn the downloaded CSV into the engine's files.

  data/btcusd.fcbin              full OHLCV, used by the desktop app and the benchmarks
  docs/data/btc_close.bin.gz     close prices only, compressed, used by the browser demo

Web file format (little endian): magic 'FCW1' | n u32 | t0 f64 | step f64 | scale i32 | first i32 |
then n-1 price changes in cents as zig-zag varints; the whole file is gzipped.
"""
import gzip
import json
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
import fastchart as fc  # noqa: E402

CSV = ROOT / "data" / "btcusd_bitstamp_1min_2012-2025.csv.gz"


def zigzag_varints(d: np.ndarray) -> bytes:
    z = ((d << 1) ^ (d >> 63)).astype(np.uint64)  # zig-zag: small +/- numbers -> small unsigned
    out = bytearray()
    for v in z.tolist():
        while v >= 0x80:
            out.append((v & 0x7F) | 0x80)
            v >>= 7
        out.append(v)
    return bytes(out)


def main() -> None:
    df = fc.load_bitstamp(str(CSV))
    s, report = fc.from_dataframe(df)
    s.save(str(ROOT / "data" / "btcusd.fcbin"))
    report.update(engine_bytes=s.memory_bytes(), pandas_bytes=fc.pandas_bytes(df))
    print(json.dumps(report, indent=2))

    close = s.close().astype(np.int64)
    header = (b"FCW1" + np.uint32(len(close)).tobytes() + np.float64(s.t0).tobytes() + np.float64(s.step).tobytes()
              + np.int32(s.scale).tobytes() + np.int32(close[0]).tobytes())
    body = zigzag_varints(np.diff(close))
    out = ROOT / "docs" / "data" / "btc_close.bin.gz"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(gzip.compress(header + body, compresslevel=9))
    print(f"web file: {out.stat().st_size / 1e6:.1f} MB for {len(close):,} prices (raw int32: {len(close) * 4 / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
