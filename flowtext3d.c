/*
 * Flow Text 3D - a Windows screensaver.
 *
 * Extruded TrueType text (wglUseFontOutlinesW, like the classic 3D Text
 * saver) shaded with GLSL, over a flowing domain-warped gradient that runs
 * seamlessly across every monitor.
 *
 * Command line (standard .scr contract):
 *   /s            run full screen on all monitors
 *   /p <hwnd>     draw the little preview inside the Screen Saver Settings dialog
 *   /c[:hwnd]     show the settings dialog (also the default with no arguments)
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <GL/gl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "resource.h"

/* ------------------------------------------------------------------------ */
/* Settings                                                                 */
/* ------------------------------------------------------------------------ */

enum { ROT_NONE, ROT_SWAY, ROT_SPIN, ROT_SEESAW, ROT_WOBBLE, ROT_TUMBLE, ROT_FLIP, ROT_COUNT };
enum { MOT_CENTER, MOT_WANDER, MOT_BOUNCE, MOT_COUNT };
enum { SURF_CHROME, SURF_SOLID, SURF_RAINBOW, SURF_PEARL, SURF_COUNT };
enum { MON_EACH, MON_MIRROR, MON_SPAN, MON_PRIMARY, MON_COUNT };

typedef struct {
    WCHAR    text[128];
    int      clock;
    WCHAR    face[LF_FACESIZE];
    int      weight;
    int      italic;
    int      rotation, motion, surface;
    COLORREF textColor;
    int      size, depth, speed;      /* 0..100 */
    int      palette;
    COLORREF custom[4];
    int      flow, swirl, bright;     /* 0..100 */
    int      monitors, quality, aa, fps;
} Settings;

typedef struct { const WCHAR *name; unsigned base; unsigned c[4]; } Palette;

static const Palette PALETTES[] = {
    { L"Bloom",     0x060A22, { 0x2F6BFF, 0x86B6FF, 0xA35CFF, 0x1838B0 } },
    { L"Aurora",    0x02111B, { 0x00C9A7, 0x3D7BFF, 0x8E5CF7, 0x00F5D4 } },
    { L"Sunset",    0x150616, { 0xFF6B6B, 0xFFB46B, 0xC850C0, 0x4158D0 } },
    { L"Ocean",     0x020E1E, { 0x0077B6, 0x00B4D8, 0x90E0EF, 0x3A0CA3 } },
    { L"Neon",      0x07000F, { 0xFF2BD6, 0x7B2FFF, 0x00E5FF, 0x2B59FF } },
    { L"Lava",      0x100300, { 0xFF3D00, 0xFF9100, 0xB80000, 0xFFC400 } },
    { L"Forest",    0x03110A, { 0x2D6A4F, 0x52B788, 0xB7E4C7, 0x1B4332 } },
    { L"Candy",     0x3A2650, { 0xFF9AC1, 0xA0E7E5, 0xB4A7F5, 0xFFD6A5 } },
    { L"Monochrome",0x060606, { 0x303030, 0x7A7A7A, 0xD0D0D0, 0x484848 } },
    { L"Custom",    0,        { 0, 0, 0, 0 } },
};
#define PALETTE_COUNT ((int)(sizeof(PALETTES) / sizeof(PALETTES[0])))
#define PALETTE_CUSTOM (PALETTE_COUNT - 1)

static const WCHAR *ROT_NAMES[]  = { L"None", L"Gentle sway", L"Spin", L"See-saw", L"Wobble", L"Tumble", L"Flip" };
static const WCHAR *MOT_NAMES[]  = { L"Stay centered", L"Wander", L"Bounce" };
static const WCHAR *SURF_NAMES[] = { L"Chrome", L"Solid colour", L"Rainbow", L"Pearl" };
static const WCHAR *MON_NAMES[]  = { L"Each monitor its own text", L"Same text on every monitor",
                                     L"One text across all monitors", L"Text on main monitor only" };
static const WCHAR *QUAL_NAMES[] = { L"Best (full resolution)", L"Balanced", L"Battery saver" };
static const WCHAR *AA_NAMES[]   = { L"Off", L"Smooth (4x)", L"Smoothest (8x)" };
static const WCHAR *FPS_NAMES[]  = { L"Match display", L"30 fps (saves power)" };

#define REG_KEY L"Software\\FlowText3D"

static Settings g_cfg;
static HINSTANCE g_inst;

static void cfgDefaults(Settings *s)
{
    memset(s, 0, sizeof(*s));
    wcscpy(s->text, L"Windows 11");
    wcscpy(s->face, L"Segoe UI");
    s->weight = 900;
    s->rotation = ROT_SWAY;
    s->motion = MOT_WANDER;
    s->surface = SURF_CHROME;
    s->textColor = RGB(245, 245, 255);
    s->size = 55; s->depth = 40; s->speed = 40;
    s->palette = 0;
    s->custom[0] = RGB(255, 94, 98);  s->custom[1] = RGB(255, 179, 71);
    s->custom[2] = RGB(80, 120, 255); s->custom[3] = RGB(160, 80, 255);
    s->flow = 35; s->swirl = 55; s->bright = 70;
    s->monitors = MON_EACH; s->quality = 1; s->aa = 1; s->fps = 0;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int regInt(HKEY k, const WCHAR *name, int def, int lo, int hi)
{
    DWORD v, sz = sizeof(v), type;
    if (RegQueryValueExW(k, name, NULL, &type, (BYTE *)&v, &sz) != ERROR_SUCCESS || type != REG_DWORD)
        return def;
    return clampi((int)v, lo, hi);
}

static void regStr(HKEY k, const WCHAR *name, WCHAR *buf, DWORD cch)
{
    WCHAR tmp[256];
    DWORD sz = sizeof(tmp) - sizeof(WCHAR), type;
    if (RegQueryValueExW(k, name, NULL, &type, (BYTE *)tmp, &sz) != ERROR_SUCCESS || type != REG_SZ)
        return;
    tmp[sz / sizeof(WCHAR)] = 0;
    wcsncpy(buf, tmp, cch - 1);
    buf[cch - 1] = 0;
}

static void cfgLoad(Settings *s)
{
    HKEY k;
    cfgDefaults(s);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &k) != ERROR_SUCCESS)
        return;
    regStr(k, L"Text", s->text, 128);
    regStr(k, L"Font", s->face, LF_FACESIZE);
    s->clock     = regInt(k, L"Clock", s->clock, 0, 1);
    s->weight    = regInt(k, L"FontWeight", s->weight, 100, 1000);
    s->italic    = regInt(k, L"FontItalic", s->italic, 0, 1);
    s->rotation  = regInt(k, L"Rotation", s->rotation, 0, ROT_COUNT - 1);
    s->motion    = regInt(k, L"Motion", s->motion, 0, MOT_COUNT - 1);
    s->surface   = regInt(k, L"Surface", s->surface, 0, SURF_COUNT - 1);
    s->textColor = (COLORREF)regInt(k, L"TextColor", (int)s->textColor, 0, 0xFFFFFF);
    s->size      = regInt(k, L"Size", s->size, 0, 100);
    s->depth     = regInt(k, L"Depth", s->depth, 0, 100);
    s->speed     = regInt(k, L"Speed", s->speed, 0, 100);
    s->palette   = regInt(k, L"Palette", s->palette, 0, PALETTE_COUNT - 1);
    s->custom[0] = (COLORREF)regInt(k, L"Custom1", (int)s->custom[0], 0, 0xFFFFFF);
    s->custom[1] = (COLORREF)regInt(k, L"Custom2", (int)s->custom[1], 0, 0xFFFFFF);
    s->custom[2] = (COLORREF)regInt(k, L"Custom3", (int)s->custom[2], 0, 0xFFFFFF);
    s->custom[3] = (COLORREF)regInt(k, L"Custom4", (int)s->custom[3], 0, 0xFFFFFF);
    s->flow      = regInt(k, L"Flow", s->flow, 0, 100);
    s->swirl     = regInt(k, L"Swirl", s->swirl, 0, 100);
    s->bright    = regInt(k, L"Brightness", s->bright, 0, 100);
    s->monitors  = regInt(k, L"Monitors", s->monitors, 0, MON_COUNT - 1);
    s->quality   = regInt(k, L"Quality", s->quality, 0, 2);
    s->aa        = regInt(k, L"Antialias", s->aa, 0, 2);
    s->fps       = regInt(k, L"FrameRate", s->fps, 0, 1);
    RegCloseKey(k);
}

static void setInt(HKEY k, const WCHAR *name, int v)
{
    DWORD d = (DWORD)v;
    RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE *)&d, sizeof(d));
}

static void setStr(HKEY k, const WCHAR *name, const WCHAR *v)
{
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)v, (DWORD)((wcslen(v) + 1) * sizeof(WCHAR)));
}

static void cfgSave(const Settings *s)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    setStr(k, L"Text", s->text);
    setStr(k, L"Font", s->face);
    setInt(k, L"Clock", s->clock);
    setInt(k, L"FontWeight", s->weight);
    setInt(k, L"FontItalic", s->italic);
    setInt(k, L"Rotation", s->rotation);
    setInt(k, L"Motion", s->motion);
    setInt(k, L"Surface", s->surface);
    setInt(k, L"TextColor", (int)s->textColor);
    setInt(k, L"Size", s->size);
    setInt(k, L"Depth", s->depth);
    setInt(k, L"Speed", s->speed);
    setInt(k, L"Palette", s->palette);
    setInt(k, L"Custom1", (int)s->custom[0]);
    setInt(k, L"Custom2", (int)s->custom[1]);
    setInt(k, L"Custom3", (int)s->custom[2]);
    setInt(k, L"Custom4", (int)s->custom[3]);
    setInt(k, L"Flow", s->flow);
    setInt(k, L"Swirl", s->swirl);
    setInt(k, L"Brightness", s->bright);
    setInt(k, L"Monitors", s->monitors);
    setInt(k, L"Quality", s->quality);
    setInt(k, L"Antialias", s->aa);
    setInt(k, L"FrameRate", s->fps);
    RegCloseKey(k);
}

