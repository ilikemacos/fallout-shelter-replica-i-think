#include "renderer/gl/GLDevice.hpp"
#include "renderer/gl/Shaders.hpp"
#include "core/Log.hpp"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#error "GLDevice.cpp targets the macOS OpenGL 4.1 core profile only"
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

namespace hv::gfx::gl {
namespace {

u32 compileStage(GLenum stage, const char* src) {
    const u32 id = glCreateShader(stage);
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);
    GLint ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GLsizei len = 0;
        glGetShaderInfoLog(id, sizeof log, &len, log);
        HV_ERROR("GL shader compile failed: %.*s", len, log);
        glDeleteShader(id);
        return 0;
    }
    return id;
}

GLenum toGLFormat(TextureFormat f, GLenum& internalFmt, GLenum& type) {
    switch (f) {
        case TextureFormat::RGBA8:  internalFmt = GL_RGBA8;  type = GL_UNSIGNED_BYTE; return GL_RGBA;
        case TextureFormat::RGBA16F:internalFmt = GL_RGBA16F;type = GL_FLOAT;         return GL_RGBA;
        case TextureFormat::R8:     internalFmt = GL_R8;     type = GL_UNSIGNED_BYTE; return GL_RED;
        case TextureFormat::RG8:    internalFmt = GL_RG8;    type = GL_UNSIGNED_BYTE; return GL_RG;
        case TextureFormat::Depth24Stencil8: internalFmt = GL_DEPTH24_STENCIL8; type = GL_UNSIGNED_INT_24_8; return GL_DEPTH_STENCIL;
        case TextureFormat::Depth32F: internalFmt = GL_DEPTH_COMPONENT32F; type = GL_FLOAT; return GL_DEPTH_COMPONENT;
    }
    internalFmt = GL_RGBA8; type = GL_UNSIGNED_BYTE; return GL_RGBA;
}

} // namespace

std::unique_ptr<GLDevice> GLDevice::create(i32 pixelWidth, i32 pixelHeight) {
    auto dev = std::unique_ptr<GLDevice>(new GLDevice());
    dev->width_ = pixelWidth;
    dev->height_ = pixelHeight;
    dev->queryDeviceInfo();

    dev->sceneShader_ = dev->compileProgram(ShaderDesc{shaders::kSceneVertex, shaders::kSceneFragment, "scene"});
    if (dev->sceneShader_ == 0) {
        HV_ERROR("GLDevice: core shader failed to link — no usable renderer");
        return nullptr;
    }

    glGenQueries(1, &dev->timerQueryFront_);
    glGenQueries(1, &dev->timerQueryBack_);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.02f, 0.02f, 0.025f, 1.0f);
    return dev;
}

GLDevice::~GLDevice() {
    for (auto& [id, m] : meshes_) {
        if (m.vao) glDeleteVertexArrays(1, &m.vao);
        if (m.vbo) glDeleteBuffers(1, &m.vbo);
        if (m.ibo) glDeleteBuffers(1, &m.ibo);
        if (m.instanceVbo) glDeleteBuffers(1, &m.instanceVbo);
    }
    for (auto& [id, t] : textures_) if (t.id) glDeleteTextures(1, &t.id);
    for (auto& [id, s] : shaders_) if (s.program) glDeleteProgram(s.program);
    for (auto& [id, f] : framebuffers_) if (f.fbo) glDeleteFramebuffers(1, &f.fbo);
    if (sceneShader_) glDeleteProgram(sceneShader_);
    if (timerQueryFront_) glDeleteQueries(1, &timerQueryFront_);
    if (timerQueryBack_) glDeleteQueries(1, &timerQueryBack_);
}

void GLDevice::queryDeviceInfo() {
    info_.backend = Backend::OpenGL;
    const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* vendor = glGetString(GL_VENDOR);
    info_.gpuName = renderer ? reinterpret_cast<const char*>(renderer) : "Unknown GPU";
    info_.apiVersion = version ? reinterpret_cast<const char*>(version) : "Unknown";
    info_.driverInfo = vendor ? reinterpret_cast<const char*>(vendor) : "Unknown";
    info_.supportsRayTracing = false;   // GL 4.1 core has no RT path; Vulkan backend reports its own
    info_.supportsComputeShaders = false;   // requires 4.3+; Apple's GL tops out at 4.1
    GLint maxTex = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    info_.maxTextureSize = maxTex;
    GLint samples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &samples);
    info_.maxSamples = samples;
    HV_INFO("OpenGL device: %s (%s) — %s", info_.gpuName.c_str(), info_.driverInfo.c_str(), info_.apiVersion.c_str());
}

