// New Alliance Gallery - native animated renderer.
//
// Replaces the Chromium kiosk. Same picture as index.html, same viewBox, same
// palette - but animated, and cheap enough that the Pi can run it all day.
//
// Where the speed comes from
// --------------------------
// The browser version ran one fullscreen quad whose fragment shader looped over
// every circle: 16 distance tests for each of ~2M pixels, ~33M tests a frame,
// most of them on background that is covered by the panel anyway. That is what
// cooked the board.
//
// Here each circle draws its own quad, sized to its bounding box. A fragment is
// visited once, by one circle, and only inside that box - about 30% of the
// screen with no loop at all. Same output, roughly 50x less fragment work, so
// the GPU idles between frames instead of pinning.
//
// The other half is not rendering faster than anyone can see: frames are paced
// to a target rate that a governor lowers as the SoC warms, so the board finds
// its own sustainable speed rather than running flat out until it throttles.
//
// Portability: SDL2 plus GL entry points fetched at runtime. No GL headers, no
// GLEW/glad - the handful of types and enums used are declared below, so the
// same file builds against desktop GL on Windows and GLES2 on the Pi.

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sys/stat.h>   // mtime polling; MinGW and glibc both provide stat()

// ---------------------------------------------------------------- GL bindings

typedef unsigned int  GLenum;
typedef unsigned char GLboolean;
typedef unsigned int  GLbitfield;
typedef int           GLint;
typedef int           GLsizei;
typedef unsigned int  GLuint;
typedef float         GLfloat;
typedef char          GLchar;
typedef ptrdiff_t     GLintptr;
typedef ptrdiff_t     GLsizeiptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RGBA 0x1908
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_LINEAR 0x2601
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLE_STRIP 0x0005
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
#define GL_UNPACK_ALIGNMENT 0x0CF5

#define GL_FUNCS(X)                                                            \
  X(GLuint, CreateShader, (GLenum))                                            \
  X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *))\
  X(void, CompileShader, (GLuint))                                             \
  X(void, GetShaderiv, (GLuint, GLenum, GLint *))                              \
  X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))            \
  X(void, DeleteShader, (GLuint))                                              \
  X(GLuint, CreateProgram, (void))                                             \
  X(void, AttachShader, (GLuint, GLuint))                                      \
  X(void, LinkProgram, (GLuint))                                               \
  X(void, GetProgramiv, (GLuint, GLenum, GLint *))                             \
  X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))           \
  X(void, UseProgram, (GLuint))                                                \
  X(void, GenBuffers, (GLsizei, GLuint *))                                     \
  X(void, BindBuffer, (GLenum, GLuint))                                        \
  X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum))              \
  X(GLint, GetAttribLocation, (GLuint, const GLchar *))                        \
  X(void, EnableVertexAttribArray, (GLuint))                                   \
  X(void, VertexAttribPointer,                                                 \
    (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))                 \
  X(GLint, GetUniformLocation, (GLuint, const GLchar *))                       \
  X(void, Uniform1i, (GLint, GLint))                                           \
  X(void, Uniform1f, (GLint, GLfloat))                                         \
  X(void, Uniform2f, (GLint, GLfloat, GLfloat))                                \
  X(void, GenTextures, (GLsizei, GLuint *))                                    \
  X(void, BindTexture, (GLenum, GLuint))                                       \
  X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum,  \
                       GLenum, const void *))                                  \
  X(void, TexParameteri, (GLenum, GLenum, GLint))                              \
  X(void, ActiveTexture, (GLenum))                                             \
  X(void, PixelStorei, (GLenum, GLint))                                        \
  X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                    \
  X(void, Clear, (GLbitfield))                                                 \
  X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                          \
  X(void, DrawArrays, (GLenum, GLint, GLsizei))                                \
  X(void, Enable, (GLenum))                                                    \
  X(void, Disable, (GLenum))                                                   \
  X(void, BlendFunc, (GLenum, GLenum))                                         \
  X(const unsigned char *, GetString, (GLenum))                                   X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))

#define X(ret, name, args) static ret(APIENTRY *gl##name) args = nullptr;
#ifndef APIENTRY
#ifdef _WIN32
#define APIENTRY __stdcall
#else
#define APIENTRY
#endif
#endif
GL_FUNCS(X)
#undef X

