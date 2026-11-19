// Small self-checking test program for the C++ core (run by CTest).
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

#include "fastchart/series.hpp"

using namespace fastchart;

static int failures = 0;
#define CHECK(cond)                                                   \
    do {                                                              \
        if (!(cond)) {                                                \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                               \
        }                                                             \
    } while (0)

static Series random_walk(size_t n, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> d(0, 50);
    std::vector<int32_t> c(n);
    double v = 1000000;
    for (auto& x : c) { v += d(rng); x = static_cast<int32_t>(v); }
    return Series(1600000000, 60, 100, std::move(c));
}

int main() {
    // 1. pyramid min/max equals a full scan for many random ranges
    for (size_t n : {1u, 15u, 16u, 17u, 255u, 4096u, 100003u}) {
        Series s = random_walk(n, 1);
        std::mt19937 rng(7);
        for (int k = 0; k < 2000; ++k) {
            size_t a = rng() % n, b = a + 1 + rng() % (n - a);
            MinMax p = s.range_minmax(a, b), q = s.range_minmax_scan(a, b);
            CHECK(p.min == q.min && p.max == q.max);
            CHECK(s.close()[p.imin] == p.min && s.close()[p.imax] == p.max);
            CHECK(p.imin >= a && p.imin < b && p.imax >= a && p.imax < b);
        }
    }

    // 2. M4 keeps the min and max of every pixel column and at most 4 points per column
    {
        Series s = random_walk(200000, 3);
        double a = s.t0(), b = s.t_end();
        auto pts = m4(s, a, b, 800);
        auto all = all_points(s, a, b, 800);
        CHECK(pts.size() <= 800 * 4 + 2);
        CHECK(all.size() == s.size());
        double lo = 1e300, hi = -1e300, lo2 = 1e300, hi2 = -1e300;
        for (auto& p : pts) { lo = std::min(lo, p.y); hi = std::max(hi, p.y); }
        for (auto& p : all) { lo2 = std::min(lo2, p.y); hi2 = std::max(hi2, p.y); }
        CHECK(lo == lo2 && hi == hi2);
        for (size_t i = 1; i < pts.size(); ++i) CHECK(pts[i].x >= pts[i - 1].x);  // in time order
    }

    // 3. candles: open/close/high/low match the raw data of each bucket
    {
        std::vector<int32_t> o{100, 110, 120, 130}, h{150, 160, 170, 180}, l{90, 95, 99, 98}, c{110, 120, 130, 125};
        Series s(0, 60, 100, c, o, h, l);
        auto cs = candles(s, 0, 240, 16, 8);  // 2 slots -> 2 samples per candle
        CHECK(cs.size() == 2);
        CHECK(cs[0].open == 1.00 && cs[0].close == 1.20 && cs[0].high == 1.60 && cs[0].low == 0.90);
        CHECK(cs[1].open == 1.20 && cs[1].close == 1.25 && cs[1].high == 1.80 && cs[1].low == 0.98);
    }

    // 4. save / load round trip
    {
        Series s = random_walk(1000, 5);
        std::string path = "test_roundtrip.fcbin";
        s.save(path);
        Series t = Series::load(path);
        CHECK(t.size() == s.size() && t.t0() == s.t0() && t.step() == s.step() && t.close() == s.close());
        std::remove(path.c_str());
    }

    // 5. memory: 4 bytes per close value + a pyramid of about 1.07 bytes per sample
    {
        Series s = random_walk(1 << 20, 9);
        CHECK(s.pyramid_bytes() < (1u << 20) * 1.1);
        CHECK(s.memory_bytes() == (1u << 20) * 4 + s.pyramid_bytes());
    }

    std::printf(failures ? "%d test(s) FAILED\n" : "all C++ tests passed\n", failures);
    return failures ? 1 : 0;
}