/* ------------------------------------------------------------------------ */
/* OpenGL extension loading                                                 */
/* ------------------------------------------------------------------------ */

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif
#define GL_FRAGMENT_SHADER_      0x8B30
#define GL_VERTEX_SHADER_        0x8B31
#define GL_COMPILE_STATUS_       0x8B81
#define GL_LINK_STATUS_          0x8B82
#define GL_FRAMEBUFFER_          0x8D40
#define GL_READ_FRAMEBUFFER_     0x8CA8
#define GL_DRAW_FRAMEBUFFER_     0x8CA9
#define GL_RENDERBUFFER_         0x8D41
#define GL_COLOR_ATTACHMENT0_    0x8CE0
#define GL_DEPTH_ATTACHMENT_     0x8D00
#define GL_FRAMEBUFFER_COMPLETE_ 0x8CD5
#define GL_RGBA16F_              0x881A
#define GL_RGBA8_                0x8058
#define GL_DEPTH_COMPONENT24_    0x81A6
#define GL_CLAMP_TO_EDGE_        0x812F
#define GL_MAX_SAMPLES_          0x8D57

#define GL_CORE_FUNCS(X) \
    X(GLuint, glCreateShader, (GLenum)) \
    X(void,   glShaderSource, (GLuint, GLsizei, const char *const *, const GLint *)) \
    X(void,   glCompileShader, (GLuint)) \
    X(void,   glGetShaderiv, (GLuint, GLenum, GLint *)) \
    X(void,   glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, char *)) \
    X(void,   glDeleteShader, (GLuint)) \
    X(GLuint, glCreateProgram, (void)) \
    X(void,   glAttachShader, (GLuint, GLuint)) \
    X(void,   glLinkProgram, (GLuint)) \
    X(void,   glGetProgramiv, (GLuint, GLenum, GLint *)) \
    X(void,   glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, char *)) \
    X(void,   glUseProgram, (GLuint)) \
    X(void,   glDeleteProgram, (GLuint)) \
    X(GLint,  glGetUniformLocation, (GLuint, const char *)) \
    X(void,   glUniform1i, (GLint, GLint)) \
    X(void,   glUniform1f, (GLint, GLfloat)) \
    X(void,   glUniform2f, (GLint, GLfloat, GLfloat)) \
    X(void,   glUniform3f, (GLint, GLfloat, GLfloat, GLfloat)) \
    X(void,   glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void,   glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *))

#define GL_FBO_FUNCS(X) \
    X(void,   glGenFramebuffers, (GLsizei, GLuint *)) \
    X(void,   glDeleteFramebuffers, (GLsizei, const GLuint *)) \
    X(void,   glBindFramebuffer, (GLenum, GLuint)) \
    X(void,   glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum)) \
    X(void,   glGenRenderbuffers, (GLsizei, GLuint *)) \
    X(void,   glDeleteRenderbuffers, (GLsizei, const GLuint *)) \
    X(void,   glBindRenderbuffer, (GLenum, GLuint)) \
    X(void,   glRenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei)) \
    X(void,   glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
    X(void,   glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))

#define GL_DECLARE(ret, name, args) typedef ret (APIENTRY *PFN_##name) args; static PFN_##name p_##name;
GL_CORE_FUNCS(GL_DECLARE)
GL_FBO_FUNCS(GL_DECLARE)
typedef BOOL (APIENTRY *PFN_wglSwapIntervalEXT)(int);
static PFN_wglSwapIntervalEXT p_wglSwapIntervalEXT;

static int g_glLoaded, g_hasShaders, g_hasFbo;

static PROC glProc(const char *name)
{
    PROC p = wglGetProcAddress(name);
    if (p == NULL || p == (PROC)1 || p == (PROC)2 || p == (PROC)3 || p == (PROC)-1) {
        HMODULE m = GetModuleHandleW(L"opengl32.dll");
        p = m ? GetProcAddress(m, name) : NULL;
    }
    return p;
}

static void glLoad(void)
{
    if (g_glLoaded) return;
    g_glLoaded = 1;
    int ok = 1;
#define LOAD_GL_FN(ret, name, args) p_##name = (PFN_##name)glProc(#name); if (!p_##name) ok = 0;
    GL_CORE_FUNCS(LOAD_GL_FN)
    g_hasShaders = ok;
    ok = 1;
    GL_FBO_FUNCS(LOAD_GL_FN)
    g_hasFbo = ok && g_hasShaders;
#undef LOAD_GL_FN
    p_wglSwapIntervalEXT = (PFN_wglSwapIntervalEXT)glProc("wglSwapIntervalEXT");
}

/* ------------------------------------------------------------------------ */
/* Shaders                                                                  */
/* ------------------------------------------------------------------------ */

#define GLSL_PALETTE \
    "uniform vec3 uBase, uC0, uC1, uC2, uC3;\n" \
    "vec3 pal(float t) {\n" \
    "  t = fract(t) * 4.0;\n" \
    "  float f = fract(t); f = f * f * (3.0 - 2.0 * f);\n" \
    "  if (t < 1.0) return mix(uC0, uC1, f);\n" \
    "  if (t < 2.0) return mix(uC1, uC2, f);\n" \
    "  if (t < 3.0) return mix(uC2, uC3, f);\n" \
    "  return mix(uC3, uC0, f);\n" \
    "}\n"

static const char *VS_QUAD =
    "#version 120\n"
    "void main() { gl_Position = gl_Vertex; }\n";

/* Flowing background. Coordinates are window pixels, so the pattern is one
   continuous field across every monitor. */
static const char *FS_BACKGROUND =
    "#version 120\n"
    "uniform vec2 uFragToWin;\n"
    "uniform vec2 uOffset;\n"
    "uniform float uRef, uTime, uSwirl, uBright, uDither;\n"
    GLSL_PALETTE
    "float hash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n"
    "float noise(vec2 p) {\n"
    "  vec2 i = floor(p), f = fract(p);\n"
    "  vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
    "  float a = hash(i), b = hash(i + vec2(1.0, 0.0));\n"
    "  float c = hash(i + vec2(0.0, 1.0)), d = hash(i + vec2(1.0, 1.0));\n"
    "  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);\n"
    "}\n"
    "float fbm(vec2 p) {\n"
    "  float s = 0.0, a = 0.5;\n"
    "  mat2 r = mat2(0.8, 0.6, -0.6, 0.8);\n"
    "  for (int i = 0; i < 4; i++) { s += a * noise(p); p = r * p * 2.03 + 17.1; a *= 0.5; }\n"
    "  return s / 0.9375;\n"
    "}\n"
    "void main() {\n"
    "  vec2 g = gl_FragCoord.xy * uFragToWin + uOffset;\n"
    "  vec2 p = g / uRef;\n"
    "  float t = uTime;\n"
    "  vec2 w1 = vec2(fbm(p * 0.7 + vec2(0.0, t * 0.06)), fbm(p * 0.7 + vec2(5.2, 1.3) - vec2(t * 0.05, 0.0)));\n"
    "  vec2 q = p + uSwirl * 1.4 * (w1 - 0.5);\n"
    "  vec2 w2 = vec2(fbm(q * 1.3 + vec2(1.7, 9.2) + t * 0.08), fbm(q * 1.3 + vec2(8.3, 2.8) - t * 0.07));\n"
    "  q += uSwirl * 0.6 * (w2 - 0.5);\n"
    "  float f = fbm(q * 0.9 - vec2(t * 0.03, t * 0.02));\n"
    "  float k = f * 1.6 + 0.18 * sin(q.x * 0.9 + t * 0.11) + 0.18 * sin(q.y * 1.1 - t * 0.09) + t * 0.015;\n"
    "  vec3 col = pal(k);\n"
    "  float shade = smoothstep(0.2, 0.8, f);\n"
    "  col = mix(uBase, col, 0.30 + 0.70 * shade);\n"
    "  float rib = 0.5 + 0.5 * sin((q.x * 0.8 + q.y * 1.3) * 5.0 + f * 9.0 - t * 0.5);\n"
    "  col += pow(rib, 14.0) * 0.10 * (col + 0.15);\n"
    "  col *= uBright;\n"
    "  col += uDither * (hash(gl_FragCoord.xy + fract(t * 7.0)) - 0.5) / 255.0;\n"
    "  gl_FragColor = vec4(col, 1.0);\n"
    "}\n";

/* Upscales the low-res float background into a monitor, adds a soft
   per-monitor vignette and dither (so 8-bit output doesn't band). */
static const char *FS_COMPOSITE =
    "#version 120\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uWin, uOffset;\n"
    "uniform vec4 uMon;\n"
    "uniform float uTime;\n"
    "float hash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n"
    "void main() {\n"
    "  vec2 g = gl_FragCoord.xy + uOffset;\n"
    "  vec3 c = texture2D(uTex, g / uWin).rgb;\n"
    "  vec2 l = (g - uMon.xy) / uMon.zw - 0.5;\n"
    "  c *= 1.0 - 0.55 * dot(l, l);\n"
    "  c += (hash(g + fract(uTime * 7.0)) - 0.5) / 255.0;\n"
    "  gl_FragColor = vec4(c, 1.0);\n"
    "}\n";

static const char *VS_TEXT =
    "#version 120\n"
    "uniform mat4 uTextInv;\n"
    "varying vec3 vEye, vTxt;\n"
    "void main() {\n"
    "  vec4 e = gl_ModelViewMatrix * gl_Vertex;\n"
    "  vEye = e.xyz;\n"
    "  vTxt = (uTextInv * e).xyz;\n"
    "  gl_Position = gl_ProjectionMatrix * e;\n"
    "}\n";

