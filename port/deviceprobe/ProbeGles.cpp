// The OpenGL ES sections of libfafdeviceprobe.so: the EGL/GLES report (versions, extensions, limits,
// compressed formats) and an offscreen render of the test pattern into an FBO, read back with
// glReadPixels into deviceprobe-gles.png. libEGL.so and libGLESv3.so are opened with dlopen, as the
// Vulkan loader is, so the executable has no NEEDED entry for either.

#include "Probe.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

namespace faf_probe
{
  namespace
  {
    std::string Hex(const unsigned long long v)
    {
      char text[32];
      snprintf(text, sizeof(text), "0x%04llx", v);
      return text;
    }

    // The entry points, looked up with dlsym (decltype only names the prototypes' types).
    struct Gl
    {
#define FAF_PROBE_EGL_FUNCTIONS(X)                                                                         \
  X(eglGetDisplay) X(eglInitialize) X(eglTerminate) X(eglQueryString) X(eglChooseConfig) X(eglGetConfigs)      \
  X(eglGetConfigAttrib) X(eglCreatePbufferSurface) X(eglDestroySurface) X(eglBindAPI) X(eglCreateContext)       \
  X(eglDestroyContext) X(eglMakeCurrent) X(eglGetError) X(eglReleaseThread)
#define FAF_PROBE_GL_FUNCTIONS(X)                                                                          \
  X(glGetString) X(glGetStringi) X(glGetIntegerv) X(glGetFloatv) X(glGetError) X(glCreateShader) X(glShaderSource) \
  X(glCompileShader) X(glGetShaderiv) X(glGetShaderInfoLog) X(glDeleteShader) X(glCreateProgram) X(glAttachShader) \
  X(glLinkProgram) X(glGetProgramiv) X(glGetProgramInfoLog) X(glDeleteProgram) X(glUseProgram) X(glGenBuffers)     \
  X(glBindBuffer) X(glBufferData) X(glDeleteBuffers) X(glGenVertexArrays) X(glBindVertexArray)                     \
  X(glDeleteVertexArrays) X(glEnableVertexAttribArray) X(glVertexAttribPointer) X(glGenFramebuffers)               \
  X(glBindFramebuffer) X(glDeleteFramebuffers) X(glGenRenderbuffers) X(glBindRenderbuffer)                         \
  X(glRenderbufferStorage) X(glFramebufferRenderbuffer) X(glDeleteRenderbuffers) X(glCheckFramebufferStatus)       \
  X(glViewport) X(glDisable) X(glClearColor) X(glClear) X(glDrawArrays) X(glFinish) X(glReadPixels)                 \
  X(glPixelStorei)
#define FAF_PROBE_DECLARE(name) decltype(&::name) name = nullptr;
      FAF_PROBE_EGL_FUNCTIONS(FAF_PROBE_DECLARE)
      FAF_PROBE_GL_FUNCTIONS(FAF_PROBE_DECLARE)
#undef FAF_PROBE_DECLARE

      void* egl = nullptr;
      void* gles = nullptr;
      std::string glesName;
      EGLDisplay display = EGL_NO_DISPLAY;
      EGLSurface surface = EGL_NO_SURFACE;
      EGLContext context = EGL_NO_CONTEXT;
      EGLConfig config = nullptr;
      EGLint eglMajor = 0;
      EGLint eglMinor = 0;
      int contextMajor = 0;
      int contextMinor = 0;
      int configCount = 0;
      double initMs = 0.0;
      double contextMs = 0.0;
      std::string step;
      std::string error;
      bool noLibrary = false;

      ~Gl()
      {
        if (display != EGL_NO_DISPLAY && eglMakeCurrent != nullptr) {
          // Queued in a forked section until its result is out (faf_probe::Teardown).
          faf_probe::Teardown([makeCurrent = eglMakeCurrent, destroyContext = eglDestroyContext,
                               destroySurface = eglDestroySurface, terminate = eglTerminate, display = display,
                               context = context, surface = surface] {
            makeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context != EGL_NO_CONTEXT && destroyContext != nullptr) destroyContext(display, context);
            if (surface != EGL_NO_SURFACE && destroySurface != nullptr) destroySurface(display, surface);
            if (terminate != nullptr) terminate(display);
          });
        }
      }

      std::string EglError() const
      {
        return eglGetError != nullptr ? Hex(static_cast<unsigned>(eglGetError())) : std::string("?");
      }