void GLDevice::resize(i32 w, i32 h) {
    width_ = std::max(1, w);
    height_ = std::max(1, h);
    glViewport(0, 0, width_, height_);
}

void GLDevice::setVSync(bool) {
    // Handled by the platform layer (NSOpenGLContext swap interval) since GL
    // itself has no portable vsync toggle; kept here only for interface symmetry.
}

u32 GLDevice::compileProgram(const ShaderDesc& desc) {
    const u32 vs = compileStage(GL_VERTEX_SHADER, desc.vertexSource.c_str());
    const u32 fs = compileStage(GL_FRAGMENT_SHADER, desc.fragmentSource.c_str());
    if (!vs || !fs) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); return 0; }
    const u32 prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048]; GLsizei len = 0;
        glGetProgramInfoLog(prog, sizeof log, &len, log);
        HV_ERROR("GL program link failed (%s): %.*s", desc.debugName, len, log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// ---------------------------------------------------------------------------
//  Resources
// ---------------------------------------------------------------------------

TextureHandle GLDevice::createTexture(const TextureDesc& desc, const void* pixels) {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    GLenum internalFmt, type;
    const GLenum fmt = toGLFormat(desc.format, internalFmt, type);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFmt), desc.width, desc.height, 0, fmt, type, pixels);
    const GLenum minFilter = desc.filter == TextureFilter::Nearest ? GL_NEAREST
        : (desc.filter == TextureFilter::LinearMipmap && desc.genMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(minFilter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, desc.filter == TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR);
    const GLenum wrap = desc.wrap == TextureWrap::Clamp ? GL_CLAMP_TO_EDGE
        : (desc.wrap == TextureWrap::MirroredRepeat ? GL_MIRRORED_REPEAT : GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(wrap));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(wrap));
    if (desc.genMipmaps && pixels) glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    const u32 idx = nextTexture_++;
    textures_.emplace(idx, GLTexture{id, desc});
    residentBytes_ += static_cast<u64>(desc.width) * static_cast<u64>(desc.height) * 4;
    return TextureHandle{idx, 1};
}

void GLDevice::updateTexture(TextureHandle h, const void* pixels, i32 x, i32 y, i32 w, i32 h2) {
    auto it = textures_.find(h.index);
    if (it == textures_.end()) return;
    GLenum internalFmt, type;
    const GLenum fmt = toGLFormat(it->second.desc.format, internalFmt, type);
    glBindTexture(GL_TEXTURE_2D, it->second.id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h2, fmt, type, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GLDevice::destroyTexture(TextureHandle h) {
    auto it = textures_.find(h.index);
    if (it == textures_.end()) return;
    glDeleteTextures(1, &it->second.id);
    textures_.erase(it);
}

MeshHandle GLDevice::createMesh(const MeshDesc& desc) {
    GLMesh m;
    m.dynamic = desc.dynamic;
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glGenBuffers(1, &m.ibo);
    glGenBuffers(1, &m.instanceVbo);
    glBindVertexArray(m.vao);

    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(desc.vertices.size() * sizeof(Vertex)),
                desc.vertices.data(), desc.dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, tangent)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, color)));

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(desc.indices.size() * sizeof(u32)),
                desc.indices.data(), desc.dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    m.indexCount = static_cast<u32>(desc.indices.size());

    // Instance stream: 4 vec4 columns (model) + tint + custom, locations 5..10.
    glBindBuffer(GL_ARRAY_BUFFER, m.instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(64 * sizeof(InstanceData)), nullptr, GL_STREAM_DRAW);
    for (int col = 0; col < 4; ++col) {
        glEnableVertexAttribArray(static_cast<GLuint>(5 + col));
        glVertexAttribPointer(static_cast<GLuint>(5 + col), 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                              reinterpret_cast<void*>(sizeof(f32) * 4 * static_cast<size_t>(col)));
        glVertexAttribDivisor(static_cast<GLuint>(5 + col), 1);
    }
    glEnableVertexAttribArray(9);
    glVertexAttribPointer(9, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData), reinterpret_cast<void*>(offsetof(InstanceData, colorTint)));
    glVertexAttribDivisor(9, 1);
    glEnableVertexAttribArray(10);
    glVertexAttribPointer(10, 2, GL_FLOAT, GL_FALSE, sizeof(InstanceData), reinterpret_cast<void*>(offsetof(InstanceData, customA)));
    glVertexAttribDivisor(10, 1);

    glBindVertexArray(0);

    const u32 idx = nextMesh_++;
    meshes_.emplace(idx, m);
    residentBytes_ += desc.vertices.size() * sizeof(Vertex) + desc.indices.size() * sizeof(u32);
    return MeshHandle{idx, 1};
}