/* Normals come from screen-space derivatives, so shading never depends on
   what the driver's wglUseFontOutlines put in the display list. */
static const char *FS_TEXT =
    "#version 120\n"
    "uniform int uSurface;\n"
    "uniform vec3 uColor;\n"
    "uniform vec2 uHalf;\n"
    "uniform float uTime;\n"
    "varying vec3 vEye, vTxt;\n"
    GLSL_PALETTE
    "vec3 hue(float h) { return clamp(abs(mod(h * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0); }\n"
    "vec3 env(vec3 r) {\n"
    "  float a = atan(r.y, r.x) * 0.15915 + r.z * 0.12 + uTime * 0.02;\n"
    "  vec3 c = pal(a);\n"
    "  float sky = smoothstep(-0.35, 0.25, r.y);\n"
    "  c = mix(uBase + c * 0.55 + 0.04, c * 1.15 + 0.10, sky);\n"
    "  c += pow(max(r.y, 0.0), 10.0) * 0.9;\n"
    "  c += smoothstep(0.03, 0.0, abs(r.y + 0.05)) * 0.35;\n"
    "  return c;\n"
    "}\n"
    "void main() {\n"
    "  vec3 N = normalize(cross(dFdx(vEye), dFdy(vEye)));\n"
    "  vec3 V = normalize(-vEye);\n"
    "  if (dot(N, V) < 0.0) N = -N;\n"
    "  vec3 No = cross(dFdx(vTxt), dFdy(vTxt));\n"
    "  float side = 1.0 - smoothstep(0.75, 0.97, abs(No.z) / max(length(No), 1e-8));\n"
    "  vec2 tp = vTxt.xy / max(uHalf, vec2(0.001));\n"
    "  vec3 L1 = normalize(vec3(-0.45, 0.65, 0.6));\n"
    "  vec3 L2 = normalize(vec3(0.6, -0.35, 0.7));\n"
    "  float ndv = max(dot(N, V), 0.0);\n"
    "  float fres = pow(1.0 - ndv, 4.0);\n"
    "  float spec = pow(max(dot(N, normalize(L1 + V)), 0.0), 60.0);\n"
    "  float diff = max(dot(N, L1), 0.0) * 0.7 + max(dot(N, L2), 0.0) * 0.3 + ndv * 0.35 + 0.18;\n"
    "  vec3 col;\n"
    "  if (uSurface == 0) {\n"
    "    vec3 Np = normalize(N + (1.0 - side) * vec3(tp.x * 0.18, tp.y * 0.35, 0.0));\n"
    "    col = env(reflect(-V, Np));\n"
    "    col *= 1.0 - side * 0.45;\n"
    "    col += spec * 0.8 + fres * 0.25;\n"
    "  } else if (uSurface == 3) {\n"
    "    vec3 film = pal(ndv * 1.3 + tp.x * 0.15 + uTime * 0.03);\n"
    "    col = mix(vec3(0.92), film, 0.55) * (0.55 + 0.5 * diff);\n"
    "    col *= 1.0 - side * 0.35;\n"
    "    col += spec * 0.9 + fres * film * 0.6;\n"
    "  } else {\n"
    "    vec3 base = (uSurface == 1) ? uColor : hue(fract(tp.x * 0.35 + tp.y * 0.1 + uTime * 0.08)) * 0.9 + 0.05;\n"
    "    base *= mix(1.0, 0.62, side);\n"
    "    col = base * diff + spec * 0.6 + fres * pal(uTime * 0.02 + tp.x * 0.1) * 0.5;\n"
    "  }\n"
    "  gl_FragColor = vec4(col, 1.0);\n"
    "}\n";

static GLuint compileShader(GLenum type, const char *src)
{
    GLuint s = p_glCreateShader(type);
    GLint ok = 0;
    p_glShaderSource(s, 1, &src, NULL);
    p_glCompileShader(s);
    p_glGetShaderiv(s, GL_COMPILE_STATUS_, &ok);
    if (!ok) {
        char log[2048];
        p_glGetShaderInfoLog(s, sizeof(log), NULL, log);
        OutputDebugStringA("FlowText3D shader error:\n");
        OutputDebugStringA(log);
        p_glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint linkProgram(const char *vs, const char *fs)
{
    GLuint v = compileShader(GL_VERTEX_SHADER_, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER_, fs);
    GLuint p;
    GLint ok = 0;
    if (!v || !f) {
        if (v) p_glDeleteShader(v);
        if (f) p_glDeleteShader(f);
        return 0;
    }
    p = p_glCreateProgram();
    p_glAttachShader(p, v);
    p_glAttachShader(p, f);
    p_glLinkProgram(p);
    p_glDeleteShader(v);
    p_glDeleteShader(f);
    p_glGetProgramiv(p, GL_LINK_STATUS_, &ok);
    if (!ok) {
        char log[2048];
        p_glGetProgramInfoLog(p, sizeof(log), NULL, log);
        OutputDebugStringA("FlowText3D link error:\n");
        OutputDebugStringA(log);
        p_glDeleteProgram(p);
        return 0;
    }
    return p;
}

/* ------------------------------------------------------------------------ */
/* Small matrix helpers (column-major, like OpenGL)                         */
/* ------------------------------------------------------------------------ */

typedef struct { float m[16]; } M4;

static M4 m4id(void) { M4 r = {{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}}; return r; }

static M4 m4mul(M4 a, M4 b)
{
    M4 r;
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}

static M4 m4tr(float x, float y, float z) { M4 r = m4id(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }
static M4 m4sc(float s) { M4 r = m4id(); r.m[0] = r.m[5] = r.m[10] = s; return r; }

static M4 m4rot(int axis, float deg)
{
    M4 r = m4id();
    float a = deg * 3.14159265f / 180.0f, c = cosf(a), s = sinf(a);
    if (axis == 0)      { r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; }
    else if (axis == 1) { r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; }
    else                { r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; }
    return r;
}

static M4 m4inv(M4 in)
{
    const float *m = in.m;
    M4 r;
    float *inv = r.m;
    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
    inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
    inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
    inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (fabsf(det) < 1e-12f) return m4id();
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) inv[i] *= det;
    return r;
}

/* ------------------------------------------------------------------------ */
/* View: one GL window (full-screen saver, settings preview, or dialog)     */
/* ------------------------------------------------------------------------ */

enum { VIEW_SAVER, VIEW_PREVIEW, VIEW_DIALOG };
#define MAX_MONS 16
#define CAM_FOV_DEG 28.0f

typedef struct { int x, y, w, h; int primary; } Mon;   /* window coords, top-left origin */
typedef struct { float px, py, vx, vy, phase; } TextState;

typedef struct {
    UINT  key;
    GLuint *lists;               /* 65536 lazily built display lists */
    GLYPHMETRICSFLOAT *gm;
    HFONT font;
    float extrusion;
} GlyphCache;

typedef struct View {
    int   kind;
    HWND  hwnd, parent;
    HDC   hdc;
    HGLRC rc;
    int   alive, glOk;
    int   w, h;
    Mon   mons[MAX_MONS];
    int   nmons;
    float refPx;                 /* background pattern scale */
    POINT cursor0;
    ULONGLONG startTick;

    GLuint progBg, progComp, progText;
    GLint  bgFragToWin, bgOffset, bgRef, bgTime, bgSwirl, bgBright, bgDither, bgPal[5];
    GLint  cpTex, cpWin, cpOffset, cpMon, cpTime;
    GLint  txInv, txSurface, txColor, txHalf, txTime, txPal[5];

    GLuint bgFbo, bgTex;
    int    bgW, bgH, bgQuality;
    GLuint msFbo, msColor, msDepth;
    int    msW, msH, msSamples, msWanted, msFailed;

    GlyphCache glyphs;
    TextState  states[MAX_MONS];

    double lastT, flowT, animT, colorT;
    LARGE_INTEGER qpf;
} View;

static const WCHAR VIEW_CLASS[] = L"FlowText3DView";

static double nowSec(View *v)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)v->qpf.QuadPart;
}

static void hexToRgb(unsigned hex, float *out)
{
    out[0] = ((hex >> 16) & 255) / 255.0f;
    out[1] = ((hex >> 8) & 255) / 255.0f;
    out[2] = (hex & 255) / 255.0f;
}

static void colorrefToRgb(COLORREF c, float *out)
{
    out[0] = GetRValue(c) / 255.0f;
    out[1] = GetGValue(c) / 255.0f;
    out[2] = GetBValue(c) / 255.0f;
}

/* out[0] = base, out[1..4] = palette colours */
static void currentPalette(float out[5][3])
{
    const Settings *s = &g_cfg;
    if (s->palette == PALETTE_CUSTOM) {
        float lum = 10.0f;
        for (int i = 0; i < 4; i++) {
            colorrefToRgb(s->custom[i], out[i + 1]);
            float l = out[i + 1][0] * 0.3f + out[i + 1][1] * 0.59f + out[i + 1][2] * 0.11f;
            if (l < lum) {
                lum = l;
                for (int k = 0; k < 3; k++) out[0][k] = out[i + 1][k] * 0.12f + 0.01f;
            }
        }
    } else {
        const Palette *p = &PALETTES[s->palette];
        hexToRgb(p->base, out[0]);
        for (int i = 0; i < 4; i++) hexToRgb(p->c[i], out[i + 1]);
    }
}

static void setPaletteUniforms(const GLint loc[5], float pal[5][3])
{
    for (int i = 0; i < 5; i++)
        if (loc[i] >= 0) p_glUniform3f(loc[i], pal[i][0], pal[i][1], pal[i][2]);
}

static void getPaletteLocs(GLuint prog, GLint loc[5])
{
    static const char *names[5] = { "uBase", "uC0", "uC1", "uC2", "uC3" };
    for (int i = 0; i < 5; i++) loc[i] = p_glGetUniformLocation(prog, names[i]);
}