static bool loadGL() {
  bool ok = true;
#define X(ret, name, args)                                                     \
  gl##name = (ret(APIENTRY *) args)SDL_GL_GetProcAddress("gl" #name);          \
  if (!gl##name) {                                                             \
    SDL_Log("missing GL entry point: gl%s", #name);                            \
    ok = false;                                                                \
  }
  GL_FUNCS(X)
#undef X
  return ok;
}

// ------------------------------------------------------------------ the model

struct Circle {
  int id = 0;
  float d = 0, cx = 0, cy = 0;
  // per-circle character, derived from the id so it survives reordering and is
  // the same after every restart
  float period = 24.f;    // seconds for one trip through the palette
  float ringScale = 1.f;  // how much of the palette the radius spans
  float phase = 0.f;      // where in the sequence it starts
};

// Deterministic hash -> [0,1). Three draws per circle, well spread, so no two
// circles land on the same period and the set never re-aligns.
static float hash01(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU;
  x ^= x >> 15; x *= 0x846ca68bU;
  x ^= x >> 16;
  return (float)(x & 0xFFFFFFu) / (float)0x1000000u;
}

// Drift apart rather than beat together: a shared period makes sixteen circles
// read as one object breathing. Each gets its own period, its own start point
// in the colour sequence, and a slightly different radial stride so the bands
// do not line up across circles either.
//
// Periods spread only downward in speed - basePeriod is the fastest, and each
// circle is that or slower - so widening the spread never makes anything run
// faster than it already does.
static void deriveCharacter(std::vector<Circle> &cs, float basePeriod,
                            float spread, float ringJitter, float phaseSpread) {
  for (size_t i = 0; i < cs.size(); i++) {
    Circle &c = cs[i];
    uint32_t k = (uint32_t)(c.id ? c.id : (int)i + 1) * 2654435761u;
    float h1 = hash01(k + 1u), h2 = hash01(k + 2u), h3 = hash01(k + 3u);
    // one-sided: --period is the fastest any circle runs, and the spread only
    // ever lengthens. Nothing speeds up when the spread is widened.
    c.period = basePeriod * (1.f + spread * h1);
    c.ringScale = 1.f + ringJitter * (h2 * 2.f - 1.f);
    c.phase = h3 * phaseSpread;
    if (c.period < 1.f) c.period = 1.f;
  }
}

// world view, identical to index.html's VB
static const float VB_X = 40.f, VB_Y = 30.f, VB_W = 288.f, VB_H = 512.f;
static const int MAX_CIRCLES = 64;

// Just enough JSON for this file: an array of objects with numeric d/cx/cy.
// A real parser would be a dependency for no gain - the file is written by
// server.py and is always the same shape.
static bool parseCoords(const std::string &text, std::vector<Circle> &out) {
  std::vector<Circle> got;
  size_t i = 0;
  auto skipWs = [&] { while (i < text.size() && isspace((unsigned char)text[i])) i++; };
  skipWs();
  if (i >= text.size() || text[i] != '[') return false;
  i++;
  while (true) {
    skipWs();
    if (i >= text.size()) return false;
    if (text[i] == ']') break;
    if (text[i] == ',') { i++; continue; }
    if (text[i] != '{') return false;
    i++;
    Circle c;
    bool haveD = false, haveX = false, haveY = false;
    while (true) {
      skipWs();                       // re-test after skipping: the closing
      if (i >= text.size()) return false;   // brace arrives as whitespace-then-}
      if (text[i] == '}') break;
      if (text[i] == ',') { i++; continue; }
      if (text[i] != '"') return false;
      size_t keyEnd = text.find('"', i + 1);
      if (keyEnd == std::string::npos) return false;
      std::string key = text.substr(i + 1, keyEnd - i - 1);
      i = keyEnd + 1;
      skipWs();
      if (i >= text.size() || text[i] != ':') return false;
      i++;
      skipWs();
      if (i >= text.size()) return false;
      if (text[i] == '"') {           // a string value: skip it, keys we want
        size_t e = text.find('"', i + 1);        // are all numbers
        if (e == std::string::npos) return false;
        i = e + 1;
        continue;
      }
      char *end = nullptr;
      double v = strtod(text.c_str() + i, &end);
      if (end == text.c_str() + i) {  // true / false / null: step over the token
        while (i < text.size() && isalpha((unsigned char)text[i])) i++;
        continue;
      }
      i = (size_t)(end - text.c_str());
      if (key == "d") { c.d = (float)v; haveD = true; }
      else if (key == "cx") { c.cx = (float)v; haveX = true; }
      else if (key == "cy") { c.cy = (float)v; haveY = true; }
      else if (key == "id") { c.id = (int)v; }
    }
    i++;   // past the '}'
    if (!(haveD && haveX && haveY)) return false;
    if (!(std::isfinite(c.d) && std::isfinite(c.cx) && std::isfinite(c.cy))) return false;
    if (c.d > 0 && (int)got.size() < MAX_CIRCLES) got.push_back(c);
  }
  if (got.empty()) return false;
  out.swap(got);
  return true;
}

static bool readFile(const char *path, std::string &out) {
  SDL_RWops *f = SDL_RWFromFile(path, "rb");
  if (!f) return false;
  Sint64 n = SDL_RWsize(f);
  if (n <= 0 || n > (1 << 20)) { SDL_RWclose(f); return false; }
  out.resize((size_t)n);
  size_t got = SDL_RWread(f, &out[0], 1, (size_t)n);
  SDL_RWclose(f);
  out.resize(got);
  return got > 0;
}

// ----------------------------------------------------------------- the shader
// One circle per draw. The vertex shader stretches a unit quad over that
// circle's bounding box and hands the fragment shader world coordinates, so the
// fragment side is the same maths index.html used, minus the loop.

static const char *VERT_SRC = R"(
attribute vec2 aQuad;
uniform vec2  uCenterNdc;
uniform vec2  uHalfNdc;
uniform vec2  uCenterWorld;
uniform float uHalfWorld;
varying vec2  vWorld;
void main() {
  gl_Position = vec4(uCenterNdc + aQuad * uHalfNdc, 0.0, 1.0);
  vWorld = uCenterWorld + aQuad * uHalfWorld;
}
)";

