"""OpenGL 4.1 core-profile renderer for Haven, with an SDL software fallback.

The game composes each frame into a single high-resolution pygame Surface.
That surface is streamed to the GPU through double-buffered pixel buffer
objects and presented through a post-processing chain:

    scene ──► bright-pass ──► two-tap separable gaussian (ping-pong FBOs)
       │                              │
       └──────────► composite ◄───────┘
                        │
                   colour grade · bloom · vignette · scanlines
                   chromatic aberration · ordered-dither grain

Why 4.1 and not a higher version: macOS caps out at OpenGL 4.1 core
(Apple froze its GL implementation there), so 4.1 is the highest version
that is genuinely portable across macOS and Windows. Every feature used
here — core-profile VAOs, FBOs, ``#version 410 core`` GLSL, PBO streaming,
instanced-free fullscreen triangles — is 4.1-clean.

If PyOpenGL is unavailable or context creation fails, ``create_renderer``
transparently falls back to plain SDL blitting so the game always runs.
"""

from __future__ import annotations

import ctypes
import math
import os
import sys

import pygame

from . import config as C

# PyOpenGL is optional; the game degrades to SDL rendering without it.
try:
    from OpenGL import GL
    _HAVE_GL = True
except Exception:  # pragma: no cover - depends on install
    GL = None
    _HAVE_GL = False


# --------------------------------------------------------------------------
# Shaders (GLSL 410 core)
# --------------------------------------------------------------------------

# A single oversized triangle covers the viewport with no vertex buffer.
_VERT = """
#version 410 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
"""

# Isolate the highlights that will bloom.
_FRAG_BRIGHT = """
#version 410 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform float uThreshold;
uniform float uSoftKnee;

void main() {
    vec3 c = texture(uScene, vec2(vUV.x, 1.0 - vUV.y)).rgb;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    // soft-knee threshold keeps the bloom from popping on/off
    float knee = max(1e-4, uThreshold * uSoftKnee);
    float soft = clamp((lum - uThreshold + knee) / (2.0 * knee), 0.0, 1.0);
    float w = max(soft * soft * (lum > uThreshold - knee ? 1.0 : 0.0),
                  step(uThreshold, lum));
    FragColor = vec4(c * w, 1.0);
}
"""

# Separable gaussian; uDir selects the horizontal or vertical pass.
_FRAG_BLUR = """
#version 410 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex;
uniform vec2 uTexel;
uniform vec2 uDir;

void main() {
    // 9-tap gaussian collapsed to 5 linearly-filtered samples
    const float off[3] = float[](0.0, 1.3846153846, 3.2307692308);
    const float wt[3]  = float[](0.2270270270, 0.3162162162, 0.0702702703);
    vec2 uv = vUV;
    vec3 acc = texture(uTex, uv).rgb * wt[0];
    for (int i = 1; i < 3; ++i) {
        vec2 d = uDir * uTexel * off[i];
        acc += texture(uTex, uv + d).rgb * wt[i];
        acc += texture(uTex, uv - d).rgb * wt[i];
    }
    FragColor = vec4(acc, 1.0);
}
"""

