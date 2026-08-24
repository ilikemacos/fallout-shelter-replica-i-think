#include "core/Settings.hpp"
#include "core/Math.hpp"
#include "core/Log.hpp"
#include "core/Paths.hpp"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace hv {

const char* toString(BackendKind v) {
    switch (v) { case BackendKind::Auto: return "Auto";
                 case BackendKind::OpenGL: return "OpenGL";
                 case BackendKind::Vulkan: return "Vulkan"; }
    return "Auto";
}
const char* toString(WindowMode v) {
    switch (v) { case WindowMode::Windowed: return "Windowed";
                 case WindowMode::Fullscreen: return "Fullscreen";
                 case WindowMode::BorderlessFullscreen: return "Borderless"; }
    return "Windowed";
}
const char* toString(RayTracingMode v) {
    switch (v) { case RayTracingMode::Off: return "Off";
                 case RayTracingMode::VeryLight: return "Very Light";
                 case RayTracingMode::Low: return "Low"; }
    return "Off";
}
const char* toString(ShadowQuality v) {
    switch (v) { case ShadowQuality::Off: return "Off"; case ShadowQuality::Low: return "Low";
                 case ShadowQuality::Medium: return "Medium"; case ShadowQuality::High: return "High"; }
    return "High";
}
const char* toString(AOQuality v) {
    switch (v) { case AOQuality::Off: return "Off"; case AOQuality::Low: return "Low";
                 case AOQuality::High: return "High"; }
    return "High";
}
const char* toString(ReflectionQuality v) {
    switch (v) { case ReflectionQuality::Off: return "Off";
                 case ReflectionQuality::ScreenSpaceLow: return "SSR Low";
                 case ReflectionQuality::ScreenSpaceHigh: return "SSR High"; }
    return "Off";
}
const char* toString(QualityLevel v) {
    switch (v) { case QualityLevel::Low: return "Low"; case QualityLevel::Medium: return "Medium";
                 case QualityLevel::High: return "High"; case QualityLevel::Ultra: return "Ultra"; }
    return "High";
}

void GraphicsSettings::applyPreset(QualityLevel q) {
    switch (q) {
        case QualityLevel::Low:
            shadows = ShadowQuality::Low;  ambientOcclusion = AOQuality::Off;
            reflections = ReflectionQuality::Off; rayTracing = RayTracingMode::Off;
            volumetrics = false; bloom = false; renderScale = 0.75f; lodBias = 0.7f;
            break;
        case QualityLevel::Medium:
            shadows = ShadowQuality::Medium; ambientOcclusion = AOQuality::Low;
            reflections = ReflectionQuality::ScreenSpaceLow; rayTracing = RayTracingMode::Off;
            volumetrics = false; bloom = true; renderScale = 1.0f; lodBias = 0.85f;
            break;
        case QualityLevel::High:
            shadows = ShadowQuality::High; ambientOcclusion = AOQuality::High;
            reflections = ReflectionQuality::ScreenSpaceHigh; rayTracing = RayTracingMode::VeryLight;
            volumetrics = true; bloom = true; renderScale = 1.0f; lodBias = 1.0f;
            break;
        case QualityLevel::Ultra:
            shadows = ShadowQuality::High; ambientOcclusion = AOQuality::High;
            reflections = ReflectionQuality::ScreenSpaceHigh; rayTracing = RayTracingMode::Low;
            volumetrics = true; bloom = true; renderScale = 1.25f; lodBias = 1.3f;
            break;
    }
}

namespace {

template <typename E>
E enumFromInt(int v, int maxValue, E fallback) {
    return (v >= 0 && v <= maxValue) ? static_cast<E>(v) : fallback;
}

} // namespace

std::string Settings::serialize() const {
    std::ostringstream o;
    o << "# Haven settings. Edited in-game under Settings; hand edits are kept.\n";
    o << "version=1\n";
    const GraphicsSettings& g = graphics;
    o << "gfx.backend=" << int(g.backend) << "\n"
      << "gfx.windowMode=" << int(g.windowMode) << "\n"
      << "gfx.width=" << g.windowWidth << "\n"
      << "gfx.height=" << g.windowHeight << "\n"
      << "gfx.renderScale=" << g.renderScale << "\n"
      << "gfx.vsync=" << int(g.vsync) << "\n"
      << "gfx.fpsLimit=" << g.fpsLimit << "\n"
      << "gfx.highDPI=" << int(g.highDPI) << "\n"
      << "gfx.shadows=" << int(g.shadows) << "\n"
      << "gfx.ao=" << int(g.ambientOcclusion) << "\n"
      << "gfx.reflections=" << int(g.reflections) << "\n"
      << "gfx.rayTracing=" << int(g.rayTracing) << "\n"
      << "gfx.volumetrics=" << int(g.volumetrics) << "\n"
      << "gfx.particles=" << int(g.particles) << "\n"
      << "gfx.bloom=" << int(g.bloom) << "\n"
      << "gfx.filmGrain=" << int(g.filmGrain) << "\n"
      << "gfx.msaa=" << g.msaa << "\n"
      << "gfx.lodBias=" << g.lodBias << "\n"
      << "gfx.occlusionCulling=" << int(g.occlusionCulling) << "\n"
      << "gfx.instancing=" << int(g.instancing) << "\n"
      << "gfx.textureStreaming=" << int(g.textureStreaming) << "\n"
      << "gfx.dynamicResolution=" << int(g.dynamicResolution) << "\n"
      << "gfx.targetFps=" << g.targetFps << "\n";
    o << "audio.master=" << audio.master << "\n"
      << "audio.music=" << audio.music << "\n"
      << "audio.sfx=" << audio.sfx << "\n"
      << "audio.ambience=" << audio.ambience << "\n"
      << "audio.muted=" << int(audio.muted) << "\n";
    o << "game.cameraSpeed=" << gameplay.cameraSpeed << "\n"
      << "game.zoomSpeed=" << gameplay.zoomSpeed << "\n"
      << "game.invertDrag=" << int(gameplay.invertDrag) << "\n"
      << "game.edgeScroll=" << int(gameplay.edgeScroll) << "\n"
      << "game.autosave=" << int(gameplay.autosaveEnabled) << "\n"
      << "game.autosaveMinutes=" << gameplay.autosaveMinutes << "\n"
      << "game.tutorialHints=" << int(gameplay.tutorialHints) << "\n"
      << "game.pauseOnFocusLoss=" << int(gameplay.pauseOnFocusLoss) << "\n"
      << "debug.panel=" << int(showDebugPanel) << "\n";
    return o.str();
}

