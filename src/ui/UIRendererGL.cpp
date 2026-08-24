// GL-backed UIRenderer implementation. Batches every rect/text glyph drawn
// this frame into one dynamic vertex buffer and issues it in a handful of
// draw calls at endFrame() — the HUD is cheap even with hundreds of glyphs.
#include "ui/UIRenderer.hpp"
#include "renderer/gl/Shaders.hpp"
#include "core/Log.hpp"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#error "UIRendererGL.cpp targets macOS OpenGL only"
#endif

namespace hv::ui {
namespace {

u32 compile(GLenum stage, const char* src) {
    const u32 id = glCreateShader(stage);
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);
    GLint ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; GLsizei len = 0;
        glGetShaderInfoLog(id, sizeof log, &len, log);
        HV_ERROR("UI shader compile failed: %.*s", len, log);
    }
    return id;
}

class UIRendererGL final : public UIRenderer {
public:
    UIRendererGL() {
        const u32 vs = compile(GL_VERTEX_SHADER, gfx::gl::shaders::kUIVertex);
        const u32 fs = compile(GL_FRAGMENT_SHADER, gfx::gl::shaders::kUIFragment);
        program_ = glCreateProgram();
        glAttachShader(program_, vs);
        glAttachShader(program_, fs);
        glLinkProgram(program_);
        glDeleteShader(vs);
        glDeleteShader(fs);

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex), reinterpret_cast<void*>(offsetof(UIVertex, pos)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex), reinterpret_cast<void*>(offsetof(UIVertex, uv)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(UIVertex), reinterpret_cast<void*>(offsetof(UIVertex, color)));
        glBindVertexArray(0);
    }
    ~UIRendererGL() override {
        if (vao_) glDeleteVertexArrays(1, &vao_);
        if (vbo_) glDeleteBuffers(1, &vbo_);
        if (program_) glDeleteProgram(program_);
    }

    void beginFrame(i32 pixelWidth, i32 pixelHeight, f32 backingScale) override {
        // UI is authored in points; the vertex shader divides by point-space
        // screen size, so we present pixelWidth/backingScale as the "screen".
        screenSize_ = Vec2{static_cast<f32>(pixelWidth) / std::max(1.0f, backingScale),
                           static_cast<f32>(pixelHeight) / std::max(1.0f, backingScale)};
        pixelSize_ = Vec2{static_cast<f32>(pixelWidth), static_cast<f32>(pixelHeight)};
        vertices_.clear();
    }

    void endFrame() override {
        if (vertices_.empty()) return;
        glViewport(0, 0, static_cast<GLsizei>(pixelSize_.x), static_cast<GLsizei>(pixelSize_.y));
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(program_);
        glUniform2f(glGetUniformLocation(program_, "uScreenSize"), screenSize_.x, screenSize_.y);
        glUniform1i(glGetUniformLocation(program_, "uUseTex"), 0);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices_.size() * sizeof(UIVertex)),
                    vertices_.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);
    }

protected:
    void pushQuad(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, Vec2 uv0, Vec2 uv1, u32 color, bool) override {
        UIVertex a{p0, uv0, color}, b{p1, {uv1.x, uv0.y}, color}, c{p2, uv1, color}, d{p3, {uv0.x, uv1.y}, color};
        vertices_.insert(vertices_.end(), {a, b, c, a, c, d});
    }

private:
    u32 program_ = 0, vao_ = 0, vbo_ = 0;
    Vec2 pixelSize_{1,1};
    std::vector<UIVertex> vertices_;
};

} // namespace

std::unique_ptr<UIRenderer> UIRenderer::create() { return std::make_unique<UIRendererGL>(); }

} // namespace hv::ui