static const char *FRAG_SRC = R"(
varying vec2  vWorld;
uniform vec2  uCenterWorld;
uniform float uRadius;
uniform float uShift;
uniform float uRingScale;
uniform float uScale;
uniform sampler2D uPalette;
void main() {
  float d = distance(vWorld, uCenterWorld);
  float a = clamp((uRadius - d) * uScale, 0.0, 1.0);
  if (a <= 0.0) discard;
  float t  = d / (3.0 * uRadius) * uRingScale;
  float pp = fract(t - uShift);
  vec3  c  = texture2D(uPalette, vec2(pp, 0.5)).rgb;
  vec2  local = (vWorld - uCenterWorld) / uRadius;
  float hi = 1.0 - smoothstep(0.0, 1.15, distance(local, vec2(-0.40, -0.50)));
  float sh = 1.0 - smoothstep(0.0, 1.00, distance(local, vec2( 0.45,  0.62)));
  c = clamp(c + hi * 0.40 - sh * 0.12, 0.0, 1.0);
  gl_FragColor = vec4(c, a);
}
)";

static GLuint compile(GLenum type, const char *body, bool es) {
  // one shader source, two dialects
  const char *pre = es ? "#version 100\nprecision highp float;\n"
                       : "#version 120\n#define highp\n#define mediump\n#define lowp\n";
  std::string src = std::string(pre) + body;
  GLuint s = glCreateShader(type);
  const char *p = src.c_str();
  glShaderSource(s, 1, &p, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048] = {0};
    glGetShaderInfoLog(s, sizeof(log) - 1, nullptr, log);
    SDL_Log("shader compile failed:\n%s", log);
    return 0;
  }
  return s;
}

// ---------------------------------------------------------------- the palette
// Same 24 bands and the same two box blurs as index.html, so the rings land in
// the same places and the native build is not a different picture.

