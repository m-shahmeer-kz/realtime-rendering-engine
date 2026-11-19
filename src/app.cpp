// Interactive chart window. The same file builds the desktop app (GLFW + OpenGL 3.3)
// and the browser demo (Emscripten: GLFW -> canvas, OpenGL ES 3 -> WebGL 2).
//
// Controls: mouse wheel = zoom, drag = pan, R or double-click = reset.
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "fastchart/renderer.hpp"
#include "fastchart/series.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define FC_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#endif

using namespace fastchart;

namespace {
struct App {
    GLFWwindow* win = nullptr;
    Renderer renderer;
    std::unique_ptr<Series> series;
    Frame frame;
    double t_from = 0, t_to = 1;
    bool dirty = true, dragging = false;
    double drag_x = 0, drag_from = 0, drag_to = 0;
    double last_click = -1;
    double cpu_ms = 0, frame_ms = 0;
    int w = 1, h = 1;
    std::chrono::steady_clock::time_point last;
} app;

using clk = std::chrono::steady_clock;

void reset_view() {
    if (!app.series) return;
    app.t_from = static_cast<double>(app.series->t0());
    app.t_to = static_cast<double>(app.series->t_end());
    app.dirty = true;
}

void clamp_view() {
    const Series& s = *app.series;
    double full = static_cast<double>(s.t_end() - s.t0());
    double span = std::clamp(app.t_to - app.t_from, 30.0 * s.step(), full * 1.02);
    double lo = s.t0() - full * 0.01, hi = s.t_end() + full * 0.01;
    double a = std::clamp(app.t_from, lo, hi - span);
    app.t_from = a;
    app.t_to = a + span;
}

void zoom(double factor, double at_x) {
    double t = app.t_from + (app.t_to - app.t_from) * (at_x / app.w);
    app.t_from = t - (t - app.t_from) * factor;
    app.t_to = t + (app.t_to - t) * factor;
    clamp_view();
    app.dirty = true;
}

void on_scroll(GLFWwindow*, double, double dy) {
    double x, y;
    glfwGetCursorPos(app.win, &x, &y);
    zoom(std::pow(0.85, dy), x);
}

void on_button(GLFWwindow*, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    double x, y;
    glfwGetCursorPos(app.win, &x, &y);
    if (action == GLFW_PRESS) {
        double now = glfwGetTime();
        if (now - app.last_click < 0.3) reset_view();
        app.last_click = now;
        app.dragging = true;
        app.drag_x = x;
        app.drag_from = app.t_from;
        app.drag_to = app.t_to;
    } else {
        app.dragging = false;
    }
}

void on_move(GLFWwindow*, double x, double) {
    if (!app.dragging) return;
    double dt = (x - app.drag_x) / app.w * (app.drag_to - app.drag_from);
    app.t_from = app.drag_from - dt;
    app.t_to = app.drag_to - dt;
    clamp_view();
    app.dirty = true;
}

void on_key(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_R) reset_view();
    if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) zoom(0.7, app.w / 2.0);
    if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) zoom(1 / 0.7, app.w / 2.0);
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, 1);
}

void tick() {
    int fbw, fbh;
    glfwGetFramebufferSize(app.win, &fbw, &fbh);
    if (fbw != app.w || fbh != app.h) { app.w = std::max(1, fbw); app.h = std::max(1, fbh); app.dirty = true; }
    if (app.series && app.dirty) {
        auto t = clk::now();
        app.frame = make_frame(*app.series, app.t_from, app.t_to, app.w);
        app.cpu_ms = std::chrono::duration<double, std::milli>(clk::now() - t).count();
        app.dirty = false;
    }
    app.renderer.draw(app.frame, app.w, app.h);
    glfwSwapBuffers(app.win);
    glfwPollEvents();
    auto now = clk::now();
    app.frame_ms = 0.9 * app.frame_ms + 0.1 * std::chrono::duration<double, std::milli>(now - app.last).count();
    app.last = now;
#if !defined(__EMSCRIPTEN__)
    static int n = 0;
    if (++n % 30 == 0) {
        char title[200];
        std::snprintf(title, sizeof title, "fastchart | %zu samples in view | %zu vertices | view %.2f ms | %.0f fps",
                      app.frame.samples_in_view, app.frame.vertices, app.cpu_ms, 1000.0 / std::max(0.01, app.frame_ms));
        glfwSetWindowTitle(app.win, title);
    }
#endif
}

