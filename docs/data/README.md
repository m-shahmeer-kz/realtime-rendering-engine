# Data used by the browser demo

`btc_close.bin.gz` holds the 6,847,200 BTC/USD minute close prices from Bitstamp (1 Jan 2012 – 7 Jan 2025).

- Source: Zielak (mczielinski), "Bitcoin Historical Data", Kaggle, licensed under [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/), obtained via [ff137/bitstamp-btcusd-minute-data](https://github.com/ff137/bitstamp-btcusd-minute-data).
- Changes: only the close column is kept, prices are rounded to whole cents and stored as minute-to-minute changes (zig-zag varints), then gzipped. The format is described in `scripts/prepare_data.py`.
- This file is shared under the same licence, CC BY-SA 4.0.
