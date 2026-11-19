// fastchart: a compact time series store with a min/max pyramid for fast chart views.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fastchart {

// Min and max of a range of samples, and where they happen (sample index).
struct MinMax {
    int32_t min = INT32_MAX;
    int32_t max = INT32_MIN;
    uint32_t imin = 0;
    uint32_t imax = 0;
};

// A point ready to draw: x in pixels inside the view, y as a price.
struct Point {
    float x;
    double y;
};

// One candlestick spanning [x0, x1) pixels.
struct Candle {
    float x0, x1;
    double open, high, low, close;
};

// Regular time series (one sample every `step` seconds) with OHLC prices stored as
// 32-bit fixed-point integers (price * scale) and an optional float32 volume.
//
// Memory per sample: 16 bytes (OHLC) + 4 (volume) + about 1 byte for the pyramid,
// against 48 bytes for the same data in a pandas DataFrame (int64 time + 5 float64).
class Series {
public:
    static constexpr size_t kBlock = 16;  // samples per pyramid block

    Series() = default;
    Series(int64_t t0, int64_t step, int32_t scale, std::vector<int32_t> close,
           std::vector<int32_t> open = {}, std::vector<int32_t> high = {},
           std::vector<int32_t> low = {}, std::vector<float> volume = {});

    size_t size() const { return close_.size(); }
    int64_t t0() const { return t0_; }
    int64_t step() const { return step_; }
    int32_t scale() const { return scale_; }
    int64_t t_end() const { return t0_ + static_cast<int64_t>(size()) * step_; }
    double time(size_t i) const { return static_cast<double>(t0_) + static_cast<double>(i) * step_; }
    bool has_ohlc() const { return !open_.empty(); }

    double price(int32_t fixed) const { return static_cast<double>(fixed) / scale_; }
    const std::vector<int32_t>& close() const { return close_; }
    const std::vector<int32_t>& open() const { return open_; }
    const std::vector<int32_t>& high() const { return high_; }
    const std::vector<int32_t>& low() const { return low_; }
    const std::vector<float>& volume() const { return volume_; }

    // Bytes used by the stored data plus the pyramid.
    size_t memory_bytes() const;
    size_t pyramid_bytes() const;

    // First sample index whose time is >= t (clamped to [0, size]).
    size_t index_at(double t) const;

    // Min/max of close over samples [i0, i1), answered with the pyramid in O(log n).
    MinMax range_minmax(size_t i0, size_t i1) const;

    // Same answer, by scanning every sample (used by tests and benchmarks).
    MinMax range_minmax_scan(size_t i0, size_t i1) const;

    // Save / load the compact binary format (.fcbin).
    void save(const std::string& path) const;
    static Series load(const std::string& path);

private:
    void build_pyramid();
    void query(int level, size_t a, size_t b, MinMax& r) const;

    int64_t t0_ = 0;
    int64_t step_ = 60;
    int32_t scale_ = 100;
    std::vector<int32_t> open_, high_, low_, close_;
    std::vector<float> volume_;
    std::vector<std::vector<MinMax>> levels_;  // levels_[k] block = kBlock items of level k-1
};

// ---------------- chart views ----------------

// What the view needs to draw: either a line (M4 points) or candlesticks.
struct Frame {
    bool candles = false;
    std::vector<Point> line;       // line strip, x in pixels
    std::vector<Candle> bars;      // candlesticks
    double ymin = 0, ymax = 1;     // price range in view (with padding)
    size_t samples_in_view = 0;    // raw samples covered by the view
    size_t vertices = 0;           // vertices sent to the GPU
};

// M4 aggregation (Jugel et al., VLDB 2014): for every pixel column keep only the
// first, last, min and max sample. Drawing these as a line gives the same pixels as
// drawing every sample, while sending at most 4 points per pixel column.
std::vector<Point> m4(const Series& s, double t_from, double t_to, int width);

// Every sample in the view (the slow reference).
std::vector<Point> all_points(const Series& s, double t_from, double t_to, int width);

// Candles aggregated so that each one is about `px_per_candle` pixels wide and covers
// at least `min_samples` samples.
std::vector<Candle> candles(const Series& s, double t_from, double t_to, int width, int px_per_candle = 8,
                            size_t min_samples = 1);

// Build what to draw for a view. Shows candles when zoomed in enough, otherwise an M4 line.
Frame make_frame(const Series& s, double t_from, double t_to, int width, size_t candle_limit = 200000);

}  // namespace fastchart
