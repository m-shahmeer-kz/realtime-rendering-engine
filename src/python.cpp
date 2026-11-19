// Python bindings (pybind11) + headless OpenGL rendering through OSMesa.
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <chrono>
#include <cstring>
#include <stdexcept>

#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/osmesa.h>

#include "fastchart/renderer.hpp"
#include "fastchart/series.hpp"

namespace py = pybind11;
using namespace fastchart;

namespace {
template <typename T>
std::vector<T> to_vec(const py::object& o) {
    if (o.is_none()) return {};
    auto a = py::array_t<T, py::array::c_style | py::array::forcecast>::ensure(o);
    if (!a) throw std::invalid_argument("expected a 1-D array");
    return std::vector<T>(a.data(), a.data() + a.size());
}

py::array_t<float> points_array(const std::vector<Point>& pts) {
    py::array_t<float> out({static_cast<py::ssize_t>(pts.size()), static_cast<py::ssize_t>(2)});
    auto m = out.mutable_unchecked<2>();
    for (size_t i = 0; i < pts.size(); ++i) { m(i, 0) = pts[i].x; m(i, 1) = static_cast<float>(pts[i].y); }
    return out;
}

// One headless OpenGL 3.3 core context (Mesa llvmpipe, runs on the CPU).
class Offscreen {
public:
    Offscreen(int w, int h) : w_(w), h_(h), buf_(static_cast<size_t>(w) * h * 4) {
        const int attribs[] = {OSMESA_FORMAT, OSMESA_RGBA, OSMESA_DEPTH_BITS, 0,
                               OSMESA_PROFILE, OSMESA_CORE_PROFILE,
                               OSMESA_CONTEXT_MAJOR_VERSION, 3, OSMESA_CONTEXT_MINOR_VERSION, 3, 0};
        ctx_ = OSMesaCreateContextAttribs(attribs, nullptr);
        if (!ctx_) throw std::runtime_error("OSMesa could not create an OpenGL 3.3 context");
        if (!OSMesaMakeCurrent(ctx_, buf_.data(), GL_UNSIGNED_BYTE, w_, h_))
            throw std::runtime_error("OSMesaMakeCurrent failed");
        if (!renderer_.init()) throw std::runtime_error(renderer_.error());
    }
    ~Offscreen() {
        OSMesaMakeCurrent(ctx_, buf_.data(), GL_UNSIGNED_BYTE, w_, h_);
        renderer_.destroy();
        OSMesaDestroyContext(ctx_);
    }
    // Draw a frame; returns (image HxWx4 uint8, milliseconds for GPU work incl. glFinish)
    py::tuple render(const Frame& f, bool grid) {
        OSMesaMakeCurrent(ctx_, buf_.data(), GL_UNSIGNED_BYTE, w_, h_);
        auto t = std::chrono::steady_clock::now();
        renderer_.draw(f, w_, h_, grid);
        glFinish();
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
        py::array_t<uint8_t> img({h_, w_, 4});
        auto* dst = img.mutable_data();
        for (int y = 0; y < h_; ++y)  // OSMesa is bottom-up; flip to top-down
            std::memcpy(dst + static_cast<size_t>(y) * w_ * 4, buf_.data() + static_cast<size_t>(h_ - 1 - y) * w_ * 4, w_ * 4);
        return py::make_tuple(img, ms);
    }
    int width() const { return w_; }
    int height() const { return h_; }

private:
    int w_, h_;
    std::vector<uint8_t> buf_;
    OSMesaContext ctx_ = nullptr;
    Renderer renderer_;
};
}  // namespace