/* ---- glyphs ------------------------------------------------------------ */

static float depthToExtrusion(int depth) { return 0.02f + depth / 100.0f * 0.58f; }

static void glyphsFree(View *v)
{
    GlyphCache *g = &v->glyphs;
    if (g->lists) {
        for (int i = 0; i < 65536; i++)
            if (g->lists[i]) glDeleteLists(g->lists[i], 1);
        free(g->lists);
        g->lists = NULL;
    }
    free(g->gm);
    g->gm = NULL;
    if (g->font) {
        SelectObject(v->hdc, GetStockObject(SYSTEM_FONT));
        DeleteObject(g->font);
        g->font = NULL;
    }
    g->key = 0;
}

static UINT fontKey(const Settings *s)
{
    UINT h = 2166136261u;
    for (const WCHAR *p = s->face; *p; p++) h = (h ^ *p) * 16777619u;
    h = (h ^ (UINT)s->weight) * 16777619u;
    h = (h ^ (UINT)s->italic) * 16777619u;
    h = (h ^ (UINT)s->depth) * 16777619u;
    return h | 1;
}

static void glyphsEnsureFont(View *v)
{
    UINT key = fontKey(&g_cfg);
    GlyphCache *g = &v->glyphs;
    if (g->key == key) return;
    glyphsFree(v);
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -256;
    lf.lfWeight = g_cfg.weight;
    lf.lfItalic = (BYTE)g_cfg.italic;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_ONLY_PRECIS;
    lf.lfQuality = ANTIALIASED_QUALITY;
    wcsncpy(lf.lfFaceName, g_cfg.face, LF_FACESIZE - 1);
    g->font = CreateFontIndirectW(&lf);
    SelectObject(v->hdc, g->font);
    g->lists = (GLuint *)calloc(65536, sizeof(GLuint));
    g->gm = (GLYPHMETRICSFLOAT *)calloc(65536, sizeof(GLYPHMETRICSFLOAT));
    g->extrusion = depthToExtrusion(g_cfg.depth);
    g->key = key;
}

static GLuint glyphGet(View *v, WCHAR ch)
{
    GlyphCache *g = &v->glyphs;
    if (!g->lists[ch]) {
        GLuint list = glGenLists(1);
        if (!list) return 0;
        if (!wglUseFontOutlinesW(v->hdc, ch, 1, list, 0.0f, g->extrusion, WGL_FONT_POLYGONS, &g->gm[ch])) {
            /* glyph missing: keep an empty list that still advances a little */
            glNewList(list, GL_COMPILE);
            glTranslatef(0.3f, 0.0f, 0.0f);
            glEndList();
            memset(&g->gm[ch], 0, sizeof(g->gm[ch]));
            g->gm[ch].gmfCellIncX = 0.3f;
        }
        g->lists[ch] = list;
    }
    return g->lists[ch];
}

typedef struct { float minx, maxx, miny, maxy; } Box;

static Box measureText(View *v, const WCHAR *s, int n)
{
    Box b = { 1e9f, -1e9f, 1e9f, -1e9f };
    float pen = 0;
    for (int i = 0; i < n; i++) {
        glyphGet(v, s[i]);
        const GLYPHMETRICSFLOAT *m = &v->glyphs.gm[s[i]];
        if (m->gmfBlackBoxX > 0.001f && s[i] != L' ') {
            float l = pen + m->gmfptGlyphOrigin.x, t = m->gmfptGlyphOrigin.y;
            if (l < b.minx) b.minx = l;
            if (l + m->gmfBlackBoxX > b.maxx) b.maxx = l + m->gmfBlackBoxX;
            if (t > b.maxy) b.maxy = t;
            if (t - m->gmfBlackBoxY < b.miny) b.miny = t - m->gmfBlackBoxY;
        }
        pen += m->gmfCellIncX;
    }
    if (b.minx > b.maxx) { b.minx = 0; b.maxx = pen > 0 ? pen : 0.5f; b.miny = 0; b.maxy = 0.7f; }
    return b;
}

/* ---- framebuffers ------------------------------------------------------ */

static void fboFreeBg(View *v)
{
    if (v->bgFbo) p_glDeleteFramebuffers(1, &v->bgFbo);
    if (v->bgTex) glDeleteTextures(1, &v->bgTex);
    v->bgFbo = v->bgTex = 0;
    v->bgW = v->bgH = 0;
}

static void fboFreeMs(View *v)
{
    if (v->msFbo) p_glDeleteFramebuffers(1, &v->msFbo);
    if (v->msColor) p_glDeleteRenderbuffers(1, &v->msColor);
    if (v->msDepth) p_glDeleteRenderbuffers(1, &v->msDepth);
    v->msFbo = v->msColor = v->msDepth = 0;
    v->msW = v->msH = v->msSamples = 0;
}

static void fboEnsureBg(View *v)
{
    static const int div[3] = { 1, 2, 4 };
    int d = div[clampi(g_cfg.quality, 0, 2)];
    if (v->kind != VIEW_SAVER && d > 2) d = 2;
    int w = (v->w + d - 1) / d, h = (v->h + d - 1) / d;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (v->bgFbo && v->bgW == w && v->bgH == h) return;
    fboFreeBg(v);
    glGenTextures(1, &v->bgTex);
    glBindTexture(GL_TEXTURE_2D, v->bgTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F_, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
    p_glGenFramebuffers(1, &v->bgFbo);
    p_glBindFramebuffer(GL_FRAMEBUFFER_, v->bgFbo);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D, v->bgTex, 0);
    if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER_) != GL_FRAMEBUFFER_COMPLETE_) {
        /* 16-bit float not renderable here: fall back to 8-bit */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8_, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        p_glFramebufferTexture2D(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D, v->bgTex, 0);
        if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER_) != GL_FRAMEBUFFER_COMPLETE_) {
            p_glBindFramebuffer(GL_FRAMEBUFFER_, 0);
            fboFreeBg(v);
            return;
        }
    }
    p_glBindFramebuffer(GL_FRAMEBUFFER_, 0);
    v->bgW = w;
    v->bgH = h;
}

static void fboEnsureMs(View *v)
{
    static const int samples[3] = { 0, 4, 8 };
    int want = samples[clampi(g_cfg.aa, 0, 2)];
    int mw = 1, mh = 1;
    for (int i = 0; i < v->nmons; i++) {
        if (v->mons[i].w > mw) mw = v->mons[i].w;
        if (v->mons[i].h > mh) mh = v->mons[i].h;
    }
    if (want != v->msWanted) { v->msWanted = want; v->msFailed = 0; fboFreeMs(v); }
    if (!want || v->msFailed) { fboFreeMs(v); return; }
    if (v->msFbo && v->msW == mw && v->msH == mh) return;
    fboFreeMs(v);
    GLint maxS = 0;
    glGetIntegerv(GL_MAX_SAMPLES_, &maxS);
    if (maxS < 2) { v->msFailed = 1; return; }
    if (want > maxS) want = maxS;
    while (glGetError() != GL_NO_ERROR) {}
    p_glGenRenderbuffers(1, &v->msColor);
    p_glBindRenderbuffer(GL_RENDERBUFFER_, v->msColor);
    p_glRenderbufferStorageMultisample(GL_RENDERBUFFER_, want, GL_RGBA8_, mw, mh);
    p_glGenRenderbuffers(1, &v->msDepth);
    p_glBindRenderbuffer(GL_RENDERBUFFER_, v->msDepth);
    p_glRenderbufferStorageMultisample(GL_RENDERBUFFER_, want, GL_DEPTH_COMPONENT24_, mw, mh);
    p_glGenFramebuffers(1, &v->msFbo);
    p_glBindFramebuffer(GL_FRAMEBUFFER_, v->msFbo);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_RENDERBUFFER_, v->msColor);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER_, GL_DEPTH_ATTACHMENT_, GL_RENDERBUFFER_, v->msDepth);
    int ok = p_glCheckFramebufferStatus(GL_FRAMEBUFFER_) == GL_FRAMEBUFFER_COMPLETE_ && glGetError() == GL_NO_ERROR;
    p_glBindFramebuffer(GL_FRAMEBUFFER_, 0);
    if (!ok) { fboFreeMs(v); v->msFailed = 1; return; }
    v->msW = mw; v->msH = mh; v->msSamples = want;
}

/* ---- layout ------------------------------------------------------------ */

typedef struct { View *v; RECT virt; } EnumCtx;

static BOOL CALLBACK monEnum(HMONITOR hm, HDC dc, LPRECT rc, LPARAM lp)
{
    (void)dc; (void)rc;
    EnumCtx *ctx = (EnumCtx *)lp;
    View *v = ctx->v;
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (v->nmons >= MAX_MONS || !GetMonitorInfoW(hm, &mi)) return TRUE;
    Mon *m = &v->mons[v->nmons++];
    m->x = mi.rcMonitor.left - ctx->virt.left;
    m->y = mi.rcMonitor.top - ctx->virt.top;
    m->w = mi.rcMonitor.right - mi.rcMonitor.left;
    m->h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    m->primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    return TRUE;
}

static int monCmp(const void *a, const void *b)
{
    const Mon *x = (const Mon *)a, *y = (const Mon *)b;
    return x->x != y->x ? x->x - y->x : x->y - y->y;
}

