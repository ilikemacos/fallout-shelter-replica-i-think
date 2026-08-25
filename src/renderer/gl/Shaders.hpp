#pragma once
// Embedded GLSL 4.1 core source. Keeping shaders in the binary (rather than
// only as loose files) means a build can never fail to find them; the
// shaders/ directory is still shipped for reference and for the Vulkan
// SPIR-V pipeline built from the same shading logic.
namespace hv::gfx::gl::shaders {

// Up to kMaxLights are uploaded per pass; LightingSystem::nearest() already
// trims to the closest ones so a large shelter stays within budget.
constexpr int kMaxLights = 16;

inline const char* kSceneVertex = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aTangent;
layout(location = 3) in vec2 aUV;
layout(location = 4) in vec4 aColor;
// Instance attributes (model matrix as 4 columns + tint + custom), only
// consumed when uInstanced == 1.
layout(location = 5) in vec4 iModel0;
layout(location = 6) in vec4 iModel1;
layout(location = 7) in vec4 iModel2;
layout(location = 8) in vec4 iModel3;
layout(location = 9) in vec4 iTint;
layout(location = 10) in vec2 iCustom;

uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uModel;
uniform int  uInstanced;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec4 vColor;
out vec4 vTint;
out vec2 vCustom;

void main() {
    mat4 model = uModel;
    vec4 tint = vec4(1.0);
    vec2 custom = vec2(0.0);
    if (uInstanced == 1) {
        model = mat4(iModel0, iModel1, iModel2, iModel3);
        tint = iTint;
        custom = iCustom;
    }
    vec4 world = model * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    vNormal = mat3(model) * aNormal;
    vUV = aUV;
    vColor = aColor;
    vTint = tint;
    vCustom = custom;
    gl_Position = uProj * uView * world;
}
)GLSL";

inline const char* kSceneFragment = R"GLSL(
#version 410 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vColor;
in vec4 vTint;
in vec2 vCustom;
out vec4 FragColor;

uniform vec3  uEyePos;
uniform vec3  uAlbedo;
uniform float uMetallic;
uniform float uRoughness;
uniform float uEmissive;
uniform vec3  uAmbient;
uniform sampler2D uAlbedoTex;
uniform int   uHasAlbedoTex;

struct Light {
    vec4 posType;      // xyz = position (or -direction for directional), w = type (0 dir,1 point,2 spot)
    vec4 colorIntensity; // rgb = color, a = intensity
    vec4 params;        // x = range, y = innerCos, z = outerCos, w unused
};
uniform int   uLightCount;
uniform Light uLights[16];

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uEyePos - vWorldPos);
    vec3 base = uAlbedo * vColor.rgb * vTint.rgb;
    if (uHasAlbedoTex == 1) base *= texture(uAlbedoTex, vUV).rgb;

    vec3 result = base * uAmbient;
    for (int i = 0; i < uLightCount; ++i) {
        Light L = uLights[i];
        vec3 toLight;
        float atten = 1.0;
        if (L.posType.w < 0.5) {
            toLight = normalize(-L.posType.xyz);
        } else {
            vec3 d = L.posType.xyz - vWorldPos;
            float dist = length(d);
            toLight = d / max(dist, 1e-4);
            atten = clamp(1.0 - dist / max(L.params.x, 1e-3), 0.0, 1.0);
            atten *= atten;
            // Spot cone falloff is approximated by the same range falloff as
            // point lights (no direction is currently packed per-light) —
            // visually close enough for the small fixture lights we use it for.
        }
        float ndotl = max(dot(N, toLight), 0.0);
        vec3 H = normalize(toLight + V);
        float ndoth = max(dot(N, H), 0.0);
        float spec = pow(ndoth, mix(8.0, 128.0, 1.0 - uRoughness)) * (0.3 + uMetallic * 0.7);
        result += base * L.colorIntensity.rgb * L.colorIntensity.a * ndotl * atten;
        result += vec3(spec) * L.colorIntensity.rgb * L.colorIntensity.a * atten * ndotl;
    }
    result += base * uEmissive * vCustom.x;

    // Tonemap + gamma-correct before writing to the (non-sRGB) default
    // framebuffer. Without this the physically-linear lighting above —
    // ambient around 0.3, most surfaces well under 1.0 — reads as almost
    // solid black on screen: a linear 0.1 needs sRGB-encoding to ~0.35 to
    // look like a dim-but-visible surface instead of "no render at all".
    result = result / (result + vec3(1.0));
    result = pow(max(result, vec3(0.0)), vec3(1.0 / 2.2));
    FragColor = vec4(result, 1.0);
}
)GLSL";

inline const char* kUIVertex = R"GLSL(
#version 410 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform vec2 uScreenSize;
out vec2 vUV;
out vec4 vColor;
void main() {
    vec2 ndc = vec2(aPos.x / uScreenSize.x * 2.0 - 1.0, 1.0 - aPos.y / uScreenSize.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
}
)GLSL";

inline const char* kUIFragment = R"GLSL(
#version 410 core
in vec2 vUV;
in vec4 vColor;
out vec4 FragColor;
uniform sampler2D uFontTex;
uniform int uUseTex;
void main() {
    vec4 c = vColor;
    if (uUseTex == 1) c.a *= texture(uFontTex, vUV).r;
    FragColor = c;
}
)GLSL";

inline const char* kPostVertex = R"GLSL(
#version 410 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }
)GLSL";

inline const char* kPostFragment = R"GLSL(
#version 410 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform float uTime;
uniform float uBloom;
uniform float uVignette;
uniform float uGrain;
uniform vec2  uResolution;

void main() {
    vec3 color = texture(uScene, vUV).rgb;
    if (uBloom > 0.0) {
        vec3 bloom = vec3(0.0);
        vec2 texel = 1.0 / uResolution;
        for (int x = -2; x <= 2; ++x)
            for (int y = -2; y <= 2; ++y)
                bloom += max(texture(uScene, vUV + vec2(x, y) * texel * 2.0).rgb - 0.8, 0.0);
        color += bloom * (uBloom / 25.0);
    }
    vec2 uv = vUV - 0.5;
    float vig = 1.0 - dot(uv, uv) * uVignette;
    color *= clamp(vig, 0.0, 1.0);
    if (uGrain > 0.0) {
        float n = fract(sin(dot(vUV * uResolution + uTime, vec2(12.9898,78.233))) * 43758.5453);
        color += (n - 0.5) * uGrain;
    }
    color = color / (color + vec3(1.0));   // Reinhard tonemap
    color = pow(color, vec3(1.0/2.2));
    FragColor = vec4(color, 1.0);
}
)GLSL";

} // namespace hv::gfx::gl::shaders
