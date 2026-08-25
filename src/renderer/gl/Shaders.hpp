#pragma once
// Embedded GLSL 4.1 core source. Keeping shaders in the binary (rather than
// only as loose files) means a build can never fail to find them; the
// shaders/ directory is still shipped for reference and for the Vulkan
// SPIR-V pipeline built from the same shading logic.
namespace hv::gfx::gl::shaders {

// Up to kMaxLights are uploaded per pass; LightingSystem::nearest() already
// trims to the closest ones so a large shelter stays within budget.
constexpr int kMaxLights = 16;
/// Must match kMaxBoxes in the scene fragment shader's box-soup uniforms.
constexpr int kMaxShadowBoxes = 48;

/// Tone-mapping and ambient constants, in one place so the game and the
/// offscreen shader preview (tools/shader_preview.cpp) can never drift apart.
/// These are linear-HDR inputs to the filmic tonemap, not final pixel values.
struct ToneParams {
    float exposure;
    float ambientSky[3];      ///< light from above: warm ceiling fixtures
    float ambientGround[3];   ///< light bounced back up off the floor: cooler
    float fogDensity;
    float fogColor[3];
};
inline constexpr ToneParams kTone = {
    // Deliberately low ambient and a strong key light: the shelter should
    // read as lit *by its fixtures*, with real cast shadows between them,
    // not as a uniformly-lit box. Ambient this low is what lets the
    // ray-traced shadows actually show up.
    1.28f,
    {0.265f, 0.238f, 0.205f},
    {0.105f, 0.112f, 0.132f},
    0.026f,
    {0.075f, 0.066f, 0.056f},
};

/// Colour grade applied after the filmic tonemap. Cool teal in the shadows,
/// warm amber in the highlights, slightly desaturated overall — the
/// grimy, high-contrast post-apocalyptic look.
struct GradeParams {
    float saturation;
    float shadowTint[3];
    float highlightTint[3];
    float contrast;
    float vignette;
};
inline constexpr GradeParams kGrade = {
    0.92f,
    {0.90f, 0.98f, 1.10f},   // shadows drift teal
    {1.16f, 1.03f, 0.80f},   // highlights drift amber
    1.07f,
    0.22f,
};

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
uniform int   uSurfaceKind;
uniform float uTexScale;
uniform float uExposure;
uniform float uFogDensity;
uniform vec3  uFogColor;
uniform float uSaturation;
uniform vec3  uShadowTint;
uniform vec3  uHighlightTint;
uniform float uContrast;
uniform float uVignette;
uniform vec2  uViewportSize;

// "Sky and bounce" ambient rather than one flat constant: light arriving from
// above (ceiling fixtures, the shaft) and light bounced up off the floor are
// different colours, so shadowed surfaces keep colour instead of going grey.
uniform vec3  uAmbientSky;
uniform vec3  uAmbientGround;

struct Light {
    vec4 posType;        // xyz = position (or -direction for directional), w = type
    vec4 colorIntensity; // rgb = colour, a = intensity
    vec4 params;         // x = range
};
uniform int   uLightCount;
uniform Light uLights[16];

// The "box soup" the shadow rays are traced against — coarse world boxes
// (one per room), not triangles, so a slab test per box per pixel is cheap.
const int kMaxBoxes = 48;
uniform int  uBoxCount;
uniform vec3 uBoxMin[kMaxBoxes];
uniform vec3 uBoxMax[kMaxBoxes];
uniform int  uRayTracedShadows;   // 0 off, 1 sun only, 2 sun + nearest fixtures

// ---------------------------------------------------------------------------
//  Noise
// ---------------------------------------------------------------------------
vec3 hash33(vec3 p) {
    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)),
             dot(p, vec3(269.5, 183.3, 246.1)),
             dot(p, vec3(113.5, 271.9, 124.6)));
    return fract(sin(p) * 43758.5453123) * 2.0 - 1.0;
}
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Gradient (Perlin-style) noise — smoother and less blocky than value noise,
// which matters when it is driving bump detail rather than just colour.
float gnoise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(dot(hash33(i + vec3(0,0,0)), f - vec3(0,0,0)),
                       dot(hash33(i + vec3(1,0,0)), f - vec3(1,0,0)), u.x),
                   mix(dot(hash33(i + vec3(0,1,0)), f - vec3(0,1,0)),
                       dot(hash33(i + vec3(1,1,0)), f - vec3(1,1,0)), u.x), u.y),
               mix(mix(dot(hash33(i + vec3(0,0,1)), f - vec3(0,0,1)),
                       dot(hash33(i + vec3(1,0,1)), f - vec3(1,0,1)), u.x),
                   mix(dot(hash33(i + vec3(0,1,1)), f - vec3(0,1,1)),
                       dot(hash33(i + vec3(1,1,1)), f - vec3(1,1,1)), u.x), u.y), u.z);
}

