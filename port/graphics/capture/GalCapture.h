#pragma once

// The M6a step-0 frame harness (docs/port/renderer.md): renders the main menu with a fixed
// frame delta, a virtual clock and a fixed random seed in a window that is never shown, and
// captures chosen frames through gal right after EndScene. Port-only: it is compiled into
// main.exe only with the FafPortGraphics MSBuild option (FAF_PORT_GRAPHICS), and it does
// nothing unless `/galharness <dir>` is on the command line. README.md in this directory has
// the options, the output and what each pin is for.
//
// The engine calls exactly two of these, both inside `#if defined(FAF_PORT_GRAPHICS)`:
//   - CScApp::Main (moho/app/CScApp.cpp), next to the CD3DDevice::Refresh that normally
//     queues the WM_PAINT: HarnessFrame.
//   - WRenViewport::Render (moho/app/WxRuntimeTypes.cpp), right after device->EndScene():
//     HarnessSceneEnded.

namespace port::graphics::capture
{
  /**
   * True when this process was started with `/galharness <dir>`. Decided once, during static
   * initialisation (HarnessSandbox.cpp), before WinMain runs.
   */
  [[nodiscard]] bool HarnessActive() noexcept;

  /**
   * Called once per CScApp::Main, where the engine asks its viewport to repaint. A hidden
   * window gets no WM_PAINT, so the harness posts its own message instead; wx dispatches it at
   * the point of the loop where the WM_PAINT would have been dispatched, and its handler calls
   * CD3DDevice::Paint (Present, then WRenViewport::Render). `frameSeconds` is Main's delta (the
   * `/framerate` value from the second frame on); it advances the harness clock that the Lua
   * time functions read. No-op without `/galharness`.
   */
  void HarnessFrame(float frameSeconds);

  /**
   * Called by WRenViewport::Render right after EndScene, with the head it rendered. Captures
   * the head's back buffer through gal (GetHeadOutputContext, GetRenderTargetData into a
   * system-memory texture, Lock) when the frame being painted is one of `/galframes`. No-op
   * without `/galharness`.
   */
  void HarnessSceneEnded(int head);
} // namespace port::graphics::capture
