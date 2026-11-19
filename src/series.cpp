#include "fastchart/series.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace fastchart {

namespace {
constexpr char kMagic[8] = {'F', 'C', 'H', 'A', 'R', 'T', '0', '1'};

inline void take(MinMax& r, int32_t v, uint32_t i) {
    if (v < r.min) { r.min = v; r.imin = i; }
    if (v > r.max) { r.max = v; r.imax = i; }
}
inline void take(MinMax& r, const MinMax& b) {
    if (b.min < r.min) { r.min = b.min; r.imin = b.imin; }
    if (b.max > r.max) { r.max = b.max; r.imax = b.imax; }
}
}  // namespace

Series::Series(int64_t t0, int64_t step, int32_t scale, std::vector<int32_t> close,
               std::vector<int32_t> open, std::vector<int32_t> high, std::vector<int32_t> low,
               std::vector<float> volume)
    : t0_(t0), step_(step), scale_(scale), open_(std::move(open)), high_(std::move(high)),
      low_(std::move(low)), close_(std::move(close)), volume_(std::move(volume)) {
    if (step_ <= 0) throw std::invalid_argument("step must be positive");
    if (scale_ <= 0) throw std::invalid_argument("scale must be positive");
    const size_t n = close_.size();
    if (n > UINT32_MAX) throw std::invalid_argument("too many samples");
    bool any = !open_.empty() || !high_.empty() || !low_.empty();
    if (any && (open_.size() != n || high_.size() != n || low_.size() != n))
        throw std::invalid_argument("open/high/low must all have the same length as close");
    if (!volume_.empty() && volume_.size() != n) throw std::invalid_argument("volume length mismatch");
    build_pyramid();
}

void Series::build_pyramid() {
    levels_.clear();
    size_t n = close_.size();
    if (n < kBlock) return;
    // level 0: blocks of raw samples
    std::vector<MinMax> lvl((n + kBlock - 1) / kBlock);
    for (size_t b = 0; b < lvl.size(); ++b) {
        MinMax r;
        size_t end = std::min(n, (b + 1) * kBlock);
        for (size_t i = b * kBlock; i < end; ++i) take(r, close_[i], static_cast<uint32_t>(i));
        lvl[b] = r;
    }
    levels_.push_back(std::move(lvl));
    while (levels_.back().size() >= kBlock) {
        const auto& prev = levels_.back();
        std::vector<MinMax> up((prev.size() + kBlock - 1) / kBlock);
        for (size_t b = 0; b < up.size(); ++b) {
            MinMax r;
            size_t end = std::min(prev.size(), (b + 1) * kBlock);
            for (size_t i = b * kBlock; i < end; ++i) take(r, prev[i]);
            up[b] = r;
        }
        levels_.push_back(std::move(up));
    }
}

size_t Series::pyramid_bytes() const {
    size_t total = 0;
    for (const auto& l : levels_) total += l.size() * sizeof(MinMax);
    return total;
}

size_t Series::memory_bytes() const {
    return (open_.size() + high_.size() + low_.size() + close_.size()) * sizeof(int32_t) +
           volume_.size() * sizeof(float) + pyramid_bytes();
}

size_t Series::index_at(double t) const {
    double k = std::ceil((t - static_cast<double>(t0_)) / static_cast<double>(step_));
    if (k <= 0) return 0;
    if (k >= static_cast<double>(size())) return size();
    return static_cast<size_t>(k);
}

// a, b are unit indices at `level` (-1 = raw samples, k = blocks of levels_[k]).
void Series::query(int level, size_t a, size_t b, MinMax& r) const {
    if (a >= b) return;
    auto unit = [&](size_t u) {
        if (level < 0) take(r, close_[u], static_cast<uint32_t>(u));
        else take(r, levels_[level][u]);
    };
    const int next = level + 1;
    size_t ka = (a + kBlock - 1) / kBlock, kb = b / kBlock;  // whole blocks of the next level
    if (next >= static_cast<int>(levels_.size()) || ka >= kb) {
        for (size_t u = a; u < b; ++u) unit(u);
        return;
    }
    for (size_t u = a; u < ka * kBlock; ++u) unit(u);
    query(next, ka, kb, r);
    for (size_t u = kb * kBlock; u < b; ++u) unit(u);
}