      // Loads the libraries, initialises EGL, creates a pbuffer and the highest GLES 3.x context the
      // driver gives, and makes it current. False with step/error set.
      bool Open()
      {
        step = "dlopen libEGL.so";
        egl = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
        if (egl == nullptr) {
          const char* why = dlerror();
          error = why != nullptr ? why : "dlopen failed";
          noLibrary = true;
          return false;
        }
        step = "dlopen libGLESv3.so";
        glesName = "libGLESv3.so";
        gles = dlopen("libGLESv3.so", RTLD_NOW | RTLD_LOCAL);
        if (gles == nullptr) {
          glesName = "libGLESv2.so";
          gles = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
        }
        if (gles == nullptr) {
          const char* why = dlerror();
          error = why != nullptr ? why : "dlopen failed";
          noLibrary = true;
          return false;
        }
#define FAF_PROBE_LOAD(lib, name)                                                  \
  name = reinterpret_cast<decltype(name)>(dlsym(lib, #name));                      \
  if (name == nullptr) {                                                           \
    step = "dlsym";                                                                \
    error = std::string("no ") + #name;                                            \
    return false;                                                                  \
  }
#define FAF_PROBE_LOAD_EGL(name) FAF_PROBE_LOAD(egl, name)
#define FAF_PROBE_LOAD_GL(name) FAF_PROBE_LOAD(gles, name)
        FAF_PROBE_EGL_FUNCTIONS(FAF_PROBE_LOAD_EGL)
        FAF_PROBE_GL_FUNCTIONS(FAF_PROBE_LOAD_GL)
#undef FAF_PROBE_LOAD_GL
#undef FAF_PROBE_LOAD_EGL
#undef FAF_PROBE_LOAD

        double t0 = NowMs();
        step = "eglGetDisplay";
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display == EGL_NO_DISPLAY) {
          error = "EGL_NO_DISPLAY, error " + EglError();
          return false;
        }
        step = "eglInitialize";
        if (eglInitialize(display, &eglMajor, &eglMinor) != EGL_TRUE) {
          error = "error " + EglError();
          display = EGL_NO_DISPLAY;
          return false;
        }
        initMs = NowMs() - t0;
        EGLint total = 0;
        eglGetConfigs(display, nullptr, 0, &total);
        configCount = total;

        step = "eglChooseConfig";
        const EGLint withDepth[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                    EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE};
        const EGLint plain[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
        EGLint found = 0;
        if (eglChooseConfig(display, withDepth, &config, 1, &found) != EGL_TRUE || found == 0) {
          if (eglChooseConfig(display, plain, &config, 1, &found) != EGL_TRUE || found == 0) {
            error = "no GLES 3 pbuffer config (error " + EglError() + ")";
            return false;
          }
        }
        step = "eglCreatePbufferSurface";
        const EGLint size[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
        surface = eglCreatePbufferSurface(display, config, size);
        if (surface == EGL_NO_SURFACE) {
          error = "error " + EglError();
          return false;
        }
        step = "eglCreateContext";
        eglBindAPI(EGL_OPENGL_ES_API);
        t0 = NowMs();
        const int minors[] = {2, 1, 0};
        for (const int minor : minors) {
          const EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, minor, EGL_NONE};
          context = eglCreateContext(display, config, EGL_NO_CONTEXT, attributes);
          if (context != EGL_NO_CONTEXT) {
            contextMajor = 3;
            contextMinor = minor;
            break;
          }
        }
        if (context == EGL_NO_CONTEXT) {
          const EGLint attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
          context = eglCreateContext(display, config, EGL_NO_CONTEXT, attributes);
          contextMajor = 3;
          contextMinor = -1;
        }
        if (context == EGL_NO_CONTEXT) {
          error = "no GLES 3 context (error " + EglError() + ")";
          return false;
        }
        step = "eglMakeCurrent";
        if (eglMakeCurrent(display, surface, surface, context) != EGL_TRUE) {
          error = "error " + EglError();
          return false;
        }
        contextMs = NowMs() - t0;
        return true;
      }

      std::string String(GLenum name) const
      {
        const GLubyte* s = glGetString(name);
        return s != nullptr ? reinterpret_cast<const char*>(s) : std::string();
      }

      std::vector<std::string> Extensions() const
      {
        std::vector<std::string> names;
        GLint count = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &count);
        for (GLint i = 0; i < count; ++i) {
          const GLubyte* s = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i));
          if (s != nullptr) {
            names.emplace_back(reinterpret_cast<const char*>(s));
          }
        }
        std::sort(names.begin(), names.end());
        return names;
      }