void Settings::deserialize(const std::string& text) {
    std::unordered_map<std::string, std::string> kv;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        while (!val.empty() && (val.back() == '\r' || val.back() == ' ')) val.pop_back();
        kv[key] = val;
    }
    auto getI = [&](const char* k, int def) {
        auto it = kv.find(k);
        return it == kv.end() ? def : std::atoi(it->second.c_str());
    };
    auto getF = [&](const char* k, f32 def) {
        auto it = kv.find(k);
        return it == kv.end() ? def : static_cast<f32>(std::atof(it->second.c_str()));
    };
    auto getB = [&](const char* k, bool def) { return getI(k, def ? 1 : 0) != 0; };

    GraphicsSettings& g = graphics;
    g.backend    = enumFromInt(getI("gfx.backend", 0), 2, BackendKind::Auto);
    g.windowMode = enumFromInt(getI("gfx.windowMode", 0), 2, WindowMode::Windowed);
    g.windowWidth  = std::max(640, getI("gfx.width",  g.windowWidth));
    g.windowHeight = std::max(400, getI("gfx.height", g.windowHeight));
    g.renderScale  = clampf(getF("gfx.renderScale", g.renderScale), 0.5f, 2.0f);
    g.vsync    = getB("gfx.vsync", g.vsync);
    g.fpsLimit = std::max(0, getI("gfx.fpsLimit", g.fpsLimit));
    g.highDPI  = getB("gfx.highDPI", g.highDPI);
    g.shadows  = enumFromInt(getI("gfx.shadows", 3), 3, ShadowQuality::High);
    g.ambientOcclusion = enumFromInt(getI("gfx.ao", 2), 2, AOQuality::High);
    g.reflections = enumFromInt(getI("gfx.reflections", 2), 2, ReflectionQuality::ScreenSpaceHigh);
    g.rayTracing  = enumFromInt(getI("gfx.rayTracing", 1), 2, RayTracingMode::VeryLight);
    g.volumetrics = getB("gfx.volumetrics", g.volumetrics);
    g.particles   = getB("gfx.particles", g.particles);
    g.bloom       = getB("gfx.bloom", g.bloom);
    g.filmGrain   = getB("gfx.filmGrain", g.filmGrain);
    g.msaa        = getI("gfx.msaa", g.msaa);
    g.lodBias     = clampf(getF("gfx.lodBias", g.lodBias), 0.4f, 2.0f);
    g.occlusionCulling  = getB("gfx.occlusionCulling", g.occlusionCulling);
    g.instancing        = getB("gfx.instancing", g.instancing);
    g.textureStreaming  = getB("gfx.textureStreaming", g.textureStreaming);
    g.dynamicResolution = getB("gfx.dynamicResolution", g.dynamicResolution);
    g.targetFps = std::max(30, getI("gfx.targetFps", g.targetFps));

    audio.master   = saturate(getF("audio.master", audio.master));
    audio.music    = saturate(getF("audio.music", audio.music));
    audio.sfx      = saturate(getF("audio.sfx", audio.sfx));
    audio.ambience = saturate(getF("audio.ambience", audio.ambience));
    audio.muted    = getB("audio.muted", audio.muted);

    gameplay.cameraSpeed = clampf(getF("game.cameraSpeed", gameplay.cameraSpeed), 0.25f, 4.0f);
    gameplay.zoomSpeed   = clampf(getF("game.zoomSpeed", gameplay.zoomSpeed), 0.25f, 4.0f);
    gameplay.invertDrag  = getB("game.invertDrag", gameplay.invertDrag);
    gameplay.edgeScroll  = getB("game.edgeScroll", gameplay.edgeScroll);
    gameplay.autosaveEnabled = getB("game.autosave", gameplay.autosaveEnabled);
    gameplay.autosaveMinutes = std::max(1, getI("game.autosaveMinutes", gameplay.autosaveMinutes));
    gameplay.tutorialHints   = getB("game.tutorialHints", gameplay.tutorialHints);
    gameplay.pauseOnFocusLoss = getB("game.pauseOnFocusLoss", gameplay.pauseOnFocusLoss);
    showDebugPanel = getB("debug.panel", showDebugPanel);
}

bool Settings::load(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    std::fclose(f);
    deserialize(text);
    return true;
}

bool Settings::save(const std::string& path) const {
    paths::ensureDirectory(path.substr(0, path.find_last_of('/')));
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const std::string text = serialize();
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fflush(f);
    std::fclose(f);
    if (!ok) { paths::removeFile(tmp); return false; }
    return paths::renameFile(tmp, path);
}

} // namespace hv