MinMax Series::range_minmax(size_t i0, size_t i1) const {
    MinMax r;
    i1 = std::min(i1, size());
    query(-1, i0, i1, r);
    return r;
}

MinMax Series::range_minmax_scan(size_t i0, size_t i1) const {
    MinMax r;
    i1 = std::min(i1, size());
    for (size_t i = i0; i < i1; ++i) take(r, close_[i], static_cast<uint32_t>(i));
    return r;
}

// ---------------- file format ----------------
// magic[8] | n u64 | t0 i64 | step i64 | scale i32 | flags u32 (1=ohlc, 2=volume) | columns...
void Series::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path);
    uint64_t n = size();
    uint32_t flags = (has_ohlc() ? 1u : 0u) | (volume_.empty() ? 0u : 2u);
    f.write(kMagic, 8);
    f.write(reinterpret_cast<const char*>(&n), 8);
    f.write(reinterpret_cast<const char*>(&t0_), 8);
    f.write(reinterpret_cast<const char*>(&step_), 8);
    f.write(reinterpret_cast<const char*>(&scale_), 4);
    f.write(reinterpret_cast<const char*>(&flags), 4);
    auto put = [&](const auto& v) { f.write(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(v[0])); };
    if (has_ohlc()) { put(open_); put(high_); put(low_); }
    put(close_);
    if (!volume_.empty()) put(volume_);
}

Series Series::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read " + path);
    char magic[8];
    uint64_t n;
    int64_t t0, step;
    int32_t scale;
    uint32_t flags;
    f.read(magic, 8);
    if (std::memcmp(magic, kMagic, 8) != 0) throw std::runtime_error("not a fastchart file: " + path);
    f.read(reinterpret_cast<char*>(&n), 8);
    f.read(reinterpret_cast<char*>(&t0), 8);
    f.read(reinterpret_cast<char*>(&step), 8);
    f.read(reinterpret_cast<char*>(&scale), 4);
    f.read(reinterpret_cast<char*>(&flags), 4);
    auto get_i = [&](std::vector<int32_t>& v) { v.resize(n); f.read(reinterpret_cast<char*>(v.data()), n * 4); };
    std::vector<int32_t> o, h, l, c;
    std::vector<float> vol;
    if (flags & 1u) { get_i(o); get_i(h); get_i(l); }
    get_i(c);
    if (flags & 2u) { vol.resize(n); f.read(reinterpret_cast<char*>(vol.data()), n * 4); }
    if (!f) throw std::runtime_error("file is truncated: " + path);
    return Series(t0, step, scale, std::move(c), std::move(o), std::move(h), std::move(l), std::move(vol));
}

// ---------------- views ----------------
namespace {
struct View {
    size_t i0, i1;  // visible samples [i0, i1)
    double t_from, span, px;
};
View view_of(const Series& s, double t_from, double t_to, int width) {
    if (t_to <= t_from || width <= 0) throw std::invalid_argument("empty view");
    View v{s.index_at(t_from), s.index_at(t_to), t_from, t_to - t_from, static_cast<double>(width)};
    return v;
}
inline float xpix(const Series& s, const View& v, size_t i) {
    return static_cast<float>((s.time(i) - v.t_from) / v.span * v.px);
}
}  // namespace