float fbm(vec3 p, int octaves) {
    float sum = 0.0, amp = 0.5, norm = 0.0;
    for (int i = 0; i < 6; ++i) {
        if (i >= octaves) break;
        sum += gnoise(p) * amp;
        norm += amp;
        p = p * 2.03 + vec3(11.3, 7.1, 5.7);
        amp *= 0.5;
    }
    return sum / max(norm, 1e-4);
}

// Cellular noise: F1 is distance to the nearest feature point, F2 to the
// second. F2-F1 gives clean cell borders (mortar, cracks, tile grout) while
// F1 alone gives blobs (concrete aggregate, rust pitting).
vec2 worley(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    float f1 = 8.0, f2 = 8.0;
    for (int x = -1; x <= 1; ++x)
    for (int y = -1; y <= 1; ++y)
    for (int z = -1; z <= 1; ++z) {
        vec3 g = vec3(x, y, z);
        vec3 o = hash33(i + g) * 0.5 + 0.5;
        float d = length(g + o - f);
        if (d < f1) { f2 = f1; f1 = d; } else if (d < f2) { f2 = d; }
    }
    return vec2(f1, f2);
}

// ---------------------------------------------------------------------------
//  Triplanar helpers — 2D lattice patterns (brick, tile, plank) projected
//  along whichever axis the surface actually faces, so nothing stretches.
// ---------------------------------------------------------------------------
vec3 triWeights(vec3 n) {
    vec3 w = pow(abs(n), vec3(6.0));
    return w / max(w.x + w.y + w.z, 1e-4);
}
vec2 triCoords(vec3 p, vec3 n) {
    vec3 w = triWeights(n);
    return w.x > max(w.y, w.z) ? p.zy : (w.y > w.z ? p.xz : p.xy);
}

/// Running-bond lattice. Returns (mortar mask 0=joint 1=face, per-unit hash).
vec2 bondPattern(vec2 uv, vec2 cell, float joint) {
    uv /= cell;
    float row = floor(uv.y);
    uv.x += 0.5 * mod(row, 2.0);
    vec2 id = floor(uv);
    vec2 f = fract(uv);
    vec2 d = min(f, 1.0 - f) * cell;
    float m = smoothstep(0.0, joint, min(d.x, d.y));
    return vec2(m, hash12(id + row * 0.37));
}

// ---------------------------------------------------------------------------
//  Height field per material — drives bump detail and cavity AO.
// ---------------------------------------------------------------------------
float surfaceHeight(vec3 p, vec3 n, int kind) {
    if (kind == 0) {          // Concrete
        return fbm(p * 1.1, 3) * 0.6 + fbm(p * 9.0, 3) * 0.4;
    } else if (kind == 1) {   // Brick
        vec2 b = bondPattern(triCoords(p, n), vec2(0.62, 0.26), 0.035);
        return b.x * 0.85 + fbm(p * 14.0, 2) * 0.15 + b.y * 0.05;
    } else if (kind == 2) {   // Rusted metal
        return fbm(p * 3.0, 3) * 0.5 + fbm(p * 22.0, 3) * 0.5;
    } else if (kind == 3) {   // Painted metal
        vec2 b = bondPattern(triCoords(p, n), vec2(1.6, 1.1), 0.03);
        return b.x * 0.7 + fbm(p * 20.0, 2) * 0.3;
    } else if (kind == 4) {   // Tile
        vec2 b = bondPattern(triCoords(p, n), vec2(0.34, 0.34), 0.028);
        return b.x * 0.9 + fbm(p * 30.0, 2) * 0.1;
    } else if (kind == 5) {   // Brushed metal
        vec2 uv = triCoords(p, n);
        return fbm(vec3(uv.x * 60.0, uv.y * 2.0, 0.0), 3) * 0.7 + fbm(p * 25.0, 2) * 0.3;
    } else if (kind == 6) {   // Wood
        vec2 uv = triCoords(p, n);
        float rings = sin(uv.y * 26.0 + fbm(p * 2.0, 3) * 7.0);
        return rings * 0.5 + 0.5;
    } else if (kind == 7) {   // Fabric
        vec2 uv = triCoords(p, n) * 90.0;
        return (sin(uv.x) * sin(uv.y)) * 0.5 + 0.5;
    } else if (kind == 10) {  // Dirt
        return fbm(p * 6.0, 4);
    } else if (kind == 11) {  // Skin
        return fbm(p * 60.0, 2) * 0.5 + 0.5;
    }
    return fbm(p * 12.0, 2) * 0.5 + 0.5;   // Glass / plastic / emissive: near flat
}