PYBIND11_MODULE(_fastchart, m) {
    m.doc() = "fastchart C++ engine";

    py::class_<Series>(m, "Series")
        .def(py::init([](int64_t t0, int64_t step, int32_t scale, py::object close, py::object open,
                         py::object high, py::object low, py::object volume) {
                 return Series(t0, step, scale, to_vec<int32_t>(close), to_vec<int32_t>(open), to_vec<int32_t>(high),
                               to_vec<int32_t>(low), to_vec<float>(volume));
             }),
             py::arg("t0"), py::arg("step"), py::arg("scale"), py::arg("close"), py::arg("open") = py::none(),
             py::arg("high") = py::none(), py::arg("low") = py::none(), py::arg("volume") = py::none())
        .def("__len__", &Series::size)
        .def_property_readonly("t0", &Series::t0)
        .def_property_readonly("t_end", &Series::t_end)
        .def_property_readonly("step", &Series::step)
        .def_property_readonly("scale", &Series::scale)
        .def_property_readonly("has_ohlc", &Series::has_ohlc)
        .def("memory_bytes", &Series::memory_bytes)
        .def("pyramid_bytes", &Series::pyramid_bytes)
        .def("index_at", &Series::index_at)
        .def("range_minmax", [](const Series& s, size_t a, size_t b) {
            MinMax r = s.range_minmax(a, b);
            return py::make_tuple(s.price(r.min), s.price(r.max), r.imin, r.imax);
        })
        .def("range_minmax_scan", [](const Series& s, size_t a, size_t b) {
            MinMax r = s.range_minmax_scan(a, b);
            return py::make_tuple(s.price(r.min), s.price(r.max), r.imin, r.imax);
        })
        .def("close", [](const Series& s) {
            py::array_t<int32_t> a(static_cast<py::ssize_t>(s.size()));
            std::memcpy(a.mutable_data(), s.close().data(), s.size() * 4);
            return a;
        })
        .def("save", &Series::save)
        .def_static("load", &Series::load);

    m.def("m4", [](const Series& s, double a, double b, int w) { return points_array(m4(s, a, b, w)); },
          "M4 points (x pixels, price) for a view", py::arg("series"), py::arg("t_from"), py::arg("t_to"), py::arg("width"));
    m.def("all_points", [](const Series& s, double a, double b, int w) { return points_array(all_points(s, a, b, w)); },
          "Every sample in the view (reference)");
    m.def("candles", [](const Series& s, double a, double b, int w, int px, size_t min_samples) {
        auto cs = candles(s, a, b, w, px, min_samples);
        py::array_t<double> out({static_cast<py::ssize_t>(cs.size()), static_cast<py::ssize_t>(6)});
        auto mm = out.mutable_unchecked<2>();
        for (size_t i = 0; i < cs.size(); ++i) {
            mm(i, 0) = cs[i].x0; mm(i, 1) = cs[i].x1; mm(i, 2) = cs[i].open;
            mm(i, 3) = cs[i].high; mm(i, 4) = cs[i].low; mm(i, 5) = cs[i].close;
        }
        return out;
    }, py::arg("series"), py::arg("t_from"), py::arg("t_to"), py::arg("width"), py::arg("px_per_candle") = 8,
       py::arg("min_samples") = 1);

    m.def("frame_stats", [](const Series& s, double a, double b, int w) {
        auto t = std::chrono::steady_clock::now();
        Frame f = make_frame(s, a, b, w);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
        py::dict d;
        d["candles"] = f.candles; d["samples"] = f.samples_in_view; d["vertices"] = f.vertices;
        d["ymin"] = f.ymin; d["ymax"] = f.ymax; d["cpu_ms"] = ms;
        return d;
    });

    py::class_<Offscreen>(m, "Offscreen")
        .def(py::init<int, int>(), py::arg("width"), py::arg("height"))
        .def_property_readonly("width", &Offscreen::width)
        .def_property_readonly("height", &Offscreen::height)
        .def("render", [](Offscreen& o, const Series& s, double a, double b, const std::string& mode, bool grid) {
            Frame f;
            if (mode == "auto") {
                f = make_frame(s, a, b, o.width());
            } else {
                // force a line drawn from M4 points or from every sample, with the same y range
                f = make_frame(s, a, b, o.width(), 0);
                if (mode == "all") { f.line = all_points(s, a, b, o.width()); f.vertices = f.line.size(); }
                else if (mode != "m4") throw std::invalid_argument("mode must be 'auto', 'm4' or 'all'");
            }
            py::tuple r = o.render(f, grid);
            return py::make_tuple(r[0], r[1], f.vertices);
        }, py::arg("series"), py::arg("t_from"), py::arg("t_to"), py::arg("mode") = "auto", py::arg("grid") = false,
           "Render a view headlessly. Returns (RGBA image, render ms, vertices).");
}