static void viewLayout(View *v)
{
    RECT r;
    GetClientRect(v->hwnd, &r);
    v->w = r.right > 1 ? r.right : 1;
    v->h = r.bottom > 1 ? r.bottom : 1;
    v->nmons = 0;
    WCHAR fake[512];
    if (v->kind == VIEW_SAVER && GetEnvironmentVariableW(L"FLOWTEXT3D_MONITORS", fake, 512) > 0) {
        /* Testing aid: "x,y,w,h;x,y,w,h" (window pixels, first one primary)
           stands in for EnumDisplayMonitors, e.g. under Wine or a VM. */
        const WCHAR *p = fake;
        while (*p && v->nmons < MAX_MONS) {
            Mon *m = &v->mons[v->nmons];
            if (swscanf(p, L"%d,%d,%d,%d", &m->x, &m->y, &m->w, &m->h) != 4) break;
            m->primary = v->nmons == 0;
            if (m->w > 0 && m->h > 0) v->nmons++;
            while (*p && *p != L';') p++;
            if (*p) p++;
        }
    } else if (v->kind == VIEW_SAVER) {
        EnumCtx ctx;
        ctx.v = v;
        ctx.virt.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        ctx.virt.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        EnumDisplayMonitors(NULL, NULL, monEnum, (LPARAM)&ctx);
        qsort(v->mons, (size_t)v->nmons, sizeof(Mon), monCmp);
    }
    if (v->nmons == 0) {
        v->mons[0].x = 0; v->mons[0].y = 0; v->mons[0].w = v->w; v->mons[0].h = v->h; v->mons[0].primary = 1;
        v->nmons = 1;
    }
    v->refPx = 1;
    for (int i = 0; i < v->nmons; i++)
        if (v->mons[i].h > v->refPx) v->refPx = (float)v->mons[i].h;
}

/* ---- per-frame text motion -------------------------------------------- */

static float perspR(float r, float D) { return r < D * 0.9f ? r * D / sqrtf(D * D - r * r) : r * 2.3f; }
static float magnify(float z, float D) { return z < D * 0.9f ? D / (D - z) : 10.0f; }

/* Projected half-extents (and nearest z) of the rotating text, world units. */
static void textExtents(int rot, float hw, float hh, float hd, float D, float *ex, float *ey, float *zmax)
{
    float r;
    switch (rot) {
    case ROT_SPIN:
        r = sqrtf(hw * hw + hd * hd);
        *ex = perspR(r, D); *ey = hh * magnify(r, D); *zmax = r; break;
    case ROT_FLIP:
        r = sqrtf(hh * hh + hd * hd);
        *ey = perspR(r, D); *ex = hw * magnify(r, D); *zmax = r; break;
    case ROT_TUMBLE:
        r = sqrtf(hw * hw + hh * hh + hd * hd);
        *ex = *ey = perspR(r, D); *zmax = r; break;
    case ROT_SEESAW:
        *zmax = hd + hw * 0.18f;
        *ex = (hw * 0.94f + hh * 0.34f) * magnify(*zmax, D);
        *ey = (hw * 0.34f + hh * 0.94f) * magnify(*zmax, D); break;
    case ROT_WOBBLE:
        *zmax = hd + hw * 0.57f + hh * 0.42f;
        *ex = hw * magnify(hw * 0.57f + hd, D);
        *ey = hh * magnify(*zmax, D) + hd * 0.42f; break;
    case ROT_SWAY:
        *zmax = hd + hw * 0.31f + hh * 0.14f;
        *ex = hw * magnify(hw * 0.31f + hd, D);
        *ey = hh * magnify(*zmax, D) + hd * 0.14f; break;
    default:
        *zmax = hd; *ex = hw * magnify(hd, D); *ey = hh * magnify(hd, D); break;
    }
}

static void rotationAngles(int rot, float t, float *rx, float *ry, float *rz)
{
    *rx = *ry = *rz = 0;
    switch (rot) {
    case ROT_SWAY:   *ry = 18.0f * sinf(0.6f * t); *rx = 8.0f * sinf(0.45f * t + 1.0f); break;
    case ROT_SPIN:   *ry = fmodf(60.0f * t, 360.0f); break;
    case ROT_SEESAW: *rz = 18.0f * sinf(t); *ry = 10.0f * sinf(0.5f * t); break;
    case ROT_WOBBLE: *ry = 35.0f * sinf(0.9f * t); *rx = 25.0f * sinf(1.3f * t + 0.7f); break;
    case ROT_TUMBLE: *rx = fmodf(40.0f * t, 360.0f); *ry = fmodf(55.0f * t, 360.0f); *rz = fmodf(25.0f * t, 360.0f); break;
    case ROT_FLIP:   *rx = fmodf(70.0f * t, 360.0f); break;
    default: break;
    }
}

static void statesInit(View *v)
{
    srand((unsigned)GetTickCount());
    for (int i = 0; i < MAX_MONS; i++) {
        TextState *s = &v->states[i];
        float a = (float)rand() / RAND_MAX * 6.2832f;
        s->vx = cosf(a); s->vy = sinf(a);
        if (fabsf(s->vx) < 0.3f) s->vx = s->vx < 0 ? -0.3f : 0.3f;
        if (fabsf(s->vy) < 0.3f) s->vy = s->vy < 0 ? -0.3f : 0.3f;
        s->px = ((float)rand() / RAND_MAX - 0.5f);
        s->py = ((float)rand() / RAND_MAX - 0.5f);
        s->phase = i * 2.17f + (float)rand() / RAND_MAX * 3.0f;
    }
}

static void statesUpdate(View *v, float dt)
{
    float sp = 0.1f + g_cfg.speed / 100.0f * 1.9f;
    for (int i = 0; i < MAX_MONS; i++) {
        TextState *s = &v->states[i];
        if (g_cfg.motion == MOT_BOUNCE) {
            s->px += s->vx * dt * 0.16f * sp;
            s->py += s->vy * dt * 0.16f * sp;
            if (s->px > 1) { s->px = 1; s->vx = -fabsf(s->vx); }
            if (s->px < -1) { s->px = -1; s->vx = fabsf(s->vx); }
            if (s->py > 1) { s->py = 1; s->vy = -fabsf(s->vy); }
            if (s->py < -1) { s->py = -1; s->vy = fabsf(s->vy); }
        }
    }
}

static void statePos(View *v, int si, float *nx, float *ny)
{
    TextState *s = &v->states[si];
    float t = (float)v->animT * 0.5f + s->phase * 10.0f;
    switch (g_cfg.motion) {
    case MOT_WANDER:
        *nx = 0.8f * sinf(t * 0.23f + s->phase) + 0.2f * sinf(t * 0.11f + 2.0f * s->phase);
        *ny = 0.7f * sinf(t * 0.31f + 1.3f * s->phase) + 0.3f * sinf(t * 0.17f + 0.5f * s->phase);
        break;
    case MOT_BOUNCE: *nx = s->px; *ny = s->py; break;
    default: *nx = *ny = 0; break;
    }
}

/* ---- GL init / teardown ------------------------------------------------ */

static int viewInitGL(View *v)
{
    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    v->hdc = GetDC(v->hwnd);
    int pf = ChoosePixelFormat(v->hdc, &pfd);
    if (!pf || !SetPixelFormat(v->hdc, pf, &pfd)) return 0;
    v->rc = wglCreateContext(v->hdc);
    if (!v->rc || !wglMakeCurrent(v->hdc, v->rc)) return 0;
    glLoad();
    if (p_wglSwapIntervalEXT) p_wglSwapIntervalEXT(v->kind == VIEW_DIALOG ? 0 : 1);

    if (g_hasShaders) {
        v->progBg = linkProgram(VS_QUAD, FS_BACKGROUND);
        v->progComp = linkProgram(VS_QUAD, FS_COMPOSITE);
        v->progText = linkProgram(VS_TEXT, FS_TEXT);
    }
    v->glOk = v->progBg && v->progComp && v->progText;
    if (v->glOk) {
        GLuint p = v->progBg;
        v->bgFragToWin = p_glGetUniformLocation(p, "uFragToWin");
        v->bgOffset = p_glGetUniformLocation(p, "uOffset");
        v->bgRef = p_glGetUniformLocation(p, "uRef");
        v->bgTime = p_glGetUniformLocation(p, "uTime");
        v->bgSwirl = p_glGetUniformLocation(p, "uSwirl");
        v->bgBright = p_glGetUniformLocation(p, "uBright");
        v->bgDither = p_glGetUniformLocation(p, "uDither");
        getPaletteLocs(p, v->bgPal);
        p = v->progComp;
        v->cpTex = p_glGetUniformLocation(p, "uTex");
        v->cpWin = p_glGetUniformLocation(p, "uWin");
        v->cpOffset = p_glGetUniformLocation(p, "uOffset");
        v->cpMon = p_glGetUniformLocation(p, "uMon");
        v->cpTime = p_glGetUniformLocation(p, "uTime");
        p = v->progText;
        v->txInv = p_glGetUniformLocation(p, "uTextInv");
        v->txSurface = p_glGetUniformLocation(p, "uSurface");
        v->txColor = p_glGetUniformLocation(p, "uColor");
        v->txHalf = p_glGetUniformLocation(p, "uHalf");
        v->txTime = p_glGetUniformLocation(p, "uTime");
        getPaletteLocs(p, v->txPal);
    }
    QueryPerformanceFrequency(&v->qpf);
    v->lastT = nowSec(v);
    v->msWanted = -1;
    statesInit(v);
    viewLayout(v);
    return 1;
}

static void viewFreeGL(View *v)
{
    if (v->rc) {
        wglMakeCurrent(v->hdc, v->rc);
        glyphsFree(v);
        if (g_hasFbo) { fboFreeBg(v); fboFreeMs(v); }
        if (v->progBg) p_glDeleteProgram(v->progBg);
        if (v->progComp) p_glDeleteProgram(v->progComp);
        if (v->progText) p_glDeleteProgram(v->progText);
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(v->rc);
        v->rc = NULL;
    }
    if (v->hdc) { ReleaseDC(v->hwnd, v->hdc); v->hdc = NULL; }
}

/* ---- drawing ----------------------------------------------------------- */

static void drawQuad(void)
{
    glBegin(GL_TRIANGLE_STRIP);
    glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(-1, 1); glVertex2f(1, 1);
    glEnd();
}