/// Perturbs the geometric normal by the gradient of the height field.
vec3 bumpNormal(vec3 n, vec3 p, int kind, float strength) {
    if (strength <= 0.0) return n;
    float e = 0.012;
    float h  = surfaceHeight(p, n, kind);
    float hx = surfaceHeight(p + vec3(e, 0.0, 0.0), n, kind);
    float hy = surfaceHeight(p + vec3(0.0, e, 0.0), n, kind);
    float hz = surfaceHeight(p + vec3(0.0, 0.0, e), n, kind);
    vec3 grad = vec3(hx - h, hy - h, hz - h) / e;
    grad -= n * dot(n, grad);          // keep only the tangential part
    return normalize(n - grad * strength);
}

struct Surface {
    vec3  albedo;
    float roughness;
    float metallic;
    float ao;
};

Surface evaluateSurface(vec3 p, vec3 n, int kind, vec3 tint) {
    Surface s;
    s.albedo = tint;
    s.roughness = uRoughness;
    s.metallic = uMetallic;
    s.ao = 1.0;

    float h = surfaceHeight(p, n, kind);

    if (kind == 0) {                       // Concrete
        vec2 w = worley(p * 13.0);
        float aggregate = smoothstep(0.35, 0.0, w.x);          // embedded stones
        float stain = smoothstep(0.15, 0.75, fbm(p * 0.8, 4) * 0.5 + 0.5);
        float grime = smoothstep(0.6, 0.0, h);                  // settles in dips
        float boards = smoothstep(0.02, 0.0, abs(fract(triCoords(p, n).y * 0.55) - 0.5) - 0.46);
        s.albedo = tint * (0.45 + 0.85 * h);
        s.albedo = mix(s.albedo, tint * 1.35, aggregate * 0.55);
        s.albedo = mix(s.albedo, tint * vec3(0.42, 0.41, 0.40), stain * 0.7);
        s.albedo = mix(s.albedo, tint * 0.55, boards * 0.5);   // board-form seams
        s.albedo *= 1.0 - grime * 0.55;
        s.roughness = clamp(0.86 + 0.12 * h - aggregate * 0.15, 0.05, 1.0);
        s.ao = 1.0 - grime * 0.6;
    } else if (kind == 1) {                // Brick
        vec2 b = bondPattern(triCoords(p, n), vec2(0.62, 0.26), 0.035);
        vec3 brickCol = tint * (0.72 + 0.55 * b.y);
        brickCol *= 0.85 + 0.3 * fbm(p * 18.0, 3);
        vec3 mortar = vec3(0.44, 0.43, 0.40) * (0.8 + 0.4 * fbm(p * 25.0, 2));
        s.albedo = mix(mortar, brickCol, b.x);
        s.roughness = mix(0.95, 0.82, b.x);
        s.ao = mix(0.55, 1.0, b.x);
    } else if (kind == 2) {                // Rusted metal
        // NB: 'patch' is a reserved GLSL keyword — this must not be named that.
        float oxide = smoothstep(0.25, 0.75, fbm(p * 2.4, 4) * 0.5 + 0.5);
        vec2 w = worley(p * 20.0);
        float pit = smoothstep(0.28, 0.0, w.x);
        vec3 steel = vec3(0.38, 0.39, 0.42);
        vec3 rust  = tint * vec3(1.15, 0.62, 0.32);
        s.albedo = mix(steel, rust, clamp(oxide + pit * 0.5, 0.0, 1.0));
        s.albedo *= 0.75 + 0.5 * h;
        s.metallic = mix(0.85, 0.08, oxide);
        s.roughness = mix(0.38, 0.95, oxide);
        s.ao = 1.0 - pit * 0.4;
    } else if (kind == 3) {                // Painted metal, chipping on edges
        vec2 b = bondPattern(triCoords(p, n), vec2(1.6, 1.1), 0.03);
        float wear = smoothstep(0.55, 0.95, fbm(p * 7.0, 4) * 0.5 + 0.5);
        vec3 paint = tint * (0.9 + 0.25 * fbm(p * 16.0, 2));
        vec3 bare  = vec3(0.40, 0.41, 0.44);
        s.albedo = mix(paint, bare, wear * 0.8);
        s.albedo = mix(s.albedo * 0.5, s.albedo, b.x);          // panel seams
        s.metallic = mix(0.15, 0.8, wear);
        s.roughness = mix(0.55, 0.42, wear);
        s.ao = mix(0.6, 1.0, b.x);
    } else if (kind == 4) {                // Tile
        vec2 b = bondPattern(triCoords(p, n), vec2(0.34, 0.34), 0.028);
        vec3 face = tint * (0.85 + 0.3 * b.y);
        vec3 grout = vec3(0.5, 0.49, 0.46);
        s.albedo = mix(grout, face, b.x);
        s.roughness = mix(0.9, 0.28, b.x);
        s.ao = mix(0.6, 1.0, b.x);
    } else if (kind == 5) {                // Brushed metal
        s.albedo = tint * (0.8 + 0.35 * h);
        s.metallic = 0.85;
        s.roughness = clamp(0.30 + 0.25 * h, 0.05, 1.0);
    } else if (kind == 6) {                // Wood
        float knots = smoothstep(0.72, 1.0, fbm(p * 3.5, 3) * 0.5 + 0.5);
        s.albedo = tint * (0.62 + 0.5 * h);
        s.albedo = mix(s.albedo, tint * 0.42, knots);
        s.roughness = 0.78 - 0.1 * h;
        s.ao = 1.0 - knots * 0.3;
    } else if (kind == 7) {                // Fabric
        s.albedo = tint * (0.75 + 0.35 * h);
        s.roughness = 0.95;
        s.ao = 0.85 + 0.15 * h;
    } else if (kind == 8) {                // Glass
        s.albedo = tint;
        s.roughness = 0.06;
        s.metallic = 0.0;
    } else if (kind == 9) {                // Plastic
        s.albedo = tint * (0.9 + 0.15 * h);
        s.roughness = 0.45;
    } else if (kind == 10) {               // Dirt
        vec2 w = worley(p * 16.0);
        s.albedo = tint * (0.65 + 0.55 * h);
        s.albedo = mix(s.albedo, tint * 1.2, smoothstep(0.3, 0.0, w.x) * 0.4);
        s.roughness = 0.97;
        s.ao = 0.8 + 0.2 * h;
    } else if (kind == 11) {               // Skin
        s.albedo = tint * (0.94 + 0.1 * h);
        s.roughness = 0.68;
    } else if (kind == 12) {               // Emissive panel
        s.albedo = tint;
        s.roughness = 0.35;
    }
    return s;
}