void GLDevice::updateMesh(MeshHandle h, const MeshDesc& desc) {
    auto it = meshes_.find(h.index);
    if (it == meshes_.end()) return;
    GLMesh& m = it->second;
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(desc.vertices.size() * sizeof(Vertex)),
                desc.vertices.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(desc.indices.size() * sizeof(u32)),
                desc.indices.data(), GL_DYNAMIC_DRAW);
    m.indexCount = static_cast<u32>(desc.indices.size());
}

void GLDevice::destroyMesh(MeshHandle h) {
    auto it = meshes_.find(h.index);
    if (it == meshes_.end()) return;
    glDeleteVertexArrays(1, &it->second.vao);
    glDeleteBuffers(1, &it->second.vbo);
    glDeleteBuffers(1, &it->second.ibo);
    glDeleteBuffers(1, &it->second.instanceVbo);
    meshes_.erase(it);
}

ShaderHandle GLDevice::createShader(const ShaderDesc& desc) {
    const u32 prog = compileProgram(desc);
    const u32 idx = nextShader_++;
    shaders_.emplace(idx, GLShader{prog});
    return ShaderHandle{idx, prog ? 1u : 0u};
}
void GLDevice::destroyShader(ShaderHandle h) {
    auto it = shaders_.find(h.index);
    if (it == shaders_.end()) return;
    if (it->second.program) glDeleteProgram(it->second.program);
    shaders_.erase(it);
}

MaterialHandle GLDevice::createMaterial(const MaterialDesc& desc) {
    const u32 idx = nextMaterial_++;
    materials_.emplace(idx, GLMaterial{desc});
    return MaterialHandle{idx, 1};
}
void GLDevice::updateMaterial(MaterialHandle h, const MaterialDesc& desc) {
    auto it = materials_.find(h.index);
    if (it != materials_.end()) it->second.desc = desc;
}
void GLDevice::destroyMaterial(MaterialHandle h) { materials_.erase(h.index); }

FramebufferHandle GLDevice::createFramebuffer(const FramebufferDesc& desc) {
    GLFramebuffer fb;
    fb.w = desc.width; fb.h = desc.height;
    glGenFramebuffers(1, &fb.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);
    for (i32 i = 0; i < desc.colorAttachments; ++i) {
        TextureDesc td; td.width = desc.width; td.height = desc.height; td.format = desc.colorFormat;
        td.genMipmaps = false; td.renderTarget = true; td.wrap = TextureWrap::Clamp;
        const TextureHandle th = createTexture(td, nullptr);
        fb.colors.push_back(th);
        glFramebufferTexture2D(GL_FRAMEBUFFER, static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i), GL_TEXTURE_2D,
                               textures_[th.index].id, 0);
    }
    if (desc.hasDepth) {
        TextureDesc dd; dd.width = desc.width; dd.height = desc.height; dd.format = TextureFormat::Depth24Stencil8;
        dd.genMipmaps = false; dd.renderTarget = true;
        fb.depth = createTexture(dd, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, textures_[fb.depth.index].id, 0);
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        HV_ERROR("GL framebuffer incomplete: %s", desc.debugName);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const u32 idx = nextFramebuffer_++;
    framebuffers_.emplace(idx, std::move(fb));
    return FramebufferHandle{idx, 1};
}
void GLDevice::destroyFramebuffer(FramebufferHandle h) {
    auto it = framebuffers_.find(h.index);
    if (it == framebuffers_.end()) return;
    glDeleteFramebuffers(1, &it->second.fbo);
    for (TextureHandle t : it->second.colors) destroyTexture(t);
    if (it->second.depth.valid()) destroyTexture(it->second.depth);
    framebuffers_.erase(it);
}
TextureHandle GLDevice::framebufferColorTexture(FramebufferHandle h, i32 index) {
    auto it = framebuffers_.find(h.index);
    if (it == framebuffers_.end() || index >= static_cast<i32>(it->second.colors.size())) return {};
    return it->second.colors[static_cast<size_t>(index)];
}