      GLint Int(GLenum name) const
      {
        GLint v = -1;
        glGetError();
        glGetIntegerv(name, &v);
        return glGetError() == GL_NO_ERROR ? v : -1;
      }
    };

    bool Has(const std::vector<std::string>& list, const char* name)
    {
      return std::find(list.begin(), list.end(), std::string(name)) != list.end();
    }

    // Extensions the D3D9 -> GLES mapping depends on (docs/port/renderer.md, the critic's R4 and Q2).
    const char* const kNotable[] = {
      "GL_EXT_texture_compression_s3tc",
      "GL_EXT_texture_compression_dxt1",
      "GL_ANGLE_texture_compression_dxt3",
      "GL_ANGLE_texture_compression_dxt5",
      "GL_EXT_texture_compression_s3tc_srgb",
      "GL_EXT_texture_compression_rgtc",
      "GL_EXT_texture_compression_bptc",
      "GL_KHR_texture_compression_astc_ldr",
      "GL_KHR_texture_compression_astc_hdr",
      "GL_OES_texture_compression_astc",
      "GL_EXT_clip_control",
      "GL_EXT_color_buffer_float",
      "GL_EXT_color_buffer_half_float",
      "GL_EXT_texture_border_clamp",
      "GL_OES_texture_border_clamp",
      "GL_EXT_texture_filter_anisotropic",
      "GL_EXT_depth_clamp",
      "GL_EXT_polygon_offset_clamp",
      "GL_NV_polygon_mode",
      "GL_ANGLE_polygon_mode",
      "GL_EXT_disjoint_timer_query",
      "GL_OES_get_program_binary",
      "GL_EXT_buffer_storage",
      "GL_EXT_shader_framebuffer_fetch",
      "GL_ARM_shader_framebuffer_fetch",
      "GL_EXT_draw_buffers_indexed",
      "GL_OES_draw_buffers_indexed",
      "GL_EXT_blend_func_extended",
      "GL_OES_texture_float_linear",
      "GL_EXT_texture_format_BGRA8888",
      "GL_EXT_read_format_bgra",
      "GL_OES_depth24",
      "GL_OES_packed_depth_stencil",
      "GL_EXT_multisampled_render_to_texture",
      "GL_OVR_multiview2",
    };

    const char* const kEglNotable[] = {
      "EGL_KHR_create_context", "EGL_KHR_surfaceless_context", "EGL_KHR_no_config_context",
      "EGL_ANDROID_get_native_client_buffer", "EGL_ANDROID_image_native_buffer", "EGL_KHR_image_base",
      "EGL_ANDROID_blob_cache", "EGL_KHR_fence_sync", "EGL_ANDROID_native_fence_sync", "EGL_EXT_pixel_format_float",
      "EGL_KHR_gl_colorspace", "EGL_ANDROID_recordable", "EGL_ANDROID_presentation_time",
    };

    std::vector<std::string> Split(const std::string& text)
    {
      std::vector<std::string> out;
      size_t i = 0;
      while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') ++i;
        size_t j = i;
        while (j < text.size() && text[j] != ' ') ++j;
        if (j > i) out.push_back(text.substr(i, j - i));
        i = j;
      }
      std::sort(out.begin(), out.end());
      return out;
    }

    constexpr char kVertexSource[] = R"(#version 300 es
layout(location = 0) in vec2 corner;
void main()
{
    gl_Position = vec4(corner, 0.0, 1.0);
}
)";

    constexpr char kFragmentSource[] = R"(#version 300 es
