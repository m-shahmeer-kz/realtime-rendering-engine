"""Download the Bitstamp BTC/USD 1-minute history (6,847,200 rows, about 90 MB).

Source: https://github.com/ff137/bitstamp-btcusd-minute-data (only the one file is fetched).
Data licence: Zielak (mczielinski), "Bitcoin Historical Data", Kaggle, CC BY-SA 4.0.

Usage:  python scripts/download_data.py
"""
import shutil
import subprocess
import tempfile
from pathlib import Path

REPO = "https://github.com/ff137/bitstamp-btcusd-minute-data"
FILE = "data/historical/btcusd_bitstamp_1min_2012-2025.csv.gz"


def main() -> None:
    dest = Path(__file__).resolve().parents[1] / "data" / Path(FILE).name
    if dest.exists():
        print("Already downloaded:", dest)
        return
    dest.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        run = lambda *a: subprocess.run(["git", *a], cwd=tmp, check=True, capture_output=True)  # noqa: E731
        subprocess.run(["git", "clone", "--filter=blob:none", "--no-checkout", "--depth", "1", REPO, tmp],
                       check=True, capture_output=True)
        run("sparse-checkout", "set", "--no-cone", "/" + FILE)
        run("checkout", "HEAD")
        shutil.copy(Path(tmp) / FILE, dest)
    print("Saved", dest)


if __name__ == "__main__":
    main()