static void buildPalette(std::vector<uint8_t> &data, int W) {
  struct Band { const char *hex; float w; };
  static const Band bands[] = {
      {"FF1E78", 10.0f}, {"D81B72", 3.5f}, {"00B5D9", 3.5f}, {"7AD7DC", 2.5f},
      {"F2D1C7", 2.5f},  {"FF1020", 8.5f}, {"D10018", 5.5f}, {"FF1A32", 6.5f},
      {"005D87", 7.5f},  {"53B4D8", 3.5f}, {"FF1728", 5.0f}, {"FF6B7D", 2.5f},
      {"D7C9BF", 8.5f},  {"E8DDD1", 4.5f}, {"2EB4E8", 2.5f}, {"FF7A86", 2.5f},
      {"E00018", 5.5f},  {"D60017", 5.5f}, {"2AA7CC", 3.5f}, {"D8CCC5", 6.0f},
      {"E5F000", 4.0f},  {"10C0CC", 5.0f}, {"FF5A8A", 5.0f}, {"FF1F8C", 6.0f}};
  const int N = (int)(sizeof(bands) / sizeof(bands[0]));
  float total = 0;
  for (int i = 0; i < N; i++) total += bands[i].w;

  data.assign((size_t)W * 4, 255);
  float acc = 0;
  std::vector<float> lo(N), hi(N);
  for (int i = 0; i < N; i++) { lo[i] = acc / total; acc += bands[i].w; hi[i] = acc / total; }
  for (int x = 0; x < W; x++) {
    float p = (float)x / (float)W;
    int pick = N - 1;
    for (int i = 0; i < N; i++) if (p >= lo[i] && p < hi[i]) { pick = i; break; }
    unsigned v = (unsigned)strtoul(bands[pick].hex, nullptr, 16);
    data[(size_t)x * 4 + 0] = (uint8_t)((v >> 16) & 255);
    data[(size_t)x * 4 + 1] = (uint8_t)((v >> 8) & 255);
    data[(size_t)x * 4 + 2] = (uint8_t)(v & 255);
    data[(size_t)x * 4 + 3] = 255;
  }
  std::vector<uint8_t> src;
  for (int pass = 0; pass < 2; pass++) {
    src = data;
    const int rad = 5;
    for (int x = 0; x < W; x++) {
      int r = 0, g = 0, b = 0, n = 0;
      for (int k = -rad; k <= rad; k++) {
        int xx = ((x + k) % W + W) % W;
        r += src[(size_t)xx * 4 + 0];
        g += src[(size_t)xx * 4 + 1];
        b += src[(size_t)xx * 4 + 2];
        n++;
      }
      data[(size_t)x * 4 + 0] = (uint8_t)(r / n);
      data[(size_t)x * 4 + 1] = (uint8_t)(g / n);
      data[(size_t)x * 4 + 2] = (uint8_t)(b / n);
    }
  }
}

// ------------------------------------------------------------ thermal governor
// The Pi will happily render until it throttles itself. Rather than let it get
// there, drop the frame rate as the SoC warms: full rate while cool, gently
// down through the warm band, and a slow crawl if it ever gets hot. The picture
// drifts over ~22 s, so even 12 fps still reads as smooth motion.

struct Governor {
  float tempC = 0;
  float fps = 60;
  Uint64 lastPoll = 0;
  float fpsCool, fpsHot, tWarm, tHot;

  Governor(float fpsCool_, float fpsHot_, float tWarm_, float tHot_)
      : fpsCool(fpsCool_), fpsHot(fpsHot_), tWarm(tWarm_), tHot(tHot_) { fps = fpsCool_; }

  static float readTempC() {
#ifdef _WIN32
    return 0.f;   // no thermal zone; nothing to govern
#else
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!f) return 0.f;
    long milli = 0;
    if (fscanf(f, "%ld", &milli) != 1) milli = 0;
    fclose(f);
    return (float)milli / 1000.f;
#endif
  }

  void update(Uint64 nowMs) {
    if (lastPoll && nowMs - lastPoll < 1000) return;
    lastPoll = nowMs;
    float t = readTempC();
    if (t <= 0) { tempC = 0; fps = fpsCool; return; }
    tempC = t;
    float target;
    if (t <= tWarm) target = fpsCool;
    else if (t >= tHot) target = fpsHot;
    else {
      float k = (t - tWarm) / (tHot - tWarm);
      target = fpsCool + (fpsHot - fpsCool) * k;
    }
    // ease, so a brief spike does not visibly step the animation
    fps += (target - fps) * 0.34f;
  }
};

// ------------------------------------------------------------------------ main

