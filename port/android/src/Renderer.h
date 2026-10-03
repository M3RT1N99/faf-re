#pragma once

// Presentation for the bring-up through DiligentCore: Vulkan when the device
// has a working driver, OpenGL ES 3.x otherwise. It draws one fullscreen
// triangle per frame: an animated background with a progress bar while the
// data loads, then the main-menu background from the user's data, letterboxed
// to keep its aspect ratio.
//
// Android specifics handled here:
// - Vulkan swap chains are created with the surface's current transform
//   (pre-rotation, no extra composition pass); the vertex shader rotates the
//   picture by the inverse of that transform, so the content is upright in
//   both landscape orientations. OpenGL ES always presents identity and the
//   compositor rotates.
// - The window goes away whenever the activity is stopped. DetachWindow drops
//   the Vulkan swap chain (or suspends the EGL surface) before the system
//   destroys the window; AttachWindow builds it again for the new one. The
//   device and all GPU resources stay.
//
// Not thread-safe: every call comes from the android_main thread.

#include <android/native_window.h>

#include <memory>
#include <string>

#include "AndroidArgs.h"
#include "faf/port/Image.h"

namespace faf::android {

  struct FrameParams
  {
    float seconds = 0.0f;   ///< Time since start; drives the loading animation.
    float progress = -1.0f; ///< 0..1 draws the progress bar; negative hides it.
  };

  enum class FrameResult
  {
    Presented,
    Skipped, ///< Nothing to draw into right now (no surface, no image acquired); try again.
    Failed,
  };

  class Renderer
  {
  public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// Creates the device, the swap chain for `window` and the pipeline.
    /// When `preferred` is Vulkan and `allowFallback` is set, any Vulkan
    /// failure is logged and OpenGL ES is tried next. On failure nothing is
    /// left behind and `error` says what went wrong with each backend.
    bool Create(ANativeWindow* window, Backend preferred, bool allowFallback, std::string& error);

    [[nodiscard]] bool HasDevice() const;
    [[nodiscard]] bool HasSurface() const;
    [[nodiscard]] Backend ActiveBackend() const;
    /// "Vulkan 1.3, Mali-G78" and the like, for the log.
    [[nodiscard]] std::string Description() const;

    /// APP_CMD_INIT_WINDOW after the device exists.
    bool AttachWindow(ANativeWindow* window, std::string& error);
    /// APP_CMD_TERM_WINDOW: release everything that refers to the window.
    void DetachWindow();
    /// The window was resized or the configuration (orientation) changed.
    bool UpdateSurface(std::string& error);

    /// Uploads `image` (RGBA8, top row first) as the background. Images
    /// larger than the device's texture limit are halved until they fit.
    bool SetImage(const faf::port::Image& image, std::string& error);
    [[nodiscard]] bool HasImage() const;

    /// Draws and presents one frame.
    FrameResult Render(const FrameParams& params, std::string& error);

    /// Releases all GPU objects and the device.
    void Destroy();

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
  };

} // namespace faf::android
