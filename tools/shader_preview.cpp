// Offscreen shader preview.
//
// Haven's renderer is macOS-only, but its shaders and procedural mesh
// generation are ordinary GLSL and portable C++. This tool builds a small
// sample scene from the *real* mesh primitives and the *real* embedded
// shaders, renders it through a software OSMesa 4.1 core context, and writes
// a PPM — so shader compile errors, back-face winding mistakes and lighting
// regressions can be caught on any Linux box or in CI instead of only on a
// Mac. It already caught three shipped bugs: a reserved-keyword GLSL compile
// error, floors wound inside-out (invisible from above), and cylinder caps
// wound inside-out.
//
// Build with -DHAVEN_BUILD_SHADER_PREVIEW=ON (needs libOSMesa).
// Usage: haven_shader_preview [out.ppm] [rayTracedShadows 0|1|2]
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "renderer/gl/Shaders.hpp"
#include "renderer/Camera.hpp"
#include "scene/Primitives.hpp"
#include "sim/SimTypes.hpp"

using namespace hv;
using namespace hv::scene;

static PFNGLCREATESHADERPROC        p_glCreateShader;
static PFNGLSHADERSOURCEPROC        p_glShaderSource;
static PFNGLCOMPILESHADERPROC       p_glCompileShader;
static PFNGLGETSHADERIVPROC         p_glGetShaderiv;
static PFNGLGETSHADERINFOLOGPROC    p_glGetShaderInfoLog;
static PFNGLCREATEPROGRAMPROC       p_glCreateProgram;
static PFNGLATTACHSHADERPROC        p_glAttachShader;
static PFNGLLINKPROGRAMPROC         p_glLinkProgram;
static PFNGLGETPROGRAMIVPROC        p_glGetProgramiv;
static PFNGLGETPROGRAMINFOLOGPROC   p_glGetProgramInfoLog;
static PFNGLUSEPROGRAMPROC          p_glUseProgram;
static PFNGLGENVERTEXARRAYSPROC     p_glGenVertexArrays;
static PFNGLBINDVERTEXARRAYPROC     p_glBindVertexArray;
static PFNGLGENBUFFERSPROC          p_glGenBuffers;
static PFNGLBINDBUFFERPROC          p_glBindBuffer;
static PFNGLBUFFERDATAPROC          p_glBufferData;
static PFNGLENABLEVERTEXATTRIBARRAYPROC p_glEnableVertexAttribArray;
static PFNGLVERTEXATTRIBPOINTERPROC p_glVertexAttribPointer;
static PFNGLGETUNIFORMLOCATIONPROC  p_glGetUniformLocation;
static PFNGLUNIFORM1IPROC           p_glUniform1i;
static PFNGLUNIFORM1FPROC           p_glUniform1f;
static PFNGLUNIFORM2FPROC           p_glUniform2f;
static PFNGLUNIFORM3FPROC           p_glUniform3f;
static PFNGLUNIFORM4FPROC           p_glUniform4f;
static PFNGLUNIFORM3FVPROC          p_glUniform3fv;
static PFNGLUNIFORMMATRIX4FVPROC    p_glUniformMatrix4fv;

#define LOAD(n) p_##n = (decltype(p_##n))OSMesaGetProcAddress(#n); \
    if (!p_##n) { std::fprintf(stderr, "missing GL entry point: %s\n", #n); return 1; }

static GLuint buildProgram(const char* vs_src, const char* fs_src) {
    auto stage = [](GLenum type, const char* src) -> GLuint {
        GLuint id = p_glCreateShader(type);
        p_glShaderSource(id, 1, &src, nullptr);
        p_glCompileShader(id);
        GLint ok = 0;
        p_glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[8192]; p_glGetShaderInfoLog(id, sizeof log, nullptr, log);
                   std::fprintf(stderr, "compile failed:\n%s\n", log); std::exit(2); }
        return id;
    };
    GLuint vs = stage(GL_VERTEX_SHADER, vs_src);
    GLuint fs = stage(GL_FRAGMENT_SHADER, fs_src);
    GLuint prog = p_glCreateProgram();
    p_glAttachShader(prog, vs);
    p_glAttachShader(prog, fs);
    p_glLinkProgram(prog);
    GLint ok = 0;
    p_glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { char log[8192]; p_glGetProgramInfoLog(prog, sizeof log, nullptr, log);
               std::fprintf(stderr, "link failed:\n%s\n", log); std::exit(2); }
    return prog;
}