// ---------------------------------------------------------------------------
//  Ray-traced shadows: real rays traced per pixel against the box soup,
//  one slab test per box. Cheap because the soup is rooms, not triangles.
// ---------------------------------------------------------------------------
bool insideBox(vec3 p, vec3 lo, vec3 hi) {
    return all(greaterThanEqual(p, lo)) && all(lessThanEqual(p, hi));
}

bool slabHit(vec3 ro, vec3 rd, vec3 lo, vec3 hi, float maxT) {
    // Component-wise reciprocal: a zero component yields +/-inf, which the
    // min/max below handle correctly for rays parallel to a slab.
    vec3 inv = 1.0 / rd;
    vec3 t0 = (lo - ro) * inv;
    vec3 t1 = (hi - ro) * inv;
    vec3 tsmall = min(t0, t1);
    vec3 tbig   = max(t0, t1);
    float tmin = max(max(tsmall.x, tsmall.y), tsmall.z);
    float tmax = min(min(tbig.x, tbig.y), tbig.z);
    return tmax >= max(tmin, 0.0) && tmin < maxT;
}

float traceShadow(vec3 origin, vec3 dir, float maxT) {
    for (int i = 0; i < kMaxBoxes; ++i) {
        if (i >= uBoxCount) break;
        vec3 lo = uBoxMin[i], hi = uBoxMax[i];
        // The room the shading point sits in must not shadow itself.
        if (insideBox(origin, lo - vec3(0.05), hi + vec3(0.05))) continue;
        if (slabHit(origin, dir, lo, hi, maxT)) return 0.0;
    }
    return 1.0;
}