precision highp float;
precision highp int;
layout(location = 0) out vec4 outColor;
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    float checker = float(((p.x >> 5) ^ (p.y >> 5)) & 1);
    outColor = vec4(float(p.x & 255) / 255.0, float(p.y & 255) / 255.0, checker, 1.0);
}
)";

    SectionResult OpenFailure(const Gl& gl, const char* what)
    {
      SectionResult result;
      Json j;
      j.Str("status", gl.noLibrary ? "unsupported" : "error");
      j.Str("step", gl.step);
      j.Str("error", gl.error);
      if (gl.eglMajor > 0) {
        j.Str("egl_version", std::to_string(gl.eglMajor) + "." + std::to_string(gl.eglMinor));
      }
      result.json = j.Text();
      result.summary = std::string(what) + " failed at " + gl.step + ": " + gl.error;
      return result;
    }
  } // namespace

  SectionResult RunGlesReport(const Options& /*options*/)
  {
    const double t0 = NowMs();
    Gl gl;
    if (!gl.Open()) {
      return OpenFailure(gl, "OpenGL ES");
    }
    SectionResult result;
    Json j;
    j.Str("status", "ok");
    Json egl;
    egl.Str("version", std::to_string(gl.eglMajor) + "." + std::to_string(gl.eglMinor));
    const char* vendor = gl.eglQueryString(gl.display, EGL_VENDOR);
    const char* version = gl.eglQueryString(gl.display, EGL_VERSION);
    const char* apis = gl.eglQueryString(gl.display, EGL_CLIENT_APIS);
    const char* extensions = gl.eglQueryString(gl.display, EGL_EXTENSIONS);
    egl.Str("vendor", vendor != nullptr ? vendor : "");
    egl.Str("version_string", version != nullptr ? version : "");
    egl.Str("client_apis", apis != nullptr ? apis : "");
    egl.Num("config_count", gl.configCount);
    const std::vector<std::string> eglExtensions = Split(extensions != nullptr ? extensions : "");
    Json eglNotable;
    for (const char* name : kEglNotable) {
      eglNotable.Bool(name, Has(eglExtensions, name));
    }
    egl.Raw("notable_extensions", eglNotable.Text());
    Json eglAll('[');
    for (const std::string& e : eglExtensions) {
      eglAll.AddStr(e);
    }
    egl.Raw("extensions", eglAll.Text());
    egl.Real("initialize_ms", gl.initMs, 2);
    j.Raw("egl", egl.Text());

    j.Str("context", std::to_string(gl.contextMajor) + "." + (gl.contextMinor >= 0 ? std::to_string(gl.contextMinor) : "x"));
    j.Real("create_context_ms", gl.contextMs, 2);
    const std::string glVendor = gl.String(GL_VENDOR);
    const std::string renderer = gl.String(GL_RENDERER);
    const std::string glVersion = gl.String(GL_VERSION);
    j.Str("vendor", glVendor);
    j.Str("renderer", renderer);
    j.Str("version", glVersion);
    j.Str("glsl_version", gl.String(GL_SHADING_LANGUAGE_VERSION));
    const std::vector<std::string> ext = gl.Extensions();

    const bool s3tc = Has(ext, "GL_EXT_texture_compression_s3tc") ||
                      (Has(ext, "GL_EXT_texture_compression_dxt1") && Has(ext, "GL_ANGLE_texture_compression_dxt3") &&
                       Has(ext, "GL_ANGLE_texture_compression_dxt5"));
    const bool astc = Has(ext, "GL_KHR_texture_compression_astc_ldr") || Has(ext, "GL_OES_texture_compression_astc");
    const bool clip = Has(ext, "GL_EXT_clip_control");
    const bool cbf = Has(ext, "GL_EXT_color_buffer_float");
    const bool border = Has(ext, "GL_EXT_texture_border_clamp") || Has(ext, "GL_OES_texture_border_clamp") ||
                        (gl.contextMajor == 3 && gl.contextMinor >= 2);
    const bool aniso = Has(ext, "GL_EXT_texture_filter_anisotropic");
    const bool wire = Has(ext, "GL_NV_polygon_mode") || Has(ext, "GL_ANGLE_polygon_mode");
    const GLint programBinaries = gl.Int(GL_NUM_PROGRAM_BINARY_FORMATS);
    Json answers;
    answers.Bool("s3tc_dxt1_5", s3tc);
    answers.Bool("etc2", true);  // core in OpenGL ES 3.0
    answers.Bool("astc_ldr", astc);
    answers.Bool("clip_control", clip);
    answers.Bool("color_buffer_float", cbf);
    answers.Bool("color_buffer_half_float", Has(ext, "GL_EXT_color_buffer_half_float"));
    answers.Bool("border_clamp", border);
    answers.Bool("anisotropic", aniso);
    answers.Bool("polygon_mode_line", wire);
    answers.Bool("depth_clamp", Has(ext, "GL_EXT_depth_clamp"));
    answers.Bool("timer_query", Has(ext, "GL_EXT_disjoint_timer_query"));
    answers.Num("program_binary_formats", programBinaries);
    answers.Num("max_texture_size", gl.Int(GL_MAX_TEXTURE_SIZE));
    j.Raw("answers", answers.Text());

    Json limits;
    const struct { const char* name; GLenum e; } ints[] = {
      {"GL_MAX_TEXTURE_SIZE", GL_MAX_TEXTURE_SIZE},
      {"GL_MAX_CUBE_MAP_TEXTURE_SIZE", GL_MAX_CUBE_MAP_TEXTURE_SIZE},
      {"GL_MAX_ARRAY_TEXTURE_LAYERS", GL_MAX_ARRAY_TEXTURE_LAYERS},
      {"GL_MAX_3D_TEXTURE_SIZE", GL_MAX_3D_TEXTURE_SIZE},
      {"GL_MAX_RENDERBUFFER_SIZE", GL_MAX_RENDERBUFFER_SIZE},
      {"GL_MAX_SAMPLES", GL_MAX_SAMPLES},
      {"GL_MAX_COLOR_ATTACHMENTS", GL_MAX_COLOR_ATTACHMENTS},
      {"GL_MAX_DRAW_BUFFERS", GL_MAX_DRAW_BUFFERS},
      {"GL_MAX_VERTEX_ATTRIBS", GL_MAX_VERTEX_ATTRIBS},
      {"GL_MAX_TEXTURE_IMAGE_UNITS", GL_MAX_TEXTURE_IMAGE_UNITS},
      {"GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS", GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS},
      {"GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS", GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS},
      {"GL_MAX_VERTEX_UNIFORM_VECTORS", GL_MAX_VERTEX_UNIFORM_VECTORS},
      {"GL_MAX_FRAGMENT_UNIFORM_VECTORS", GL_MAX_FRAGMENT_UNIFORM_VECTORS},
      {"GL_MAX_UNIFORM_BLOCK_SIZE", GL_MAX_UNIFORM_BLOCK_SIZE},
      {"GL_MAX_VARYING_VECTORS", GL_MAX_VARYING_VECTORS},
      {"GL_NUM_PROGRAM_BINARY_FORMATS", GL_NUM_PROGRAM_BINARY_FORMATS},
      {"GL_NUM_SHADER_BINARY_FORMATS", GL_NUM_SHADER_BINARY_FORMATS},
      {"GL_NUM_COMPRESSED_TEXTURE_FORMATS", GL_NUM_COMPRESSED_TEXTURE_FORMATS},
    };
    for (const auto& i : ints) {
      limits.Num(i.name, gl.Int(i.e));
    }
    if (aniso) {
      GLfloat maxAniso = 0.0f;
      gl.glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
      limits.Real("GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT", maxAniso, 1);
    }
    j.Raw("limits", limits.Text());

    const GLint compressedCount = gl.Int(GL_NUM_COMPRESSED_TEXTURE_FORMATS);
    Json compressed('[');
    if (compressedCount > 0 && compressedCount < 4096) {
      std::vector<GLint> formats(static_cast<size_t>(compressedCount));
      gl.glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, formats.data());
      for (const GLint f : formats) {
        compressed.AddStr(Hex(static_cast<unsigned>(f)));
      }
    }
    j.Raw("compressed_texture_formats", compressed.Text());

    Json notable;
    for (const char* name : kNotable) {
      notable.Bool(name, Has(ext, name));
    }
    j.Raw("notable_extensions", notable.Text());
    j.Num("extension_count", static_cast<long long>(ext.size()));
    Json all('[');
    for (const std::string& e : ext) {
      all.AddStr(e);
    }
    j.Raw("extensions", all.Text());
    j.Real("ms", NowMs() - t0, 1);
    result.json = j.Text();

    auto yn = [](bool v) { return v ? "yes" : "no"; };
    char line[768];
    snprintf(line, sizeof(line),
             "OpenGL ES %d.%s on %s (%s, %s): S3TC %s, ETC2 yes, ASTC %s, clip_control %s, color_buffer_float %s, "
             "border clamp %s, anisotropic %s, polygon mode %s, program binary formats %d, max texture %d",
             gl.contextMajor, gl.contextMinor >= 0 ? std::to_string(gl.contextMinor).c_str() : "x", renderer.c_str(),
             glVendor.c_str(), glVersion.c_str(), yn(s3tc), yn(astc), yn(clip), yn(cbf), yn(border), yn(aniso), yn(wire),
             programBinaries, gl.Int(GL_MAX_TEXTURE_SIZE));
    result.summary = line;
    return result;
  }

  SectionResult RunGlesRender(const Options& options)
  {
    const double t0 = NowMs();
    Gl gl;
    if (!gl.Open()) {
      return OpenFailure(gl, "OpenGL ES render");
    }
    SectionResult result;
    Json j;
    std::string step;
    std::string error;
    double compileMs = -1.0;
    double linkMs = -1.0;
    double drawMs = -1.0;
    double readMs = -1.0;
    GLint binaryLength = -1;
    PatternCheck pattern;
    std::string png;
    std::string pngError;
    std::string pixels;
    GLuint vs = 0, fs = 0, program = 0, vbo = 0, vao = 0, fbo = 0, rbo = 0;

    const bool ok = [&]() -> bool {
      auto compile = [&](GLenum type, const char* source, GLuint& shader, const char* what) {
        step = what;
        shader = gl.glCreateShader(type);
        gl.glShaderSource(shader, 1, &source, nullptr);
        gl.glCompileShader(shader);
        GLint status = 0;
        gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (status != GL_TRUE) {
          char log[1024] = {};
          gl.glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
          error = log;
          return false;
        }
        return true;
      };
      double t = NowMs();
      if (!compile(GL_VERTEX_SHADER, kVertexSource, vs, "compile vertex shader")) return false;
      if (!compile(GL_FRAGMENT_SHADER, kFragmentSource, fs, "compile fragment shader")) return false;
      compileMs = NowMs() - t;
      step = "link";
      program = gl.glCreateProgram();
      gl.glAttachShader(program, vs);
      gl.glAttachShader(program, fs);
      t = NowMs();
      gl.glLinkProgram(program);
      GLint linked = 0;
      gl.glGetProgramiv(program, GL_LINK_STATUS, &linked);
      linkMs = NowMs() - t;
      if (linked != GL_TRUE) {
        char log[1024] = {};
        gl.glGetProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
        error = log;
        return false;
      }
      gl.glGetError();
      gl.glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
      if (gl.glGetError() != GL_NO_ERROR) {
        binaryLength = -1;
      }

      step = "framebuffer";
      gl.glGenRenderbuffers(1, &rbo);
      gl.glBindRenderbuffer(GL_RENDERBUFFER, rbo);
      gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, kPatternSize, kPatternSize);
      gl.glGenFramebuffers(1, &fbo);
      gl.glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);
      const GLenum fbStatus = gl.glCheckFramebufferStatus(GL_FRAMEBUFFER);
      if (fbStatus != GL_FRAMEBUFFER_COMPLETE) {
        error = "incomplete: " + Hex(fbStatus);
        return false;
      }

      step = "draw";
      // A vertex buffer, not gl_VertexID: some GLES implementations drop a draw without an enabled
      // attribute (port/android/src/Renderer.cpp found that on the emulator).
      const GLfloat corners[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
      gl.glGenVertexArrays(1, &vao);
      gl.glBindVertexArray(vao);
      gl.glGenBuffers(1, &vbo);
      gl.glBindBuffer(GL_ARRAY_BUFFER, vbo);
      gl.glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
      gl.glEnableVertexAttribArray(0);
      gl.glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
      gl.glViewport(0, 0, kPatternSize, kPatternSize);
      gl.glDisable(GL_BLEND);
      gl.glDisable(GL_DEPTH_TEST);
      gl.glDisable(GL_CULL_FACE);
      gl.glDisable(GL_DITHER);
      gl.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      gl.glClear(GL_COLOR_BUFFER_BIT);
      gl.glUseProgram(program);
      t = NowMs();
      gl.glDrawArrays(GL_TRIANGLES, 0, 3);
      gl.glFinish();
      drawMs = NowMs() - t;
      const GLenum drawError = gl.glGetError();
      if (drawError != GL_NO_ERROR) {
        error = "GL error " + Hex(drawError);
        return false;
      }

      step = "glReadPixels";
      std::vector<uint8_t> rows(static_cast<size_t>(kPatternSize) * kPatternSize * 4);
      gl.glPixelStorei(GL_PACK_ALIGNMENT, 4);
      t = NowMs();
      gl.glReadPixels(0, 0, kPatternSize, kPatternSize, GL_RGBA, GL_UNSIGNED_BYTE, rows.data());
      readMs = NowMs() - t;
      const GLenum readError = gl.glGetError();
      if (readError != GL_NO_ERROR) {
        error = "GL error " + Hex(readError);
        return false;
      }
      const size_t pitch = static_cast<size_t>(kPatternSize) * 4;
      // glReadPixels' row 0 is window y = 0 (the bottom), the pattern's y = 0.
      pattern = CheckPattern(rows.data(), pitch);
      Json samples;
      const int points[][2] = {{0, 0}, {255, 0}, {0, 255}, {255, 255}, {128, 64}};
      for (const auto& pt : points) {
        const uint8_t* px = rows.data() + static_cast<size_t>(pt[1]) * pitch + static_cast<size_t>(pt[0]) * 4;
        char key[16];
        snprintf(key, sizeof(key), "%d,%d", pt[0], pt[1]);
        char value[48];
        snprintf(value, sizeof(value), "[%u,%u,%u,%u]", px[0], px[1], px[2], px[3]);
        samples.Raw(key, value);
      }
      pixels = samples.Text();
      // Rows in readback order (window y = 0 first), as the Vulkan PNG: the two files look the same
      // when both APIs are right.
      png = "deviceprobe-gles.png";
      if (!WritePng(options.outDir + "/" + png, kPatternSize, kPatternSize, rows.data(), pitch, false, pngError)) {
        png.clear();
      }
      step = "done";
      return true;
    }();

    if (program != 0) gl.glDeleteProgram(program);
    if (vs != 0) gl.glDeleteShader(vs);
    if (fs != 0) gl.glDeleteShader(fs);
    if (vbo != 0) gl.glDeleteBuffers(1, &vbo);
    if (vao != 0) gl.glDeleteVertexArrays(1, &vao);
    if (fbo != 0) gl.glDeleteFramebuffers(1, &fbo);
    if (rbo != 0) gl.glDeleteRenderbuffers(1, &rbo);

    const std::string renderer = gl.String(GL_RENDERER);
    j.Str("status", ok ? "ok" : "error");
    if (!ok) {
      j.Str("step", step);
      j.Str("error", error);
    }
    j.Str("renderer", renderer);
    j.Str("context", std::to_string(gl.contextMajor) + "." + (gl.contextMinor >= 0 ? std::to_string(gl.contextMinor) : "x"));
    j.Real("create_context_ms", gl.contextMs, 2);
    j.Real("compile_ms", compileMs, 2);
    j.Real("link_ms", linkMs, 2);
    j.Num("program_binary_length", binaryLength);
    j.Real("draw_finish_ms", drawMs, 2);
    j.Real("read_pixels_ms", readMs, 2);
    if (ok) {
      j.Str("pattern", pattern.verdict);
      j.Num("mismatched_pixels", pattern.mismatched);
      j.Num("max_channel_error", pattern.maxError);
      if (pattern.mismatched > 0) {
        j.Raw("first_mismatch", "[" + std::to_string(pattern.firstX) + "," + std::to_string(pattern.firstY) + "]");
      }
      j.Raw("pixels", pixels);
      if (!png.empty()) {
        j.Str("png", png);
      } else {
        j.Null("png");
        j.Str("png_error", pngError);
      }
    }
    j.Real("ms", NowMs() - t0, 1);
    result.json = j.Text();
    char line[512];
    if (ok) {
      snprintf(line, sizeof(line),
               "OpenGL ES render on %s: pattern %s (%d of %d pixels off), compile %.1f ms, link %.1f ms, draw %.1f ms%s%s",
               renderer.c_str(), pattern.verdict.c_str(), pattern.mismatched, kPatternSize * kPatternSize, compileMs, linkMs,
               drawMs, png.empty() ? ", no PNG: " : ", ", png.empty() ? pngError.c_str() : png.c_str());
    } else {
      snprintf(line, sizeof(line), "OpenGL ES render failed at %s: %s", step.c_str(), error.c_str());
    }
    result.summary = line;
    return result;
  }
} // namespace faf_probe