struct GpuMesh { GLuint vao = 0; GLsizei indexCount = 0; };

static GpuMesh upload(const MeshBuild& m) {
    GpuMesh g;
    GLuint vbo, ibo;
    p_glGenVertexArrays(1, &g.vao);
    p_glBindVertexArray(g.vao);
    p_glGenBuffers(1, &vbo);
    p_glBindBuffer(GL_ARRAY_BUFFER, vbo);
    p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(m.v.size() * sizeof(gfx::Vertex)), m.v.data(), GL_STATIC_DRAW);
    p_glEnableVertexAttribArray(0);
    p_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(gfx::Vertex), (void*)offsetof(gfx::Vertex, position));
    p_glEnableVertexAttribArray(1);
    p_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(gfx::Vertex), (void*)offsetof(gfx::Vertex, normal));
    p_glEnableVertexAttribArray(2);
    p_glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(gfx::Vertex), (void*)offsetof(gfx::Vertex, tangent));
    p_glEnableVertexAttribArray(3);
    p_glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(gfx::Vertex), (void*)offsetof(gfx::Vertex, uv));
    p_glEnableVertexAttribArray(4);
    p_glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(gfx::Vertex), (void*)offsetof(gfx::Vertex, color));
    p_glGenBuffers(1, &ibo);
    p_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    p_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(m.i.size() * sizeof(u32)), m.i.data(), GL_STATIC_DRAW);
    g.indexCount = (GLsizei)m.i.size();
    p_glBindVertexArray(0);
    return g;
}