typedef struct {
    WCHAR str[128];
    int   len;
    Box   box;
    float ext;
} TextInfo;

static void currentText(View *v, TextInfo *ti)
{
    if (g_cfg.clock) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        if (!GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, ti->str, 64))
            wsprintfW(ti->str, L"%d:%02d", st.wHour, st.wMinute);
    } else {
        wcsncpy(ti->str, g_cfg.text, 127);
        ti->str[127] = 0;
    }
    ti->len = (int)wcslen(ti->str);
    if (ti->len == 0) { wcscpy(ti->str, L" "); ti->len = 1; }
    ti->box = measureText(v, ti->str, ti->len);
    ti->ext = v->glyphs.extrusion;
}

/* Draws one monitor's slice of a scene. (sx,sy,sw,sh) is the scene rect and
   m the monitor, both in window pixels with a top-left origin. */
static void drawTextScene(View *v, const TextInfo *ti, int si, const Mon *m,
                          int sx, int sy, int sw, int sh, float pal[5][3])
{
    const float D = 1.0f / tanf(CAM_FOV_DEG * 0.5f * 3.14159265f / 180.0f);
    const float n = 0.25f;
    float a = (float)sw / (float)sh;

    /* sub-frustum for this monitor inside the scene frustum */
    float X0 = ((float)(m->x - sx) / sw * 2.0f - 1.0f) * a;
    float X1 = ((float)(m->x + m->w - sx) / sw * 2.0f - 1.0f) * a;
    float Yt = 1.0f - (float)(m->y - sy) / sh * 2.0f;
    float Yb = 1.0f - (float)(m->y + m->h - sy) / sh * 2.0f;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(X0 * n / D, X1 * n / D, Yb * n / D, Yt * n / D, n, D + 6.0f);

    /* fit size to the scene, then motion range */
    float hw = (ti->box.maxx - ti->box.minx) * 0.5f;
    float hh = (ti->box.maxy - ti->box.miny) * 0.5f;
    float hd = ti->ext * 0.5f;
    float cx = (ti->box.maxx + ti->box.minx) * 0.5f;
    float cy = (ti->box.maxy + ti->box.miny) * 0.5f;
    float sc = 0.25f + g_cfg.size / 100.0f * 1.15f;  /* em height in world units (screen = 2) */
    float ex, ey, zmax;
    for (int k = 0; k < 8; k++) {
        textExtents(g_cfg.rotation, hw * sc, hh * sc, hd * sc, D, &ex, &ey, &zmax);
        float f = fminf(a * 0.92f / ex, 0.92f / ey);
        if (f >= 1.0f) break;
        sc *= f * 0.98f;
    }
    textExtents(g_cfg.rotation, hw * sc, hh * sc, hd * sc, D, &ex, &ey, &zmax);
    float shrink = (D - zmax) / D;
    float rangeX = fmaxf(0.0f, a * 0.93f * shrink - ex);
    float rangeY = fmaxf(0.0f, 0.93f * shrink - ey);
    float nx, ny;
    statePos(v, si, &nx, &ny);

    float rx, ry, rz;
    rotationAngles(g_cfg.rotation, (float)v->animT + v->states[si].phase, &rx, &ry, &rz);

    M4 base = m4mul(m4tr(0, 0, -D), m4tr(nx * rangeX, ny * rangeY, 0));
    base = m4mul(base, m4rot(2, rz));
    base = m4mul(base, m4rot(1, ry));
    base = m4mul(base, m4rot(0, rx));
    base = m4mul(base, m4sc(sc));
    M4 inv = m4inv(base);
    M4 mv = m4mul(base, m4tr(-cx, -cy, hd));

    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(mv.m);

    if (v->glOk) {
        float col[3];
        colorrefToRgb(g_cfg.textColor, col);
        p_glUseProgram(v->progText);
        p_glUniformMatrix4fv(v->txInv, 1, GL_FALSE, inv.m);
        p_glUniform1i(v->txSurface, g_cfg.surface);
        p_glUniform3f(v->txColor, col[0], col[1], col[2]);
        p_glUniform2f(v->txHalf, hw > 0.01f ? hw : 0.01f, hh > 0.01f ? hh : 0.01f);
        p_glUniform1f(v->txTime, (float)v->colorT);
        setPaletteUniforms(v->txPal, pal);
    } else {
        GLfloat lp[4] = { -0.45f, 0.65f, 0.6f, 0.0f };
        GLfloat white[4] = { 1, 1, 1, 1 };
        float col[3];
        if (g_cfg.surface == SURF_SOLID) colorrefToRgb(g_cfg.textColor, col);
        else { col[0] = pal[2][0] * 0.6f + 0.4f; col[1] = pal[2][1] * 0.6f + 0.4f; col[2] = pal[2][2] * 0.6f + 0.4f; }
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glEnable(GL_NORMALIZE);
        glEnable(GL_COLOR_MATERIAL);
        glPushMatrix();
        glLoadIdentity();
        glLightfv(GL_LIGHT0, GL_POSITION, lp);
        glPopMatrix();
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, white);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 50.0f);
        glColor3f(col[0], col[1], col[2]);
    }

    for (int i = 0; i < ti->len; i++) {
        GLuint list = glyphGet(v, ti->str[i]);
        if (list) glCallList(list);
    }

    if (v->glOk) p_glUseProgram(0);
    else { glDisable(GL_LIGHTING); glDisable(GL_COLOR_MATERIAL); }
}