_FRAG_COMPOSITE = """
#version 410 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform vec2  uResolution;
uniform float uTime;
uniform float uBloomStrength;
uniform float uVignette;
uniform float uScanline;
uniform float uAberration;
uniform float uGrain;
uniform float uSaturation;
uniform float uContrast;

// Bayer 8x8 ordered dither — breaks up banding in the dark shelter gradients
// far more cheaply than a noise texture lookup.
float bayer(vec2 p) {
    int x = int(mod(p.x, 8.0));
    int y = int(mod(p.y, 8.0));
    int i = y * 8 + x;
    const int m[64] = int[](
         0,32, 8,40, 2,34,10,42,   48,16,56,24,50,18,58,26,
        12,44, 4,36,14,46, 6,38,   60,28,52,20,62,30,54,22,
         3,35,11,43, 1,33, 9,41,   51,19,59,27,49,17,57,25,
        15,47, 7,39,13,45, 5,37,   63,31,55,23,61,29,53,21);
    return float(m[i]) / 64.0 - 0.5;
}

void main() {
    vec2 uv = vec2(vUV.x, 1.0 - vUV.y);
    vec2 centred = uv - 0.5;

    // Chromatic aberration grows toward the edges of the screen.
    vec3 col;
    if (uAberration > 0.0) {
        vec2 off = centred * uAberration * 0.004;
        col.r = texture(uScene, uv + off).r;
        col.g = texture(uScene, uv).g;
        col.b = texture(uScene, uv - off).b;
    } else {
        col = texture(uScene, uv).rgb;
    }

    // Additive bloom.
    if (uBloomStrength > 0.0) {
        vec3 b = texture(uBloom, uv).rgb;
        col += b * uBloomStrength;
    }

    // Contrast around mid grey, then saturation.
    col = (col - 0.5) * uContrast + 0.5;
    float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = mix(vec3(lum), col, uSaturation);

    // Warm the highlights, cool the shadows — subtle underground grade.
    col *= mix(vec3(0.94, 0.97, 1.06), vec3(1.06, 1.01, 0.92),
               smoothstep(0.0, 0.85, lum));

    // Vignette.
    if (uVignette > 0.0) {
        float d = length(centred * vec2(uResolution.x / uResolution.y, 1.0));
        col *= mix(1.0, smoothstep(0.95, 0.28, d), uVignette);
    }

    // Very fine scanlines, scaled so they stay a constant physical size.
    if (uScanline > 0.0) {
        float line = sin(uv.y * uResolution.y * 3.14159265);
        col *= 1.0 - uScanline * 0.06 * line * line;
    }

    // Animated grain, kept low so it reads as film rather than noise.
    if (uGrain > 0.0) {
        float n = fract(sin(dot(uv * uResolution + uTime * 37.0,
                                vec2(12.9898, 78.233))) * 43758.5453);
        col += (n - 0.5) * uGrain * 0.06;
    }

    col += bayer(gl_FragCoord.xy) / 255.0;
    FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
"""


# --------------------------------------------------------------------------
# Quality presets
# --------------------------------------------------------------------------
# Bloom is deliberately restrained: the art is already high-contrast pixel work,
# and anything above ~0.3 turns lamp-lit room ceilings into white fog.
QUALITY_PRESETS = {
    "low":    dict(bloom=0.0,  vignette=0.14, scanline=0.0,  aberration=0.0,
                   grain=0.0,  bloom_div=4, saturation=1.0,  contrast=1.0),
    "medium": dict(bloom=0.16, vignette=0.24, scanline=0.0,  aberration=0.0,
                   grain=0.18, bloom_div=4, saturation=1.04, contrast=1.02),
    "high":   dict(bloom=0.24, vignette=0.30, scanline=0.18, aberration=0.35,
                   grain=0.24, bloom_div=2, saturation=1.07, contrast=1.03),
    "ultra":  dict(bloom=0.30, vignette=0.34, scanline=0.24, aberration=0.55,
                   grain=0.28, bloom_div=2, saturation=1.09, contrast=1.04),
}


class RendererError(RuntimeError):
    pass


# --------------------------------------------------------------------------
# SDL fallback
# --------------------------------------------------------------------------
class SDLRenderer:
    """Plain SDL2 presentation. Always available."""

    name = "SDL"
    gl_info = "software (SDL2 surface blit)"

    def __init__(self, size, flags):
        self.screen = pygame.display.set_mode(size, flags)
        self._surface = self.screen

    @property
    def size(self):
        return self.screen.get_size()

    def begin(self) -> pygame.Surface:
        return self._surface

    def present(self, dt: float = 0.0):
        pygame.display.flip()

    def resize(self, size, flags):
        self.screen = pygame.display.set_mode(size, flags)
        self._surface = self.screen

    def set_quality(self, preset: str):
        pass

    def shutdown(self):
        pass


