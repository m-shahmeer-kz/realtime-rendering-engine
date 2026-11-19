#include "fastchart/renderer.hpp"

#include <vector>

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#define FC_GLSL_HEADER "#version 300 es\nprecision highp float;\n"
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#define FC_GLSL_HEADER "#version 330 core\n"
#endif

namespace fastchart {

namespace {
const char* kVert = FC_GLSL_HEADER
    "layout(location = 0) in vec2 a_pos;\n"
    "uniform vec2 u_scale;\n"
    "uniform vec2 u_offset;\n"
    "void main() { gl_Position = vec4(a_pos * u_scale + u_offset, 0.0, 1.0); }\n";

const char* kFrag = FC_GLSL_HEADER
    "uniform vec4 u_color;\n"
    "out vec4 o_color;\n"
    "void main() { o_color = u_color; }\n";

GLuint compile(GLenum type, const char* src, bool& ok) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint status = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &status);
    ok = ok && status == GL_TRUE;
    return s;
}
}  // namespace

bool Renderer::init() {
    bool ok = true;
    GLuint vs = compile(GL_VERTEX_SHADER, kVert, ok);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFrag, ok);
    if (!ok) { error_ = "shader compile failed"; return false; }
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs);
    glAttachShader(prog_, fs);
    glLinkProgram(prog_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(prog_, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) { error_ = "shader link failed"; return false; }
    u_scale_ = glGetUniformLocation(prog_, "u_scale");
    u_offset_ = glGetUniformLocation(prog_, "u_offset");
    u_color_ = glGetUniformLocation(prog_, "u_color");
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    return true;
}

void Renderer::destroy() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (prog_) glDeleteProgram(prog_);
    vbo_ = vao_ = prog_ = 0;
}

void Renderer::upload_and_draw(const float* xy, int count, unsigned mode, const float* color) {
    if (count <= 0) return;
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(count) * 2 * sizeof(float), xy, GL_STREAM_DRAW);
    glUniform4fv(u_color_, 1, color);
    glDrawArrays(mode, 0, count);
}

void Renderer::draw(const Frame& f, int w, int h, bool grid) {
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(theme_.bg[0], theme_.bg[1], theme_.bg[2], theme_.bg[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(prog_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    // x: pixels -> clip space; y: price relative to ymin -> clip space.
    // Prices are made relative on the CPU in double precision, so float32 on the GPU
    // stays accurate even when zoomed into a few dollars at $100,000.
    const double yspan = f.ymax - f.ymin;
    glUniform2f(u_scale_, 2.0f / static_cast<float>(w), static_cast<float>(2.0 / yspan));
    glUniform2f(u_offset_, -1.0f, -1.0f);

    std::vector<float> buf;
    if (grid) {
        for (int k = 1; k < 5; ++k) {
            float y = static_cast<float>(yspan * k / 5.0);
            buf.insert(buf.end(), {0.0f, y, static_cast<float>(w), y});
        }
        upload_and_draw(buf.data(), static_cast<int>(buf.size() / 2), GL_LINES, theme_.grid);
    }

    if (!f.candles) {
        buf.clear();
        buf.reserve(f.line.size() * 2);
        for (const auto& p : f.line) {
            buf.push_back(p.x);
            buf.push_back(static_cast<float>(p.y - f.ymin));
        }
        upload_and_draw(buf.data(), static_cast<int>(f.line.size()), GL_LINE_STRIP, theme_.line);
        return;
    }

    for (int pass = 0; pass < 2; ++pass) {  // pass 0: rising candles, pass 1: falling
        std::vector<float> body, wick;
        for (const auto& c : f.bars) {
            bool up = c.close >= c.open;
            if ((pass == 0) != up) continue;
            float x0 = c.x0 + 1.0f, x1 = c.x1 - 1.0f;
            if (x1 < x0 + 1.0f) x1 = x0 + 1.0f;
            float xm = 0.5f * (c.x0 + c.x1);
            float yo = static_cast<float>(c.open - f.ymin), yc = static_cast<float>(c.close - f.ymin);
            float yl = static_cast<float>(c.low - f.ymin), yh = static_cast<float>(c.high - f.ymin);
            float b0 = yo < yc ? yo : yc, b1 = yo < yc ? yc : yo;
            float min_body = static_cast<float>(yspan / h);  // at least one pixel tall
            if (b1 - b0 < min_body) b1 = b0 + min_body;
            body.insert(body.end(), {x0, b0, x1, b0, x1, b1, x0, b0, x1, b1, x0, b1});
            wick.insert(wick.end(), {xm, yl, xm, yh});
        }
        const float* col = pass == 0 ? theme_.up : theme_.down;
        upload_and_draw(wick.data(), static_cast<int>(wick.size() / 2), GL_LINES, col);
        upload_and_draw(body.data(), static_cast<int>(body.size() / 2), GL_TRIANGLES, col);
    }
}

}  // namespace fastchart