// Narkowicz's ACES fit: rolls bright surfaces off smoothly toward white
// instead of dividing them down, while leaving mid/low values near their
// input, so shadowed surfaces keep their colour instead of crushing.
vec3 acesFilm(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 Ng = normalize(vNormal);
    vec3 P = vWorldPos * uTexScale;
    vec3 V = normalize(uEyePos - vWorldPos);

    vec3 tint = uAlbedo * vColor.rgb * vTint.rgb;
    Surface surf = evaluateSurface(P, Ng, uSurfaceKind, tint);

    // Fade bump detail out with distance so distant geometry doesn't shimmer.
    float viewDist = length(uEyePos - vWorldPos);
    float detail = 1.0 - smoothstep(18.0, 55.0, viewDist);
    vec3 N = bumpNormal(Ng, P, uSurfaceKind, 0.06 * detail);

    // Sky-and-bounce ambient, modulated by the material's own cavity AO.
    float up = N.y * 0.5 + 0.5;
    vec3 ambient = mix(uAmbientGround, uAmbientSky, up);
    vec3 result = surf.albedo * ambient * surf.ao;

    for (int i = 0; i < 16; ++i) {
        if (i >= uLightCount) break;
        Light L = uLights[i];
        vec3 toLight;
        float atten = 1.0;
        float maxT;
        bool directional = L.posType.w < 0.5;
        if (directional) {
            toLight = normalize(-L.posType.xyz);
            maxT = 1e4;
        } else {
            vec3 d = L.posType.xyz - vWorldPos;
            float dist = length(d);
            toLight = d / max(dist, 1e-4);
            atten = clamp(1.0 - dist / max(L.params.x, 1e-3), 0.0, 1.0);
            atten *= atten;
            maxT = dist;
        }
        float ndotl = max(dot(N, toLight), 0.0);
        if (ndotl <= 0.0 || atten <= 0.0) continue;

        float shadow = 1.0;
        bool traceThis = uRayTracedShadows > 0 &&
                         (directional || (uRayTracedShadows > 1 && i < 3));
        if (traceThis) {
            shadow = traceShadow(vWorldPos + Ng * 0.06, toLight, maxT - 0.12);
        }
        if (shadow <= 0.0) continue;

        vec3 radiance = L.colorIntensity.rgb * L.colorIntensity.a * atten * shadow;
        vec3 H = normalize(toLight + V);
        float ndoth = max(dot(N, H), 0.0);
        float spec = pow(ndoth, mix(8.0, 220.0, 1.0 - surf.roughness)) *
                     (0.25 + surf.metallic * 0.9);
        result += surf.albedo * radiance * ndotl;
        result += mix(vec3(spec), surf.albedo * spec, surf.metallic) * radiance * ndotl;
    }

    result += surf.albedo * uEmissive * vCustom.x;

    // Distance haze tinted toward the shelter's own light rather than neutral
    // grey, so far corridors recede into warm dark instead of a flat wall.
    float fog = 1.0 - exp(-viewDist * uFogDensity);
    result = mix(result, uFogColor, clamp(fog, 0.0, 0.85));

    // Linear HDR in, filmic-tonemapped and gamma-encoded out. Without this
    // the linear values above read as near-black on a non-sRGB framebuffer.
    result *= uExposure;
    result = acesFilm(result);

    // --- colour grade ------------------------------------------------------
    // Split-tone: cool the shadows, warm the highlights. Done on the
    // tonemapped (0..1) signal so the tints are perceptual, not physical.
    float luma = dot(result, vec3(0.2126, 0.7152, 0.0722));
    result *= mix(uShadowTint, uHighlightTint, smoothstep(0.15, 0.75, luma));
    result = mix(vec3(luma), result, uSaturation);
    result = clamp((result - 0.5) * uContrast + 0.5, 0.0, 1.0);

    // Vignette, computed from the fragment's own screen position.
    vec2 ndc = gl_FragCoord.xy / max(uViewportSize, vec2(1.0)) - 0.5;
    result *= clamp(1.0 - dot(ndc, ndc) * uVignette, 0.0, 1.0);

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