# --------------------------------------------------------------------------
# OpenGL 4.1 core renderer
# --------------------------------------------------------------------------
class GLRenderer:
    """OpenGL 4.1 core-profile renderer with a post-processing chain."""

    name = "OpenGL"

    def __init__(self, size, flags, quality: str = "high", vsync: bool = True):
        if not _HAVE_GL:
            raise RendererError("PyOpenGL is not installed")

        pygame.display.gl_set_attribute(pygame.GL_CONTEXT_MAJOR_VERSION, 4)
        pygame.display.gl_set_attribute(pygame.GL_CONTEXT_MINOR_VERSION, 1)
        pygame.display.gl_set_attribute(
            pygame.GL_CONTEXT_PROFILE_MASK, pygame.GL_CONTEXT_PROFILE_CORE)
        pygame.display.gl_set_attribute(pygame.GL_DOUBLEBUFFER, 1)
        # Forward-compatible is required to get a core context on macOS.
        if sys.platform == "darwin":
            pygame.display.gl_set_attribute(pygame.GL_CONTEXT_FORWARD_COMPATIBLE_FLAG, 1)

        gl_flags = flags | pygame.OPENGL | pygame.DOUBLEBUF
        # vsync is a request, not a guarantee; SDL warns rather than fails when
        # the driver cannot honour it, so keep that noise out of the console.
        self.screen = None
        if vsync:
            import warnings
            try:
                with warnings.catch_warnings():
                    warnings.simplefilter("ignore")
                    self.screen = pygame.display.set_mode(size, gl_flags, vsync=1)
            except (TypeError, pygame.error):
                self.screen = None
        if self.screen is None:
            try:
                self.screen = pygame.display.set_mode(size, gl_flags)
            except pygame.error as exc:
                raise RendererError(f"could not create an OpenGL context: {exc}") from exc

        try:
            ver = GL.glGetString(GL.GL_VERSION)
            if ver is None:
                raise RendererError("OpenGL context is not current")
            self.gl_info = (f"{GL.glGetString(GL.GL_RENDERER).decode(errors='replace')} · "
                            f"GL {ver.decode(errors='replace')}")
        except RendererError:
            raise
        except Exception as exc:
            raise RendererError(f"OpenGL query failed: {exc}") from exc

        self._quality = quality if quality in QUALITY_PRESETS else "high"
        self._progs = {}
        self._fbos = {}
        self._pbos = []
        self._pbo_index = 0
        self._surface = None
        self._time = 0.0

        self._build_programs()
        self._vao = GL.glGenVertexArrays(1)
        self._alloc(size)

    # -- setup ------------------------------------------------------------
    def _compile(self, src, stage):
        sid = GL.glCreateShader(stage)
        GL.glShaderSource(sid, src)
        GL.glCompileShader(sid)
        if GL.glGetShaderiv(sid, GL.GL_COMPILE_STATUS) != GL.GL_TRUE:
            log = GL.glGetShaderInfoLog(sid)
            if isinstance(log, bytes):
                log = log.decode(errors="replace")
            raise RendererError(f"shader compile failed: {log}")
        return sid

    def _link(self, frag_src):
        vs = self._compile(_VERT, GL.GL_VERTEX_SHADER)
        fs = self._compile(frag_src, GL.GL_FRAGMENT_SHADER)
        pid = GL.glCreateProgram()
        GL.glAttachShader(pid, vs)
        GL.glAttachShader(pid, fs)
        GL.glLinkProgram(pid)
        if GL.glGetProgramiv(pid, GL.GL_LINK_STATUS) != GL.GL_TRUE:
            log = GL.glGetProgramInfoLog(pid)
            if isinstance(log, bytes):
                log = log.decode(errors="replace")
            raise RendererError(f"program link failed: {log}")
        GL.glDeleteShader(vs)
        GL.glDeleteShader(fs)
        return pid

    def _build_programs(self):
        self._progs["bright"] = self._link(_FRAG_BRIGHT)
        self._progs["blur"] = self._link(_FRAG_BLUR)
        self._progs["composite"] = self._link(_FRAG_COMPOSITE)

    def _make_target(self, w, h):
        w, h = max(1, int(w)), max(1, int(h))
        tex = GL.glGenTextures(1)
        GL.glBindTexture(GL.GL_TEXTURE_2D, tex)
        GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA8, w, h, 0,
                        GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, None)
        GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MIN_FILTER, GL.GL_LINEAR)
        GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MAG_FILTER, GL.GL_LINEAR)
        GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_WRAP_S, GL.GL_CLAMP_TO_EDGE)
        GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_WRAP_T, GL.GL_CLAMP_TO_EDGE)
        fbo = GL.glGenFramebuffers(1)
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, fbo)
        GL.glFramebufferTexture2D(GL.GL_FRAMEBUFFER, GL.GL_COLOR_ATTACHMENT0,
                                  GL.GL_TEXTURE_2D, tex, 0)
        status = GL.glCheckFramebufferStatus(GL.GL_FRAMEBUFFER)
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, 0)
        if status != GL.GL_FRAMEBUFFER_COMPLETE:
            raise RendererError(f"incomplete framebuffer ({status})")
        return dict(tex=tex, fbo=fbo, w=w, h=h)

    def _alloc(self, size):
        w, h = int(size[0]), int(size[1])
        self._w, self._h = w, h

        # CPU-side compositing surface. 32-bit with no per-pixel alpha so the
        # memory layout is BGRA, matching GL_BGRA for a zero-conversion upload.
        self._surface = pygame.Surface((w, h), 0, 32)

        # Scene texture streamed from the CPU each frame.
        self._scene_tex = GL.glGenTextures(1)
        GL.glBindTexture(GL.GL_TEXTURE_2D, self._scene_tex)
        GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA8, w, h, 0,
                        GL.GL_BGRA, GL.GL_UNSIGNED_BYTE, None)
        GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MIN_FILTER, GL.GL_LINEAR)
        GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MAG_FILTER, GL.GL_LINEAR)
        GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_WRAP_S, GL.GL_CLAMP_TO_EDGE)
        GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_WRAP_T, GL.GL_CLAMP_TO_EDGE)

        # Double-buffered PBOs let the DMA overlap with the next frame's CPU work.
        self._nbytes = w * h * 4
        self._pbos = list(GL.glGenBuffers(2))
        for pbo in self._pbos:
            GL.glBindBuffer(GL.GL_PIXEL_UNPACK_BUFFER, pbo)
            GL.glBufferData(GL.GL_PIXEL_UNPACK_BUFFER, self._nbytes, None, GL.GL_STREAM_DRAW)
        GL.glBindBuffer(GL.GL_PIXEL_UNPACK_BUFFER, 0)

        div = QUALITY_PRESETS[self._quality]["bloom_div"]
        self._fbos["bright"] = self._make_target(w // div, h // div)
        self._fbos["ping"] = self._make_target(w // div, h // div)
        self._fbos["pong"] = self._make_target(w // div, h // div)

    def _free(self):
        try:
            for key in ("bright", "ping", "pong"):
                t = self._fbos.pop(key, None)
                if t:
                    GL.glDeleteFramebuffers(1, [t["fbo"]])
                    GL.glDeleteTextures(1, [t["tex"]])
            if getattr(self, "_scene_tex", None):
                GL.glDeleteTextures(1, [self._scene_tex])
                self._scene_tex = None
            if self._pbos:
                GL.glDeleteBuffers(len(self._pbos), self._pbos)
                self._pbos = []
        except Exception:
            pass

    # -- frame ------------------------------------------------------------
    @property
    def size(self):
        return (self._w, self._h)

    def begin(self) -> pygame.Surface:
        return self._surface

    def _upload(self):
        """Stream the composed surface into the scene texture via a PBO."""
        pbo = self._pbos[self._pbo_index]
        self._pbo_index = (self._pbo_index + 1) % len(self._pbos)

        GL.glBindBuffer(GL.GL_PIXEL_UNPACK_BUFFER, pbo)
        # Orphan the previous store so the driver never stalls waiting on it.
        GL.glBufferData(GL.GL_PIXEL_UNPACK_BUFFER, self._nbytes, None, GL.GL_STREAM_DRAW)
        ptr = GL.glMapBufferRange(
            GL.GL_PIXEL_UNPACK_BUFFER, 0, self._nbytes,
            GL.GL_MAP_WRITE_BIT | GL.GL_MAP_INVALIDATE_BUFFER_BIT)
        if ptr:
            view = self._surface.get_view("0")
            src = ctypes.c_char.from_buffer(view)
            ctypes.memmove(ctypes.c_void_p(int(ptr)), ctypes.byref(src),
                           min(self._nbytes, view.length))
            del src, view
            GL.glUnmapBuffer(GL.GL_PIXEL_UNPACK_BUFFER)
            GL.glBindTexture(GL.GL_TEXTURE_2D, self._scene_tex)
            GL.glTexSubImage2D(GL.GL_TEXTURE_2D, 0, 0, 0, self._w, self._h,
                               GL.GL_BGRA, GL.GL_UNSIGNED_BYTE, ctypes.c_void_p(0))
        GL.glBindBuffer(GL.GL_PIXEL_UNPACK_BUFFER, 0)

    def _draw_fullscreen(self):
        GL.glDrawArrays(GL.GL_TRIANGLES, 0, 3)

    def _pass(self, target, prog, textures, uniforms):
        if target is None:
            GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, 0)
            GL.glViewport(0, 0, self._w, self._h)
        else:
            GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, target["fbo"])
            GL.glViewport(0, 0, target["w"], target["h"])
        GL.glUseProgram(prog)
        for unit, (uname, tex) in enumerate(textures):
            GL.glActiveTexture(GL.GL_TEXTURE0 + unit)
            GL.glBindTexture(GL.GL_TEXTURE_2D, tex)
            loc = GL.glGetUniformLocation(prog, uname)
            if loc != -1:
                GL.glUniform1i(loc, unit)
        for uname, val in uniforms.items():
            loc = GL.glGetUniformLocation(prog, uname)
            if loc == -1:
                continue
            if isinstance(val, tuple):
                if len(val) == 2:
                    GL.glUniform2f(loc, *val)
                elif len(val) == 3:
                    GL.glUniform3f(loc, *val)
            else:
                GL.glUniform1f(loc, float(val))
        self._draw_fullscreen()

    def present(self, dt: float = 0.0):
        q = QUALITY_PRESETS[self._quality]
        self._time += dt

        self._upload()
        GL.glBindVertexArray(self._vao)
        GL.glDisable(GL.GL_DEPTH_TEST)
        GL.glDisable(GL.GL_BLEND)

        bloom_tex = self._fbos["ping"]["tex"]
        if q["bloom"] > 0.0:
            bright = self._fbos["bright"]
            self._pass(bright, self._progs["bright"],
                       [("uScene", self._scene_tex)],
                       dict(uThreshold=0.82, uSoftKnee=0.4))

            ping, pong = self._fbos["ping"], self._fbos["pong"]
            texel = (1.0 / bright["w"], 1.0 / bright["h"])
            # Horizontal then vertical, twice, for a wide soft falloff.
            self._pass(pong, self._progs["blur"], [("uTex", bright["tex"])],
                       dict(uTexel=texel, uDir=(1.0, 0.0)))
            self._pass(ping, self._progs["blur"], [("uTex", pong["tex"])],
                       dict(uTexel=texel, uDir=(0.0, 1.0)))
            self._pass(pong, self._progs["blur"], [("uTex", ping["tex"])],
                       dict(uTexel=texel, uDir=(1.0, 0.0)))
            self._pass(ping, self._progs["blur"], [("uTex", pong["tex"])],
                       dict(uTexel=texel, uDir=(0.0, 1.0)))
            bloom_tex = ping["tex"]

        self._pass(None, self._progs["composite"],
                   [("uScene", self._scene_tex), ("uBloom", bloom_tex)],
                   dict(uResolution=(float(self._w), float(self._h)),
                        uTime=self._time,
                        uBloomStrength=q["bloom"],
                        uVignette=q["vignette"],
                        uScanline=q["scanline"],
                        uAberration=q["aberration"],
                        uGrain=q["grain"],
                        uSaturation=q["saturation"],
                        uContrast=q["contrast"]))
        GL.glBindVertexArray(0)
        pygame.display.flip()

    # -- lifecycle --------------------------------------------------------
    def resize(self, size, flags):
        gl_flags = flags | pygame.OPENGL | pygame.DOUBLEBUF
        self.screen = pygame.display.set_mode(size, gl_flags)
        self._free()
        self._alloc(size)

    def set_quality(self, preset: str):
        if preset not in QUALITY_PRESETS or preset == self._quality:
            return
        old_div = QUALITY_PRESETS[self._quality]["bloom_div"]
        self._quality = preset
        if QUALITY_PRESETS[preset]["bloom_div"] != old_div:
            for key in ("bright", "ping", "pong"):
                t = self._fbos.pop(key, None)
                if t:
                    GL.glDeleteFramebuffers(1, [t["fbo"]])
                    GL.glDeleteTextures(1, [t["tex"]])
            div = QUALITY_PRESETS[preset]["bloom_div"]
            self._fbos["bright"] = self._make_target(self._w // div, self._h // div)
            self._fbos["ping"] = self._make_target(self._w // div, self._h // div)
            self._fbos["pong"] = self._make_target(self._w // div, self._h // div)

    def shutdown(self):
        self._free()


# --------------------------------------------------------------------------
def create_renderer(size, flags, quality="high", prefer_gl=True, vsync=True):
    """Build the best available renderer. Never raises; falls back to SDL."""
    if prefer_gl and _HAVE_GL and not os.environ.get("HAVEN_FORCE_SDL"):
        try:
            r = GLRenderer(size, flags, quality=quality, vsync=vsync)
            return r, None
        except Exception as exc:
            # Tear down a half-made GL context before retrying in SDL mode.
            try:
                pygame.display.quit()
                pygame.display.init()
            except Exception:
                pass
            return SDLRenderer(size, flags), str(exc)
    reason = None
    if prefer_gl and not _HAVE_GL:
        reason = "PyOpenGL is not installed"
    return SDLRenderer(size, flags), reason