static void usage() {
  printf(
      "New Alliance Gallery - native renderer\n"
      "  --coords PATH   circle file to render (default ./coords.json)\n"
      "  --windowed WxH  run in a window instead of fullscreen\n"
      "  --fps N         frame rate ceiling while cool (default 60)\n"
      "  --min-fps N     frame rate once hot (default 12)\n"
      "  --warm C        temperature where slowing starts (default 65)\n"
      "  --hot C         temperature of the floor rate (default 78)\n"
      "  --speed X       animation speed multiplier (default 1.0)\n"
      "  --static        no animation, draw one frame and idle\n"
      "  --bench         print fps / frame ms / temperature every 2 s\n"
      "  --help\n");
}

int main(int argc, char **argv) {
  std::string coordsPath = "coords.json";
  int winW = 0, winH = 0;   // 0 = fullscreen desktop
  float fpsCool = 60, fpsHot = 12, tWarm = 65, tHot = 78, speed = 1.f;
  float basePeriod = 24.f, spread = 0.8f, ringJitter = 0.05f, phaseSpread = 1.f;
  bool bench = false, staticMode = false, vsync = true;
  std::string shotPath;

  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&](float def) { return (i + 1 < argc) ? (float)atof(argv[++i]) : def; };
    if (a == "--coords" && i + 1 < argc) coordsPath = argv[++i];
    else if (a == "--windowed" && i + 1 < argc) { sscanf(argv[++i], "%dx%d", &winW, &winH); }
    else if (a == "--fps") fpsCool = next(60);
    else if (a == "--min-fps") fpsHot = next(12);
    else if (a == "--warm") tWarm = next(65);
    else if (a == "--hot") tHot = next(78);
    else if (a == "--speed") speed = next(1.f);
    else if (a == "--period") basePeriod = next(24.f);
    else if (a == "--spread") spread = next(0.8f);
    else if (a == "--ring-jitter") ringJitter = next(0.05f);
    else if (a == "--phase-spread") phaseSpread = next(1.f);
    else if (a == "--sync") { spread = 0.f; ringJitter = 0.f; phaseSpread = 0.f; }
    else if (a == "--bench") bench = true;
    else if (a == "--novsync") vsync = false;
    else if (a == "--shot" && i + 1 < argc) shotPath = argv[++i];
    else if (a == "--static") staticMode = true;
    else if (a == "--help" || a == "-h") { usage(); return 0; }
    else { fprintf(stderr, "unknown option: %s\n", a.c_str()); usage(); return 2; }
  }
  if (tHot <= tWarm) tHot = tWarm + 1.f;
  if (basePeriod < 1.f) basePeriod = 1.f;
  spread = std::min(3.f, std::max(0.f, spread));
  ringJitter = std::min(0.95f, std::max(0.f, ringJitter));
  phaseSpread = std::min(1.f, std::max(0.f, phaseSpread));
  if (fpsCool < 1) fpsCool = 1;
  if (fpsHot < 1) fpsHot = 1;

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }

  // Ask for GLES2 first: that is what the Pi's VideoCore driver wants, and it
  // is the profile the shaders are written for. Desktop GL is the fallback so
  // the same binary logic works on the dev machine.
  bool es = true;
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

  Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;
  if (!winW || !winH) { flags |= SDL_WINDOW_FULLSCREEN_DESKTOP; winW = 800; winH = 1422; }

  SDL_Window *win = SDL_CreateWindow("New Alliance Gallery", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, winW, winH, flags);
  SDL_GLContext ctx = win ? SDL_GL_CreateContext(win) : nullptr;
  if (!ctx) {
    if (win) SDL_DestroyWindow(win);
    es = false;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    win = SDL_CreateWindow("New Alliance Gallery", SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_CENTERED, winW, winH, flags);
    ctx = win ? SDL_GL_CreateContext(win) : nullptr;
  }
  if (!win || !ctx) {
    fprintf(stderr, "no GL context: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  SDL_GL_MakeCurrent(win, ctx);
  SDL_GL_SetSwapInterval(vsync ? 1 : 0);   // vsync; the pacer only slows us further
  SDL_ShowCursor(SDL_DISABLE);
  SDL_DisableScreenSaver();

  if (!loadGL()) { fprintf(stderr, "could not load GL entry points\n"); return 1; }
  SDL_Log("GL %s | %s | %s", (const char *)glGetString(GL_VERSION),
          (const char *)glGetString(GL_RENDERER), es ? "GLES2" : "desktop");

  GLuint vs = compile(GL_VERTEX_SHADER, VERT_SRC, es);
  GLuint fs = compile(GL_FRAGMENT_SHADER, FRAG_SRC, es);
  if (!vs || !fs) return 1;
  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glLinkProgram(prog);
  GLint linked = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &linked);
  if (!linked) {
    char log[2048] = {0};
    glGetProgramInfoLog(prog, sizeof(log) - 1, nullptr, log);
    fprintf(stderr, "link failed:\n%s\n", log);
    return 1;
  }
  glUseProgram(prog);
  glDeleteShader(vs);
  glDeleteShader(fs);

  const GLfloat quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
  GLuint vbo = 0;
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
  GLint aQuad = glGetAttribLocation(prog, "aQuad");
  glEnableVertexAttribArray((GLuint)aQuad);
  glVertexAttribPointer((GLuint)aQuad, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

  const GLint uCenterNdc = glGetUniformLocation(prog, "uCenterNdc");
  const GLint uHalfNdc = glGetUniformLocation(prog, "uHalfNdc");
  const GLint uCenterWorld = glGetUniformLocation(prog, "uCenterWorld");
  const GLint uHalfWorld = glGetUniformLocation(prog, "uHalfWorld");
  const GLint uRadius = glGetUniformLocation(prog, "uRadius");
  const GLint uShift = glGetUniformLocation(prog, "uShift");
  const GLint uRingScale = glGetUniformLocation(prog, "uRingScale");
  const GLint uScale = glGetUniformLocation(prog, "uScale");
  const GLint uPalette = glGetUniformLocation(prog, "uPalette");

  const int PW = 1024;
  std::vector<uint8_t> pal;
  buildPalette(pal, PW);
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, PW, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pal.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glUniform1i(uPalette, 0);

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  std::vector<Circle> circles;
  std::string raw;
  if (!readFile(coordsPath.c_str(), raw) || !parseCoords(raw, circles)) {
    fprintf(stderr, "could not read circles from %s\n", coordsPath.c_str());
    return 1;
  }
  deriveCharacter(circles, basePeriod, spread, ringJitter, phaseSpread);
  SDL_Log("%d circles from %s", (int)circles.size(), coordsPath.c_str());
  if (spread > 0.f || ringJitter > 0.f) {
    std::string list;
    float lo = 1e9f, hi = 0.f;
    for (const Circle &c : circles) {
      lo = std::min(lo, c.period);
      hi = std::max(hi, c.period);
      char b[32];
      snprintf(b, sizeof(b), "%s%d:%.1f", list.empty() ? "" : " ", c.id, c.period);
      list += b;
    }
    SDL_Log("periods %.1f-%.1f s, colour stride +-%.0f%%", lo, hi, ringJitter * 100.f);
    SDL_Log("per circle (id:seconds) %s", list.c_str());
  }

  // reload when the editor writes the file, so both can run at once
  long long coordsMtime = 0;
  auto mtimeOf = [&](const char *p) -> long long {
    struct stat st;
    return ::stat(p, &st) == 0 ? (long long)st.st_mtime : 0;
  };
  coordsMtime = mtimeOf(coordsPath.c_str());

  Governor gov(fpsCool, fpsHot, tWarm, tHot);
  const Uint64 perfFreq = SDL_GetPerformanceFrequency();
  const Uint64 startTick = SDL_GetPerformanceCounter();
  Uint64 lastFrame = startTick, lastBench = SDL_GetTicks64(), lastStat = lastBench;
  int frames = 0;
  double frameMsSum = 0;
  bool running = true;
  int drawW = 0, drawH = 0;

  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) running = false;
      else if (ev.type == SDL_KEYDOWN &&
               (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_q))
        running = false;
    }

    Uint64 nowMs = SDL_GetTicks64();
    gov.update(nowMs);

    if (nowMs - lastStat > 1000) {
      lastStat = nowMs;
      long long m = mtimeOf(coordsPath.c_str());
      if (m && m != coordsMtime) {
        std::string txt;
        std::vector<Circle> fresh;
        if (readFile(coordsPath.c_str(), txt) && parseCoords(txt, fresh)) {
          deriveCharacter(fresh, basePeriod, spread, ringJitter, phaseSpread);
          circles.swap(fresh);
          coordsMtime = m;
          SDL_Log("reloaded %d circles", (int)circles.size());
        }
      }
    }

    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(win, &w, &h);
    if (w != drawW || h != drawH) { drawW = w; drawH = h; glViewport(0, 0, w, h); }
    if (w <= 0 || h <= 0) { SDL_Delay(50); continue; }

    const float scale = std::min((float)w / VB_W, (float)h / VB_H);
    const float ox = ((float)w - VB_W * scale) * 0.5f;
    const float oy = ((float)h - VB_H * scale) * 0.5f;

    const Uint64 tick = SDL_GetPerformanceCounter();
    const float T = staticMode ? 0.f
                               : (float)((double)(tick - startTick) / (double)perfFreq) * speed;

    glClearColor(1.f, 1.f, 1.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUniform1f(uScale, scale);

    for (size_t i = 0; i < circles.size(); i++) {
      const Circle &c = circles[i];
      const float r = c.d * 0.5f;
      const float p = c.phase + T / c.period;
      glUniform1f(uShift, p - std::floor(p));
      glUniform1f(uRingScale, c.ringScale);

      const float margin = 1.5f / scale;          // room for the antialiased rim
      const float halfW = r + margin;
      const float px = ox + (c.cx - VB_X) * scale;
      const float py = oy + (c.cy - VB_Y) * scale;
      if (px + halfW * scale < 0 || px - halfW * scale > (float)w ||
          py + halfW * scale < 0 || py - halfW * scale > (float)h)
        continue;                                  // fully off-screen, skip it

      glUniform2f(uCenterNdc, 2.f * px / (float)w - 1.f, 1.f - 2.f * py / (float)h);
      glUniform2f(uHalfNdc, halfW * scale * 2.f / (float)w,
                  -halfW * scale * 2.f / (float)h);
      glUniform2f(uCenterWorld, c.cx, c.cy);
      glUniform1f(uHalfWorld, halfW);
      glUniform1f(uRadius, r);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    const Uint64 workEnd = SDL_GetPerformanceCounter();

    if (!shotPath.empty() && frames >= 1) {
      // read the frame back and save it, so the render can be checked without
      // anyone having to look at the screen
      std::vector<uint8_t> px((size_t)w * h * 4);
      glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
      SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
      if (surf) {
        for (int y = 0; y < h; y++)   // GL origin is bottom-left, BMP is top-down
          memcpy((uint8_t *)surf->pixels + (size_t)y * surf->pitch,
                 px.data() + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
        if (SDL_SaveBMP(surf, shotPath.c_str()) == 0)
          SDL_Log("wrote %s (%dx%d)", shotPath.c_str(), w, h);
        else
          SDL_Log("could not write %s: %s", shotPath.c_str(), SDL_GetError());
        SDL_FreeSurface(surf);
      }
      running = false;
    }

    SDL_GL_SwapWindow(win);

    const Uint64 done = SDL_GetPerformanceCounter();
    frameMsSum += 1000.0 * (double)(workEnd - tick) / (double)perfFreq;
    frames++;

    // pace to the governor's target; vsync already caps the top end
    const double budgetMs = 1000.0 / (double)gov.fps;
    const double spentMs = 1000.0 * (double)(done - lastFrame) / (double)perfFreq;
    if (spentMs < budgetMs) {
      double waitMs = budgetMs - spentMs;
      if (waitMs > 1.0) SDL_Delay((Uint32)(waitMs));
    }
    lastFrame = SDL_GetPerformanceCounter();

    if (bench && nowMs - lastBench >= 2000) {
      double secs = (double)(nowMs - lastBench) / 1000.0;
      // CPU time only: the GPU runs behind us, and timing across SwapWindow
      // would just report the vsync period rather than any real work
      printf("%.1f fps | %.2f ms cpu/frame | %.1f C | target %.0f fps\n",
             frames / secs, frameMsSum / std::max(1, frames), gov.tempC, gov.fps);
      fflush(stdout);
      frames = 0; frameMsSum = 0; lastBench = nowMs;
    }
  }

  SDL_GL_DeleteContext(ctx);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
