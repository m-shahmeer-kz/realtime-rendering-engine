// OpenGL renderer for fastchart frames (OpenGL 3.3 core on desktop, WebGL 2 in the browser).
#pragma once

#include "fastchart/series.hpp"

namespace fastchart {

struct Theme {
    float bg[4] = {0.031f, 0.031f, 0.039f, 1.0f};
    float grid[4] = {1.0f, 1.0f, 1.0f, 0.07f};
    float line[4] = {0.95f, 0.95f, 0.955f, 1.0f};
    float up[4] = {0.93f, 0.93f, 0.94f, 1.0f};
    float down[4] = {0.42f, 0.42f, 0.45f, 1.0f};
};

class Renderer {
public:
    // Needs a current OpenGL context. Returns false if shaders fail to compile.
    bool init();
    // Draw `f` into a viewport of w x h pixels (x coordinates in the frame are in pixels).
    void draw(const Frame& f, int w, int h, bool grid = true);
    void destroy();
    ~Renderer() { destroy(); }
    const char* error() const { return error_; }

private:
    void upload_and_draw(const float* xy, int count, unsigned mode, const float* color);
    unsigned prog_ = 0, vao_ = 0, vbo_ = 0;
    int u_scale_ = -1, u_offset_ = -1, u_color_ = -1;
    const char* error_ = "";
    Theme theme_;
};

}  // namespace fastchart