// ---------------------------------------------------------------------------
//  Frame
// ---------------------------------------------------------------------------

CommandBuffer& GLDevice::begin() {
    frameCmd_.reset();
    stats_ = FrameStats{};
    if (timerQueryPending_) {
        GLint available = 0;
        glGetQueryObjectiv(timerQueryBack_, GL_QUERY_RESULT_AVAILABLE, &available);
        if (available) {
            GLuint64 elapsedNs = 0;
            glGetQueryObjectui64v(timerQueryBack_, GL_QUERY_RESULT, &elapsedNs);
            lastGpuMs_ = static_cast<f64>(elapsedNs) / 1e6;
        }
    }
    glBeginQuery(GL_TIME_ELAPSED, timerQueryFront_);
    return frameCmd_;
}

void GLDevice::executePass(const CommandBuffer::Pass& pass) {
    if (pass.desc.target.valid() && pass.desc.target.index != 0) {
        auto it = framebuffers_.find(pass.desc.target.index);
        if (it != framebuffers_.end()) {
            glBindFramebuffer(GL_FRAMEBUFFER, it->second.fbo);
            glViewport(0, 0, it->second.w, it->second.h);
        }
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width_, height_);
    }

    GLbitfield clearMask = 0;
    if (pass.desc.clearColor) {
        glClearColor(pass.desc.clearColorValue.x, pass.desc.clearColorValue.y,
                    pass.desc.clearColorValue.z, pass.desc.clearColorValue.w);
        clearMask |= GL_COLOR_BUFFER_BIT;
    }
    if (pass.desc.clearDepth) clearMask |= GL_DEPTH_BUFFER_BIT;
    if (clearMask) glClear(clearMask);

    glUseProgram(sceneShader_);
    glUniformMatrix4fv(glGetUniformLocation(sceneShader_, "uView"), 1, GL_FALSE, pass.view.m);
    glUniformMatrix4fv(glGetUniformLocation(sceneShader_, "uProj"), 1, GL_FALSE, pass.proj.m);
    glUniform3f(glGetUniformLocation(sceneShader_, "uEyePos"), pass.eye.x, pass.eye.y, pass.eye.z);
    // A dim underground bunker still needs enough fill light to read as
    // "dark and atmospheric" rather than "not rendering" — this is tuned
    // against the tonemap/gamma pass in the fragment shader, not raw.
    glUniform3f(glGetUniformLocation(sceneShader_, "uAmbient"), 0.52f, 0.55f, 0.60f);
    glUniform1f(glGetUniformLocation(sceneShader_, "uExposure"), 1.6f);
    glUniform1f(glGetUniformLocation(sceneShader_, "uFogDensity"), 0.028f);
    // Warm amber haze, matching the industrial fixture colour, rather than a
    // neutral grey — corridors recede into the shelter's own light instead
    // of a generic fog wall.
    glUniform3f(glGetUniformLocation(sceneShader_, "uFogColor"), 0.16f, 0.13f, 0.10f);

    const int lightCount = std::min<int>(static_cast<int>(pass.lights.size()), shaders::kMaxLights);
    glUniform1i(glGetUniformLocation(sceneShader_, "uLightCount"), lightCount);
    for (int i = 0; i < lightCount; ++i) {
        const Light& L = pass.lights[static_cast<size_t>(i)];
        char name[64];
        std::snprintf(name, sizeof name, "uLights[%d].posType", i);
        const Vec3 posOrDir = L.type == LightType::Directional ? L.direction : L.position;
        glUniform4f(glGetUniformLocation(sceneShader_, name), posOrDir.x, posOrDir.y, posOrDir.z,
                   L.type == LightType::Directional ? 0.0f : (L.type == LightType::Spot ? 2.0f : 1.0f));
        std::snprintf(name, sizeof name, "uLights[%d].colorIntensity", i);
        glUniform4f(glGetUniformLocation(sceneShader_, name), L.color.x, L.color.y, L.color.z, L.intensity);
        std::snprintf(name, sizeof name, "uLights[%d].params", i);
        glUniform4f(glGetUniformLocation(sceneShader_, name), L.range, L.innerCone, L.outerCone, 0.0f);
    }

    for (const DrawItem& item : pass.items) {
        auto meshIt = meshes_.find(item.mesh.index);
        auto matIt = materials_.find(item.material.index);
        if (meshIt == meshes_.end() || matIt == materials_.end()) continue;
        const MaterialDesc& md = matIt->second.desc;

        glUniform3f(glGetUniformLocation(sceneShader_, "uAlbedo"), md.albedoTint.x, md.albedoTint.y, md.albedoTint.z);
        glUniform1f(glGetUniformLocation(sceneShader_, "uMetallic"), md.metallic);
        glUniform1f(glGetUniformLocation(sceneShader_, "uRoughness"), md.roughness);
        glUniform1f(glGetUniformLocation(sceneShader_, "uEmissive"), md.emissiveStrength);
        const bool hasTex = md.albedo.valid() && textures_.count(md.albedo.index) != 0;
        glUniform1i(glGetUniformLocation(sceneShader_, "uHasAlbedoTex"), hasTex ? 1 : 0);
        if (hasTex) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, textures_[md.albedo.index].id);
            glUniform1i(glGetUniformLocation(sceneShader_, "uAlbedoTex"), 0);
        }
        if (md.cull == CullMode::None) glDisable(GL_CULL_FACE);
        else { glEnable(GL_CULL_FACE); glCullFace(md.cull == CullMode::Front ? GL_FRONT : GL_BACK); }
        if (md.blend == BlendMode::Opaque) glDisable(GL_BLEND);
        else {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, md.blend == BlendMode::Additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        }

        GLMesh& gm = meshIt->second;
        glBindVertexArray(gm.vao);

        if (item.instanceCount > 0) {
            glUniform1i(glGetUniformLocation(sceneShader_, "uInstanced"), 1);
            glBindBuffer(GL_ARRAY_BUFFER, gm.instanceVbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(item.instanceCount * sizeof(InstanceData)),
                        pass.instances.data() + item.instanceStart, GL_STREAM_DRAW);
            glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(gm.indexCount), GL_UNSIGNED_INT, nullptr,
                                    item.instanceCount);
            ++stats_.instancedDrawCalls;
            stats_.triangles += (gm.indexCount / 3) * static_cast<u32>(item.instanceCount);
        } else {
            glUniform1i(glGetUniformLocation(sceneShader_, "uInstanced"), 0);
            glUniformMatrix4fv(glGetUniformLocation(sceneShader_, "uModel"), 1, GL_FALSE, item.model.m);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(gm.indexCount), GL_UNSIGNED_INT, nullptr);
            ++stats_.drawCalls;
            stats_.triangles += gm.indexCount / 3;
        }
        ++stats_.stateChanges;
    }
    glBindVertexArray(0);
}

void GLDevice::submit(CommandBuffer& cmd) {
    for (const CommandBuffer::Pass& pass : cmd.passes()) executePass(pass);
    glEndQuery(GL_TIME_ELAPSED);
    timerQueryPending_ = true;
    std::swap(timerQueryFront_, timerQueryBack_);
}

void GLDevice::present() {
    // The platform window (NSOpenGLContext) performs the actual buffer swap
    // right after this call returns; nothing GL-specific happens here.
}

} // namespace hv::gfx::gl
