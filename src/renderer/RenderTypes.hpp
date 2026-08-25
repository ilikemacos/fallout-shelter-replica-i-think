#pragma once
// Backend-agnostic renderer vocabulary. OpenGL and Vulkan backends both
// implement RenderDevice against these types; nothing above this layer
// ever names a GL or Vk symbol directly.
#include "core/Types.hpp"
#include "core/Math.hpp"
#include <string>
#include <vector>

namespace hv::gfx {

enum class Backend : u8 { None = 0, OpenGL, Vulkan };
const char* backendName(Backend b);

struct TextureTag {}; struct MeshTag {}; struct ShaderTag {};
struct MaterialTag {}; struct FramebufferTag {};
using TextureHandle    = Handle<TextureTag>;
using MeshHandle       = Handle<MeshTag>;
using ShaderHandle     = Handle<ShaderTag>;
using MaterialHandle   = Handle<MaterialTag>;
using FramebufferHandle= Handle<FramebufferTag>;

enum class TextureFormat : u8 { RGBA8, RGBA16F, R8, RG8, Depth24Stencil8, Depth32F };
enum class TextureFilter : u8 { Nearest, Linear, LinearMipmap };
enum class TextureWrap   : u8 { Repeat, Clamp, MirroredRepeat };

struct TextureDesc {
    i32 width = 1, height = 1;
    TextureFormat format = TextureFormat::RGBA8;
    TextureFilter filter = TextureFilter::LinearMipmap;
    TextureWrap wrap = TextureWrap::Repeat;
    bool genMipmaps = true;
    bool renderTarget = false;
    const char* debugName = "";
};

/// Interleaved vertex layout used by every mesh in the game — position,
/// normal, tangent, uv, and one packed vertex colour for tint/AO baking.
struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec3 tangent;
    Vec2 uv;
    u32  color = 0xFFFFFFFFu;
};

struct MeshDesc {
    std::vector<Vertex> vertices;
    std::vector<u32>    indices;
    bool dynamic = false;   ///< true for meshes rewritten every frame (UI, debug)
    const char* debugName = "";
};

enum class ShaderStage : u8 { Vertex, Fragment };
struct ShaderDesc {
    std::string vertexSource;
    std::string fragmentSource;
    const char* debugName = "";
};

/// Instance data for GPU instancing (residents, machines, foliage).
struct InstanceData {
    Mat4 model;
    Vec4 colorTint{1, 1, 1, 1};
    f32  customA = 0.0f;   ///< e.g. animation phase
    f32  customB = 0.0f;
};

enum class BlendMode : u8 { Opaque, AlphaBlend, Additive };
enum class CullMode  : u8 { Back, Front, None };

/// Which procedural texture the fragment shader synthesizes for a surface.
/// Haven ships zero texture files — every material's albedo, bump detail,
/// roughness and cavity AO is generated in-shader from world-space noise and
/// lattice patterns, so it stays sharp at any zoom with no memory cost.
enum class SurfaceKind : u8 {
    Concrete = 0,   ///< poured/board-formed concrete with aggregate + staining
    Brick,          ///< running-bond brick with recessed mortar
    Tile,           ///< gridded ceramic/utility tile
    RustedMetal,    ///< pitted, patchy oxidised steel
    PaintedMetal,   ///< painted panel, chipping to bare metal on edges
    BrushedMetal,   ///< directional brushed/machined housing
    Wood,           ///< grain + knots
    Fabric,         ///< woven weave
    Glass,          ///< smooth, low roughness
    Plastic,        ///< subtle orange-peel
    Dirt,           ///< loose ground/grit
    Skin,           ///< character skin, very light pore detail
    Emissive,       ///< self-lit panel
    Count
};

struct MaterialDesc {
    ShaderHandle shader;
    SurfaceKind surface = SurfaceKind::Concrete;
    TextureHandle albedo;
    TextureHandle normal;
    TextureHandle metalRoughAO;
    TextureHandle emissive;
    Vec3 albedoTint{1, 1, 1};
    f32  metallic = 0.0f;
    f32  roughness = 0.8f;
    f32  emissiveStrength = 0.0f;
    BlendMode blend = BlendMode::Opaque;
    CullMode  cull = CullMode::Back;
    bool castsShadow = true;
    const char* debugName = "";
};

/// Maximum occluder boxes a backend will consider when tracing shadow rays.
/// Part of the renderer contract, so the scene layer can trim to it without
/// reaching into a specific backend's internals.
constexpr int kMaxOccluderBoxes = 48;

enum class LightType : u8 { Directional, Point, Spot };
struct Light {
    LightType type = LightType::Point;
    Vec3  position;
    Vec3  direction{0, -1, 0};
    Vec3  color{1, 1, 1};
    f32   intensity = 1.0f;
    f32   range = 10.0f;
    f32   innerCone = 0.9f;   ///< cos(angle), spot lights only
    f32   outerCone = 0.75f;
    bool  castsShadow = false;
    f32   flickerSpeed = 0.0f;   ///< >0 = flickering industrial light
    f32   flickerAmount = 0.0f;
};

struct FramebufferDesc {
    i32 width = 1, height = 1;
    bool hasDepth = true;
    i32 colorAttachments = 1;
    TextureFormat colorFormat = TextureFormat::RGBA16F;
    const char* debugName = "";
};

/// A GPU/backend capability snapshot for the debug panel — never guessed,
/// always what the active device actually reports.
struct DeviceInfo {
    Backend backend = Backend::None;
    std::string gpuName;
    std::string apiVersion;
    std::string driverInfo;
    bool supportsRayTracing = false;
    bool supportsGeometryInstancing = true;
    bool supportsComputeShaders = false;
    i32  maxTextureSize = 4096;
    i32  maxSamples = 4;
};

/// Per-frame counters, read after the frame for the debug panel.
struct FrameStats {
    u32 drawCalls = 0;
    u32 instancedDrawCalls = 0;
    u64 triangles = 0;
    u32 stateChanges = 0;
    u64 textureBytesResident = 0;
    u64 meshBytesResident = 0;
};

} // namespace hv::gfx