std::vector<Point> m4(const Series& s, double t_from, double t_to, int width) {
    View v = view_of(s, t_from, t_to, width);
    std::vector<Point> out;
    out.reserve(static_cast<size_t>(width) * 4 + 2);
    auto add = [&](size_t i) { out.push_back({xpix(s, v, i), s.price(s.close()[i])}); };
    if (v.i0 > 0 && v.i0 <= s.size()) add(v.i0 - 1);  // line coming in from the left
    const double dt = v.span / v.px;
    size_t start = v.i0;
    for (int c = 0; c < width && start < v.i1; ++c) {
        size_t end = (c == width - 1) ? v.i1 : std::min(v.i1, s.index_at(t_from + (c + 1) * dt));
        if (end <= start) continue;
        MinMax mm = s.range_minmax(start, end);
        size_t idx[4] = {start, std::min<size_t>(mm.imin, mm.imax), std::max<size_t>(mm.imin, mm.imax), end - 1};
        size_t last = SIZE_MAX;
        for (size_t i : idx) {
            if (i != last) add(i);
            last = i;
        }
        start = end;
    }
    if (v.i1 < s.size()) add(v.i1);  // line leaving to the right
    return out;
}

std::vector<Point> all_points(const Series& s, double t_from, double t_to, int width) {
    View v = view_of(s, t_from, t_to, width);
    size_t a = v.i0 > 0 ? v.i0 - 1 : 0, b = std::min(s.size(), v.i1 + 1);
    std::vector<Point> out;
    out.reserve(b - a);
    for (size_t i = a; i < b; ++i) out.push_back({xpix(s, v, i), s.price(s.close()[i])});
    return out;
}

std::vector<Candle> candles(const Series& s, double t_from, double t_to, int width, int px_per_candle,
                            size_t min_samples) {
    View v = view_of(s, t_from, t_to, width);
    std::vector<Candle> out;
    if (v.i1 <= v.i0) return out;
    size_t n = v.i1 - v.i0;
    size_t slots = std::max<size_t>(1, static_cast<size_t>(width / std::max(1, px_per_candle)));
    size_t per = std::max<size_t>(std::max<size_t>(1, min_samples), (n + slots - 1) / slots);
    // align buckets to multiples of `per` samples so candles do not jump while panning
    size_t first = (v.i0 / per) * per;
    const auto& c = s.close();
    for (size_t a = first; a < v.i1; a += per) {
        size_t b = std::min(s.size(), a + per);
        int32_t o, h = INT32_MIN, l = INT32_MAX, cl = c[b - 1];
        if (s.has_ohlc()) {  // real candles from open/high/low/close
            o = s.open()[a];
            for (size_t i = a; i < b; ++i) { h = std::max(h, s.high()[i]); l = std::min(l, s.low()[i]); }
        } else {             // close-only data: candles built from closes
            o = c[a];
            for (size_t i = a; i < b; ++i) { h = std::max(h, c[i]); l = std::min(l, c[i]); }
        }
        out.push_back({xpix(s, v, a), xpix(s, v, b), s.price(o), s.price(h), s.price(l), s.price(cl)});
    }
    return out;
}

Frame make_frame(const Series& s, double t_from, double t_to, int width, size_t candle_limit) {
    Frame f;
    View v = view_of(s, t_from, t_to, width);
    f.samples_in_view = v.i1 > v.i0 ? v.i1 - v.i0 : 0;
    if (f.samples_in_view == 0) return f;
    double lo, hi;
    if (f.samples_in_view <= candle_limit && f.samples_in_view <= static_cast<size_t>(width) * 30) {
        f.candles = true;
        // close-only data: use at least 5 samples per candle so wicks carry information
        f.bars = candles(s, t_from, t_to, width, 8, s.has_ohlc() ? 1 : 5);
        lo = 1e300; hi = -1e300;
        for (const auto& b : f.bars) { lo = std::min(lo, b.low); hi = std::max(hi, b.high); }
        f.vertices = f.bars.size() * 10;  // 2 triangles + 2 wick vertices
    } else {
        f.line = m4(s, t_from, t_to, width);
        MinMax mm = s.range_minmax(v.i0, v.i1);
        lo = s.price(mm.min); hi = s.price(mm.max);
        f.vertices = f.line.size();
    }
    double pad = std::max(1e-9, (hi - lo) * 0.06);
    f.ymin = lo - pad;
    f.ymax = hi + pad;
    return f;
}

}  // namespace fastchart