int main(int argc, char** argv) {
    const int W = 1280, H = 720;
    const int attribs[] = {
        OSMESA_FORMAT, OSMESA_RGBA, OSMESA_DEPTH_BITS, 24,
        OSMESA_PROFILE, OSMESA_CORE_PROFILE,
        OSMESA_CONTEXT_MAJOR_VERSION, 4, OSMESA_CONTEXT_MINOR_VERSION, 1, 0 };
    OSMesaContext ctx = OSMesaCreateContextAttribs(attribs, nullptr);
    if (!ctx) { std::fprintf(stderr, "context failed\n"); return 1; }
    std::vector<unsigned char> buf((size_t)W * H * 4);
    if (!OSMesaMakeCurrent(ctx, buf.data(), GL_UNSIGNED_BYTE, W, H)) return 1;
    OSMesaPixelStore(OSMESA_Y_UP, 0);   // top-down, matches PPM row order

    LOAD(glCreateShader) LOAD(glShaderSource) LOAD(glCompileShader) LOAD(glGetShaderiv)
    LOAD(glGetShaderInfoLog) LOAD(glCreateProgram) LOAD(glAttachShader) LOAD(glLinkProgram)
    LOAD(glGetProgramiv) LOAD(glGetProgramInfoLog) LOAD(glUseProgram)
    LOAD(glGenVertexArrays) LOAD(glBindVertexArray) LOAD(glGenBuffers) LOAD(glBindBuffer)
    LOAD(glBufferData) LOAD(glEnableVertexAttribArray) LOAD(glVertexAttribPointer)
    LOAD(glGetUniformLocation) LOAD(glUniform1i) LOAD(glUniform1f) LOAD(glUniform2f) LOAD(glUniform3f)
    LOAD(glUniform4f) LOAD(glUniform3fv) LOAD(glUniformMatrix4fv)

    GLuint prog = buildProgram(gfx::gl::shaders::kSceneVertex, gfx::gl::shaders::kSceneFragment);
    std::printf("scene shader compiled + linked OK\n");

    // ---- geometry: a floor slab and a row of room-sized blocks -------------
    MeshBuild floorB;
    appendQuadXZ(floorB, {0, 0, 0}, 60.0f, 20.0f, {8, 4});
    GpuMesh floorMesh = upload(floorB);

    MeshBuild boxB;
    appendBox(boxB, {0, 1.2f, 0}, {2.2f, 1.2f, 2.2f}, {2, 2});
    GpuMesh boxMesh = upload(boxB);

    MeshBuild drumB;
    appendCylinder(drumB, {0, 0, 0}, 1.1f, 2.4f, 24);
    GpuMesh drumMesh = upload(drumB);

    MeshBuild bodyB;
    appendCapsule(bodyB, {0, 0, 0}, 0.28f, 1.65f, 12);
    GpuMesh bodyMesh = upload(bodyB);

    // ---- camera, matching the game's default framing ----------------------
    gfx::Camera cam;
    cam.setAspect((f32)W / (f32)H);
    cam.focusPoint({0, 0, 0});
    cam.zoom(-6.0f);
    cam.update(10.0f);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glViewport(0, 0, W, H);
    glClearColor(0.05f, 0.055f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    p_glUseProgram(prog);
    auto U = [&](const char* n) { return p_glGetUniformLocation(prog, n); };
    p_glUniformMatrix4fv(U("uView"), 1, GL_FALSE, cam.view().m);
    p_glUniformMatrix4fv(U("uProj"), 1, GL_FALSE, cam.projection().m);
    const Vec3 eye = cam.eyePosition();
    p_glUniform3f(U("uEyePos"), eye.x, eye.y, eye.z);
    // Same values GLDevice.cpp uploads.
    using hv::gfx::gl::shaders::kTone;
    p_glUniform3fv(U("uAmbientSky"), 1, kTone.ambientSky);
    p_glUniform3fv(U("uAmbientGround"), 1, kTone.ambientGround);
    p_glUniform1f(U("uExposure"), kTone.exposure);
    p_glUniform1f(U("uFogDensity"), kTone.fogDensity);
    p_glUniform3fv(U("uFogColor"), 1, kTone.fogColor);
    using hv::gfx::gl::shaders::kGrade;
    p_glUniform1f(U("uSaturation"), kGrade.saturation);
    p_glUniform3fv(U("uShadowTint"), 1, kGrade.shadowTint);
    p_glUniform3fv(U("uHighlightTint"), 1, kGrade.highlightTint);
    p_glUniform1f(U("uContrast"), kGrade.contrast);
    p_glUniform1f(U("uVignette"), kGrade.vignette);
    p_glUniform2f(U("uViewportSize"), (float)W, (float)H);
    p_glUniform1f(U("uTexScale"), 1.0f);
    p_glUniform1i(U("uInstanced"), 0);

    // Lights: the App's sun plus two warm fixtures.
    p_glUniform1i(U("uLightCount"), 3);
    const Vec3 sunDir = normalize(Vec3{0.62f, -0.60f, 0.42f});
    p_glUniform4f(U("uLights[0].posType"), sunDir.x, sunDir.y, sunDir.z, 0.0f);
    p_glUniform4f(U("uLights[0].colorIntensity"), 0.85f, 0.86f, 0.92f, 2.6f);
    p_glUniform4f(U("uLights[0].params"), 1000.0f, 0.9f, 0.75f, 0.0f);
    p_glUniform4f(U("uLights[1].posType"), -6.0f, 3.9f, 0.0f, 1.0f);
    p_glUniform4f(U("uLights[1].colorIntensity"), 1.0f, 0.72f, 0.42f, 5.0f);
    p_glUniform4f(U("uLights[1].params"), 11.0f, 0.9f, 0.75f, 0.0f);
    p_glUniform4f(U("uLights[2].posType"), 7.0f, 3.9f, 0.0f, 1.0f);
    p_glUniform4f(U("uLights[2].colorIntensity"), 1.0f, 0.72f, 0.42f, 5.0f);
    p_glUniform4f(U("uLights[2].params"), 11.0f, 0.9f, 0.75f, 0.0f);

    // Box soup + ray-traced shadows on (Very Light = sun only).
    // Box soup matching the row of blocks actually drawn below.
    const float xs[5] = { -9.5f, -4.7f, 0.0f, 4.7f, 9.5f };
    float boxMin[15], boxMax[15];
    for (int i = 0; i < 5; ++i) {
        boxMin[i*3+0] = xs[i] - 2.2f; boxMin[i*3+1] = 0.0f; boxMin[i*3+2] = -2.2f;
        boxMax[i*3+0] = xs[i] + 2.2f; boxMax[i*3+1] = 2.4f; boxMax[i*3+2] =  2.2f;
    }
    p_glUniform1i(U("uBoxCount"), 5);
    p_glUniform3fv(U("uBoxMin"), 5, boxMin);
    p_glUniform3fv(U("uBoxMax"), 5, boxMax);
    p_glUniform1i(U("uRayTracedShadows"), argc > 2 ? atoi(argv[2]) : 1);

    auto draw = [&](const GpuMesh& m, Mat4 model, Vec3 tint, int kind,
                    float metallic, float rough, float emissive) {
        p_glUniformMatrix4fv(U("uModel"), 1, GL_FALSE, model.m);
        p_glUniform3f(U("uAlbedo"), tint.x, tint.y, tint.z);
        p_glUniform1i(U("uSurfaceKind"), kind);
        p_glUniform1f(U("uMetallic"), metallic);
        p_glUniform1f(U("uRoughness"), rough);
        p_glUniform1f(U("uEmissive"), emissive);
        p_glBindVertexArray(m.vao);
        glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, nullptr);
    };

    // Concrete floor, then one block per material so every procedural
    // texture is visible in a single frame.
    draw(floorMesh, Mat4::identity(), colorFromHex(0x6E6A62), 0, 0.02f, 0.92f, 0.0f);
    draw(boxMesh, Mat4::translate({-9.5f, 0, 0}), colorFromHex(0x6E6A62), 0, 0.02f, 0.92f, 0.0f);   // concrete
    draw(boxMesh, Mat4::translate({-4.7f, 0, 0}), colorFromHex(0x7E4030), 1, 0.0f, 0.9f, 0.0f);     // brick
    draw(boxMesh, Mat4::translate({0.0f, 0, 0}),  colorFromHex(0x7A4A2E), 2, 0.55f, 0.65f, 0.0f);   // rusted
    draw(boxMesh, Mat4::translate({4.7f, 0, 0}),  colorFromHex(0x3C4A42), 3, 0.35f, 0.5f, 0.0f);    // painted
    draw(boxMesh, Mat4::translate({9.5f, 0, 0}),  colorFromHex(0x5E7C84), 4, 0.1f, 0.3f, 0.0f);     // tile
    draw(drumMesh, Mat4::translate({-7.0f, 0, 5.0f}), colorFromHex(0x53483C), 5, 0.85f, 0.35f, 0.0f); // brushed
    draw(drumMesh, Mat4::translate({-2.5f, 0, 5.0f}), colorFromHex(0x6A4E2E), 6, 0.0f, 0.78f, 0.0f);  // wood
    draw(boxMesh,  Mat4::translate({2.5f, 0, 5.5f}) * Mat4::scale({0.6f,0.4f,0.6f}),
         colorFromHex(0x4E5A44), 7, 0.0f, 0.9f, 0.0f);                                               // fabric
    draw(bodyMesh, Mat4::translate({6.5f, 0, 5.5f}) * Mat4::scale({1.6f,1.6f,1.6f}),
         colorFromHex(0xC98E63), 11, 0.0f, 0.68f, 0.0f);                                             // skin

    glFinish();

    const char* out = argc > 1 ? argv[1] : "/tmp/haven_render.ppm";
    std::FILE* f = std::fopen(out, "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (size_t i = 0; i < (size_t)W * H; ++i)
        std::fwrite(&buf[i * 4], 1, 3, f);
    std::fclose(f);
    std::printf("wrote %s\n", out);

    // Report the average luminance so "is it actually bright enough" is a
    // number, not an impression.
    double lum = 0.0;
    for (size_t i = 0; i < (size_t)W * H; ++i)
        lum += 0.2126 * buf[i*4] + 0.7152 * buf[i*4+1] + 0.0722 * buf[i*4+2];
    std::printf("mean luminance: %.1f / 255\n", lum / ((double)W * H));
    OSMesaDestroyContext(ctx);
    return 0;
}