bool open_window(int w, int h) {
    if (!glfwInit()) return false;
#if defined(__EMSCRIPTEN__)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    app.win = glfwCreateWindow(w, h, "fastchart", nullptr, nullptr);
    if (!app.win) return false;
    glfwMakeContextCurrent(app.win);
    glfwSwapInterval(1);
#if !defined(__EMSCRIPTEN__)
    glfwSetScrollCallback(app.win, on_scroll);  // the web page handles the wheel itself (see web/index.html)
#endif
    glfwSetMouseButtonCallback(app.win, on_button);
    glfwSetCursorPosCallback(app.win, on_move);
    glfwSetKeyCallback(app.win, on_key);
    app.last = clk::now();
    return app.renderer.init();
}
}  // namespace

#if defined(__EMSCRIPTEN__)
// ---- functions called from JavaScript ----
FC_EXPORT int fc_start(int w, int h) { return open_window(w, h) ? 1 : 0; }
FC_EXPORT void fc_set_size(int w, int h) { glfwSetWindowSize(app.win, w, h); app.dirty = true; }
// close: int32 fixed-point prices (price * scale), one every `step` seconds from t0
FC_EXPORT int fc_load(const int32_t* close, int n, double t0, double step, int scale) {
    std::vector<int32_t> c(close, close + n);
    app.series = std::make_unique<Series>(static_cast<int64_t>(t0), static_cast<int64_t>(step), scale, std::move(c));
    reset_view();
    emscripten_set_main_loop(tick, 0, 0);
    return 1;
}
FC_EXPORT double fc_view_from() { return app.t_from; }
FC_EXPORT double fc_view_to() { return app.t_to; }
FC_EXPORT double fc_ymin() { return app.frame.ymin; }
FC_EXPORT double fc_ymax() { return app.frame.ymax; }
FC_EXPORT double fc_samples() { return static_cast<double>(app.frame.samples_in_view); }
FC_EXPORT double fc_vertices() { return static_cast<double>(app.frame.vertices); }
FC_EXPORT double fc_cpu_ms() { return app.cpu_ms; }
FC_EXPORT double fc_frame_ms() { return app.frame_ms; }
FC_EXPORT int fc_candles() { return app.frame.candles ? 1 : 0; }
FC_EXPORT double fc_memory() { return app.series ? static_cast<double>(app.series->memory_bytes()) : 0; }
FC_EXPORT void fc_zoom(double factor) { zoom(factor, app.w / 2.0); }
FC_EXPORT void fc_zoom_at(double factor, double x) { zoom(factor, x); }
FC_EXPORT void fc_set_view(double a, double b) { app.t_from = a; app.t_to = b; clamp_view(); app.dirty = true; }
int main() { return 0; }
#else
int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "data/btcusd.fcbin";
    try {
        app.series = std::make_unique<Series>(Series::load(path));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\nRun: python scripts/prepare_data.py   (creates %s)\n", e.what(), path);
        return 1;
    }
    std::printf("Loaded %zu samples (%.1f MB in memory)\n", app.series->size(), app.series->memory_bytes() / 1e6);
    if (!open_window(1280, 720)) { std::fprintf(stderr, "Could not open an OpenGL 3.3 window\n"); return 1; }
    reset_view();
    while (!glfwWindowShouldClose(app.win)) tick();
    app.renderer.destroy();
    glfwTerminate();
    return 0;
}
#endif