static void viewRender(View *v)
{
    if (!v->rc) return;
    wglMakeCurrent(v->hdc, v->rc);

    double now = nowSec(v);
    double dt = now - v->lastT;
    if (dt < 0) dt = 0;
    if (dt > 0.1) dt = 0.1;
    v->lastT = now;
    v->flowT += dt * (0.05 + g_cfg.flow / 100.0 * 1.6);
    v->animT += dt * (0.1 + g_cfg.speed / 100.0 * 1.9);
    v->colorT += dt * 0.5;
    statesUpdate(v, (float)dt);

    glyphsEnsureFont(v);
    TextInfo ti;
    currentText(v, &ti);

    float pal[5][3];
    currentPalette(pal);
    float bright = 0.35f + g_cfg.bright / 100.0f * 0.95f;

    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    int haveBg = 0;
    if (v->glOk && g_hasFbo) {
        fboEnsureBg(v);
        fboEnsureMs(v);
        haveBg = v->bgFbo != 0;
    }

    /* 1. background, once for the whole window */
    if (haveBg) {
        p_glBindFramebuffer(GL_FRAMEBUFFER_, v->bgFbo);
        glViewport(0, 0, v->bgW, v->bgH);
        p_glUseProgram(v->progBg);
        p_glUniform2f(v->bgFragToWin, (float)v->w / v->bgW, (float)v->h / v->bgH);
        p_glUniform2f(v->bgOffset, 0, 0);
        p_glUniform1f(v->bgRef, v->refPx);
        p_glUniform1f(v->bgTime, (float)v->flowT);
        p_glUniform1f(v->bgSwirl, 0.15f + g_cfg.swirl / 100.0f * 1.6f);
        p_glUniform1f(v->bgBright, bright);
        p_glUniform1f(v->bgDither, 0.0f);
        setPaletteUniforms(v->bgPal, pal);
        drawQuad();
        p_glBindFramebuffer(GL_FRAMEBUFFER_, 0);
    }

    glViewport(0, 0, v->w, v->h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* union of monitors, for spanning mode */
    int ux0 = v->mons[0].x, uy0 = v->mons[0].y, ux1 = ux0 + v->mons[0].w, uy1 = uy0 + v->mons[0].h;
    for (int i = 1; i < v->nmons; i++) {
        const Mon *m = &v->mons[i];
        if (m->x < ux0) ux0 = m->x;
        if (m->y < uy0) uy0 = m->y;
        if (m->x + m->w > ux1) ux1 = m->x + m->w;
        if (m->y + m->h > uy1) uy1 = m->y + m->h;
    }

    for (int i = 0; i < v->nmons; i++) {
        const Mon *m = &v->mons[i];
        int glx = m->x, gly = v->h - (m->y + m->h);
        int useMs = v->msFbo && m->w <= v->msW && m->h <= v->msH;
        float offX = useMs ? (float)glx : 0.0f, offY = useMs ? (float)gly : 0.0f;

        if (useMs) {
            p_glBindFramebuffer(GL_FRAMEBUFFER_, v->msFbo);
            glViewport(0, 0, m->w, m->h);
            glDisable(GL_SCISSOR_TEST);
        } else {
            glViewport(glx, gly, m->w, m->h);
            glEnable(GL_SCISSOR_TEST);
            glScissor(glx, gly, m->w, m->h);
        }

        /* 2. background into this monitor */
        glDisable(GL_DEPTH_TEST);
        if (haveBg) {
            p_glUseProgram(v->progComp);
            glBindTexture(GL_TEXTURE_2D, v->bgTex);
            p_glUniform1i(v->cpTex, 0);
            p_glUniform2f(v->cpWin, (float)v->w, (float)v->h);
            p_glUniform2f(v->cpOffset, offX, offY);
            p_glUniform4f(v->cpMon, (float)glx, (float)gly, (float)m->w, (float)m->h);
            p_glUniform1f(v->cpTime, (float)v->colorT);
            drawQuad();
            p_glUseProgram(0);
        } else if (v->glOk) {
            /* no FBO support: draw the flow field directly at full resolution */
            p_glUseProgram(v->progBg);
            p_glUniform2f(v->bgFragToWin, 1, 1);
            p_glUniform2f(v->bgOffset, offX, offY);
            p_glUniform1f(v->bgRef, v->refPx);
            p_glUniform1f(v->bgTime, (float)v->flowT);
            p_glUniform1f(v->bgSwirl, 0.15f + g_cfg.swirl / 100.0f * 1.6f);
            p_glUniform1f(v->bgBright, bright);
            p_glUniform1f(v->bgDither, 1.0f);
            setPaletteUniforms(v->bgPal, pal);
            drawQuad();
            p_glUseProgram(0);
        } else {
            /* OpenGL 1.1 (no driver): slow colour drift */
            float t = (float)v->flowT * 0.3f;
            float k0 = 0.5f + 0.5f * sinf(t), k1 = 0.5f + 0.5f * sinf(t + 2.1f);
            glMatrixMode(GL_PROJECTION); glLoadIdentity();
            glMatrixMode(GL_MODELVIEW); glLoadIdentity();
            glBegin(GL_TRIANGLE_STRIP);
            glColor3f((pal[0][0] + pal[1][0] * k0) * bright, (pal[0][1] + pal[1][1] * k0) * bright, (pal[0][2] + pal[1][2] * k0) * bright);
            glVertex2f(-1, -1);
            glColor3f(pal[0][0] * bright, pal[0][1] * bright, pal[0][2] * bright);
            glVertex2f(1, -1);
            glColor3f((pal[0][0] + pal[3][0] * k1) * bright, (pal[0][1] + pal[3][1] * k1) * bright, (pal[0][2] + pal[3][2] * k1) * bright);
            glVertex2f(-1, 1);
            glColor3f((pal[0][0] + pal[2][0] * 0.6f) * bright, (pal[0][1] + pal[2][1] * 0.6f) * bright, (pal[0][2] + pal[2][2] * 0.6f) * bright);
            glVertex2f(1, 1);
            glEnd();
        }

        /* 3. text */
        int showText = 1, si = i, sx = m->x, sy = m->y, sw = m->w, sh = m->h;
        switch (v->kind == VIEW_SAVER ? g_cfg.monitors : MON_EACH) {
        case MON_MIRROR:  si = 0; break;
        case MON_SPAN:    si = 0; sx = ux0; sy = uy0; sw = ux1 - ux0; sh = uy1 - uy0; break;
        case MON_PRIMARY: si = 0; showText = m->primary; break;
        default: break;
        }
        if (showText) {
            glClear(GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            drawTextScene(v, &ti, si, m, sx, sy, sw, sh, pal);
            glDisable(GL_DEPTH_TEST);
        }

        if (useMs) {
            p_glBindFramebuffer(GL_READ_FRAMEBUFFER_, v->msFbo);
            p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER_, 0);
            p_glBlitFramebuffer(0, 0, m->w, m->h, glx, gly, glx + m->w, gly + m->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            p_glBindFramebuffer(GL_FRAMEBUFFER_, 0);
        }
    }
    glDisable(GL_SCISSOR_TEST);
    SwapBuffers(v->hdc);
}

/* ---- window procedure -------------------------------------------------- */

static void saverClose(View *v)
{
    if (v->alive && v->hwnd) DestroyWindow(v->hwnd);
}

static LRESULT CALLBACK ViewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    View *v = (View *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        v = (View *)((CREATESTRUCTW *)lp)->lpCreateParams;
        v->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)v);
    }
    if (!v) return DefWindowProcW(hwnd, msg, wp, lp);

    int saver = v->kind == VIEW_SAVER;
    int armed = saver && GetTickCount64() - v->startTick > 400;

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        if (v->rc) viewLayout(v);
        return 0;
    case WM_SETCURSOR:
        if (saver) { SetCursor(NULL); return TRUE; }
        break;
    case WM_MOUSEMOVE:
        if (saver) {
            POINT p;
            GetCursorPos(&p);
            if (abs(p.x - v->cursor0.x) > 12 || abs(p.y - v->cursor0.y) > 12) {
                if (armed) saverClose(v);
                else v->cursor0 = p;
            }
            return 0;
        }
        break;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_XBUTTONDOWN:
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
        if (saver) { if (armed) saverClose(v); return 0; }
        break;
    case WM_ACTIVATEAPP:
        if (saver && !wp && armed) saverClose(v);
        break;
    case WM_SYSCOMMAND:
        if (saver && ((wp & 0xFFF0) == SC_SCREENSAVE || (wp & 0xFFF0) == SC_MONITORPOWER)) {
            if ((wp & 0xFFF0) == SC_SCREENSAVE) return 0;
        }
        break;
    case WM_DISPLAYCHANGE:
        if (saver) saverClose(v);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        viewFreeGL(v);
        v->alive = 0;
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        v->hwnd = NULL;
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void registerViewClass(void)
{
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = ViewProc;
    wc.hInstance = g_inst;
    wc.hIcon = LoadIconW(g_inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = VIEW_CLASS;
    RegisterClassExW(&wc);
}

static View *viewCreate(int kind, HWND parent, int x, int y, int w, int h)
{
    View *v = (View *)calloc(1, sizeof(View));
    DWORD style, exStyle = 0;
    v->kind = kind;
    v->parent = parent;
    v->alive = 1;
    v->startTick = GetTickCount64();
    GetCursorPos(&v->cursor0);
    if (kind == VIEW_SAVER) {
        style = WS_POPUP | WS_VISIBLE;
        exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    } else {
        style = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_DISABLED;
    }
    CreateWindowExW(exStyle, VIEW_CLASS, L"Flow Text 3D", style, x, y, w, h, parent, NULL, g_inst, v);
    if (!v->hwnd) { free(v); return NULL; }
    if (!viewInitGL(v)) {
        DestroyWindow(v->hwnd);
        free(v);
        return NULL;
    }
    return v;
}

static void viewDestroy(View *v)
{
    if (!v) return;
    if (v->hwnd) DestroyWindow(v->hwnd);
    free(v);
}

/* Runs a full-screen or settings-preview view until it closes. */
static void runView(int kind, HWND parent)
{
    View *v;
    if (kind == VIEW_SAVER) {
        v = viewCreate(kind, NULL,
                       GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                       GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
    } else {
        RECT r;
        GetClientRect(parent, &r);
        v = viewCreate(kind, parent, 0, 0, r.right, r.bottom);
    }
    if (!v) return;
    if (kind == VIEW_SAVER) {
        SetForegroundWindow(v->hwnd);
        SetFocus(v->hwnd);
        ShowCursor(FALSE);
    }
    timeBeginPeriod(1);
    double frameMin = (g_cfg.fps == 1 || kind == VIEW_PREVIEW) ? 1.0 / 30.0 : (p_wglSwapIntervalEXT ? 0.0 : 1.0 / 60.0);
    while (v->alive) {
        MSG msg;
        double t0 = nowSec(v);
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                PostQuitMessage((int)msg.wParam);
                saverClose(v);
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!v->alive) break;
        if (kind == VIEW_PREVIEW && !IsWindow(parent)) break;
        viewRender(v);
        double left = frameMin - (nowSec(v) - t0);
        if (left > 0.001) Sleep((DWORD)(left * 1000.0));
    }
    timeEndPeriod(1);
    if (kind == VIEW_SAVER) ShowCursor(TRUE);
    viewDestroy(v);
}

/* ------------------------------------------------------------------------ */
/* Settings dialog                                                          */
/* ------------------------------------------------------------------------ */

static View *g_dlgView;
static int g_dlgLoading;

static void comboFill(HWND dlg, int id, const WCHAR *const *items, int n, int sel)
{
    HWND c = GetDlgItem(dlg, id);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < n; i++) SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)items[i]);
    SendMessageW(c, CB_SETCURSEL, (WPARAM)sel, 0);
}

static void trackInit(HWND dlg, int id, int pos)
{
    HWND t = GetDlgItem(dlg, id);
    SendMessageW(t, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(t, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(t, TBM_SETPOS, TRUE, pos);
}

static void updateFontLabel(HWND dlg)
{
    WCHAR buf[LF_FACESIZE + 32];
    const WCHAR *w = g_cfg.weight >= 850 ? L" Black" : g_cfg.weight >= 650 ? L" Bold" :
                     g_cfg.weight >= 550 ? L" Semibold" : g_cfg.weight <= 350 ? L" Light" : L"";
    wsprintfW(buf, L"%s%s%s", g_cfg.face, w, g_cfg.italic ? L" Italic" : L"");
    SetDlgItemTextW(dlg, IDC_FONTNAME, buf);
}

static void updateEnables(HWND dlg)
{
    EnableWindow(GetDlgItem(dlg, IDC_TEXT), !g_cfg.clock);
    EnableWindow(GetDlgItem(dlg, IDC_TEXTCOLOR), g_cfg.surface == SURF_SOLID);
    for (int i = 0; i < 4; i++) {
        EnableWindow(GetDlgItem(dlg, IDC_CUST1 + i), g_cfg.palette == PALETTE_CUSTOM);
        InvalidateRect(GetDlgItem(dlg, IDC_CUST1 + i), NULL, TRUE);
    }
    InvalidateRect(GetDlgItem(dlg, IDC_TEXTCOLOR), NULL, TRUE);
}

static void dialogFromSettings(HWND dlg)
{
    g_dlgLoading = 1;
    CheckRadioButton(dlg, IDC_RADIO_TEXT, IDC_RADIO_CLOCK, g_cfg.clock ? IDC_RADIO_CLOCK : IDC_RADIO_TEXT);
    SetDlgItemTextW(dlg, IDC_TEXT, g_cfg.text);
    updateFontLabel(dlg);
    comboFill(dlg, IDC_ROTATION, ROT_NAMES, ROT_COUNT, g_cfg.rotation);
    comboFill(dlg, IDC_MOTION, MOT_NAMES, MOT_COUNT, g_cfg.motion);
    comboFill(dlg, IDC_SURFACE, SURF_NAMES, SURF_COUNT, g_cfg.surface);
    comboFill(dlg, IDC_MONITORS, MON_NAMES, MON_COUNT, g_cfg.monitors);
    comboFill(dlg, IDC_QUALITY, QUAL_NAMES, 3, g_cfg.quality);
    comboFill(dlg, IDC_AA, AA_NAMES, 3, g_cfg.aa);
    comboFill(dlg, IDC_FPS, FPS_NAMES, 2, g_cfg.fps);
    {
        const WCHAR *names[PALETTE_COUNT];
        for (int i = 0; i < PALETTE_COUNT; i++) names[i] = PALETTES[i].name;
        comboFill(dlg, IDC_PALETTE, names, PALETTE_COUNT, g_cfg.palette);
    }
    trackInit(dlg, IDC_TEXTSIZE, g_cfg.size);
    trackInit(dlg, IDC_DEPTH, g_cfg.depth);
    trackInit(dlg, IDC_SPEED, g_cfg.speed);
    trackInit(dlg, IDC_FLOW, g_cfg.flow);
    trackInit(dlg, IDC_SWIRL, g_cfg.swirl);
    trackInit(dlg, IDC_BRIGHT, g_cfg.bright);
    updateEnables(dlg);
    g_dlgLoading = 0;
}

static int pickColor(HWND owner, COLORREF *c)
{
    static COLORREF custom[16];
    CHOOSECOLORW cc;
    memset(&cc, 0, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = owner;
    cc.rgbResult = *c;
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
    if (!ChooseColorW(&cc)) return 0;
    *c = cc.rgbResult;
    return 1;
}

static void pickFont(HWND dlg)
{
    LOGFONTW lf;
    CHOOSEFONTW cf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -24;
    lf.lfWeight = g_cfg.weight;
    lf.lfItalic = (BYTE)g_cfg.italic;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy(lf.lfFaceName, g_cfg.face, LF_FACESIZE - 1);
    memset(&cf, 0, sizeof(cf));
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = dlg;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_TTONLY | CF_NOVERTFONTS |
               CF_SCALABLEONLY | CF_NOSIZESEL | CF_FORCEFONTEXIST;
    if (!ChooseFontW(&cf)) return;
    wcsncpy(g_cfg.face, lf.lfFaceName, LF_FACESIZE - 1);
    g_cfg.face[LF_FACESIZE - 1] = 0;
    g_cfg.weight = lf.lfWeight ? lf.lfWeight : 400;
    g_cfg.italic = lf.lfItalic ? 1 : 0;
    updateFontLabel(dlg);
}

static void drawSwatch(const DRAWITEMSTRUCT *d)
{
    COLORREF c = d->CtlID == IDC_TEXTCOLOR ? g_cfg.textColor : g_cfg.custom[d->CtlID - IDC_CUST1];
    RECT r = d->rcItem;
    int disabled = (d->itemState & ODS_DISABLED) != 0;
    FillRect(d->hDC, &r, GetSysColorBrush(COLOR_BTNFACE));
    DrawEdge(d->hDC, &r, (d->itemState & ODS_SELECTED) ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT | BF_ADJUST);
    InflateRect(&r, -2, -2);
    if (disabled) {
        int g = (GetRValue(c) + GetGValue(c) + GetBValue(c)) / 3;
        c = RGB((g + 400) / 3, (g + 400) / 3, (g + 400) / 3);
    }
    HBRUSH b = CreateSolidBrush(c);
    FillRect(d->hDC, &r, b);
    DeleteObject(b);
    if (d->itemState & ODS_FOCUS) {
        InflateRect(&r, 1, 1);
        DrawFocusRect(d->hDC, &r);
    }
}

static void createDialogPreview(HWND dlg)
{
    HWND area = GetDlgItem(dlg, IDC_PREVIEWAREA);
    RECT r;
    GetWindowRect(area, &r);
    MapWindowPoints(NULL, dlg, (POINT *)&r, 2);
    ShowWindow(area, SW_HIDE);
    g_dlgView = viewCreate(VIEW_DIALOG, dlg, r.left, r.top, r.right - r.left, r.bottom - r.top);
    if (g_dlgView) SetTimer(dlg, 1, 16, NULL);
}

static INT_PTR CALLBACK ConfigProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        HICON icon = LoadIconW(g_inst, MAKEINTRESOURCEW(IDI_APP));
        SendMessageW(dlg, WM_SETICON, ICON_BIG, (LPARAM)icon);
        SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)icon);
        SendDlgItemMessageW(dlg, IDC_TEXT, EM_LIMITTEXT, 127, 0);
        dialogFromSettings(dlg);
        createDialogPreview(dlg);
        return TRUE;
    }
    case WM_TIMER:
        if (g_dlgView && IsWindowVisible(dlg)) viewRender(g_dlgView);
        return TRUE;
    case WM_DRAWITEM:
        drawSwatch((const DRAWITEMSTRUCT *)lp);
        return TRUE;
    case WM_HSCROLL: {
        int pos = (int)SendMessageW((HWND)lp, TBM_GETPOS, 0, 0);
        switch (GetDlgCtrlID((HWND)lp)) {
        case IDC_TEXTSIZE:   g_cfg.size = pos; break;
        case IDC_DEPTH:  g_cfg.depth = pos; break;
        case IDC_SPEED:  g_cfg.speed = pos; break;
        case IDC_FLOW:   g_cfg.flow = pos; break;
        case IDC_SWIRL:  g_cfg.swirl = pos; break;
        case IDC_BRIGHT: g_cfg.bright = pos; break;
        }
        return TRUE;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (g_dlgLoading) return TRUE;
        if (code == CBN_SELCHANGE) {
            int sel = (int)SendMessageW((HWND)lp, CB_GETCURSEL, 0, 0);
            if (sel < 0) return TRUE;
            switch (id) {
            case IDC_ROTATION: g_cfg.rotation = sel; break;
            case IDC_MOTION:   g_cfg.motion = sel; break;
            case IDC_SURFACE:  g_cfg.surface = sel; break;
            case IDC_PALETTE:  g_cfg.palette = sel; break;
            case IDC_MONITORS: g_cfg.monitors = sel; break;
            case IDC_QUALITY:  g_cfg.quality = sel; break;
            case IDC_AA:       g_cfg.aa = sel; break;
            case IDC_FPS:      g_cfg.fps = sel; break;
            }
            updateEnables(dlg);
            return TRUE;
        }
        switch (id) {
        case IDC_TEXT:
            if (code == EN_CHANGE) GetDlgItemTextW(dlg, IDC_TEXT, g_cfg.text, 128);
            return TRUE;
        case IDC_RADIO_TEXT:
        case IDC_RADIO_CLOCK:
            g_cfg.clock = IsDlgButtonChecked(dlg, IDC_RADIO_CLOCK) == BST_CHECKED;
            updateEnables(dlg);
            return TRUE;
        case IDC_FONT:
            pickFont(dlg);
            return TRUE;
        case IDC_TEXTCOLOR:
            if (pickColor(dlg, &g_cfg.textColor)) InvalidateRect((HWND)lp, NULL, TRUE);
            return TRUE;
        case IDC_CUST1: case IDC_CUST2: case IDC_CUST3: case IDC_CUST4:
            if (pickColor(dlg, &g_cfg.custom[id - IDC_CUST1])) InvalidateRect((HWND)lp, NULL, TRUE);
            return TRUE;
        case IDC_DEFAULTS:
            cfgDefaults(&g_cfg);
            dialogFromSettings(dlg);
            return TRUE;
        case IDC_TEST:
            KillTimer(dlg, 1);
            ShowWindow(dlg, SW_HIDE);
            runView(VIEW_SAVER, NULL);
            ShowWindow(dlg, SW_SHOW);
            SetForegroundWindow(dlg);
            if (g_dlgView) SetTimer(dlg, 1, 16, NULL);
            return TRUE;
        case IDOK:
            cfgSave(&g_cfg);
            EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    case WM_DESTROY:
        KillTimer(dlg, 1);
        viewDestroy(g_dlgView);
        g_dlgView = NULL;
        break;
    }
    return FALSE;
}

/* ------------------------------------------------------------------------ */
/* Entry point                                                              */
/* ------------------------------------------------------------------------ */

static HWND parseHwnd(const WCHAR *s)
{
    while (*s == L':' || *s == L' ') s++;
    return (HWND)(UINT_PTR)wcstoull(s, NULL, 10);
}

static void setDpiAware(void)
{
    typedef BOOL (WINAPI *PFN_SetCtx)(HANDLE);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    PFN_SetCtx f = u ? (PFN_SetCtx)(void *)GetProcAddress(u, "SetProcessDpiAwarenessContext") : NULL;
    if (f) f((HANDLE)-4); /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
    else SetProcessDPIAware();
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    (void)prev; (void)cmd; (void)show;
    g_inst = inst;
    setDpiAware();
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    registerViewClass();
    cfgLoad(&g_cfg);

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    WCHAR mode = L'c';
    HWND hwndArg = NULL;
    if (argv && argc > 1) {
        const WCHAR *a = argv[1];
        if (*a == L'/' || *a == L'-') a++;
        mode = (WCHAR)towlower(*a);
        if (a[0] && a[1]) hwndArg = parseHwnd(a + 1);
        if (!hwndArg && argc > 2) hwndArg = parseHwnd(argv[2]);
    }

    switch (mode) {
    case L's': {
        HANDLE mutex = CreateMutexW(NULL, TRUE, L"FlowText3D.Screensaver.Running");
        if (GetLastError() != ERROR_ALREADY_EXISTS) runView(VIEW_SAVER, NULL);
        if (mutex) CloseHandle(mutex);
        break;
    }
    case L'p':
        if (hwndArg && IsWindow(hwndArg)) runView(VIEW_PREVIEW, hwndArg);
        break;
    default:
        DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_CONFIG),
                        hwndArg && IsWindow(hwndArg) ? hwndArg : GetForegroundWindow(), ConfigProc, 0);
        break;
    }
    if (argv) LocalFree(argv);
    return 0;
}
