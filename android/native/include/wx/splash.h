#pragma once

// wx-lite (android/native/WX_LITE.md): wxSplashScreen of wxWindows 2.4.2
// (include/wx/generic/splash.h), for what the engine uses: a frame that holds a bitmap window. The
// engine shows it without a timeout (no wxSPLASH_TIMEOUT), and deletes it itself; wx-lite has no
// timer for wxSPLASH_TIMEOUT (wxSplashScreen's constructor says it at the place).

#include "wx/bitmap.h"
#include "wx/frame.h"

#define wxSPLASH_CENTRE_ON_PARENT 0x01
#define wxSPLASH_CENTRE_ON_SCREEN 0x02
#define wxSPLASH_NO_CENTRE 0x00
#define wxSPLASH_TIMEOUT 0x04
#define wxSPLASH_NO_TIMEOUT 0x00

class wxSplashScreenWindow;

class wxSplashScreen : public wxFrame
{
public:
  // for RTTI macros only
  wxSplashScreen() {}
  wxSplashScreen(
    const wxBitmap& bitmap,
    long splashStyle,
    int milliseconds,
    wxWindow* parent,
    wxWindowID id,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxSIMPLE_BORDER | wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP
  );
  ~wxSplashScreen() override;

  void OnCloseWindow(wxCloseEvent& event);

  long GetSplashStyle() const
  {
    return m_splashStyle;
  }
  wxSplashScreenWindow* GetSplashWindow() const
  {
    return m_window;
  }
  int GetTimeout() const
  {
    return m_milliseconds;
  }

protected:
  wxSplashScreenWindow* m_window;
  long m_splashStyle;
  int m_milliseconds;

  DECLARE_EVENT_TABLE()
};

class wxSplashScreenWindow : public wxWindow
{
public:
  wxSplashScreenWindow(
    const wxBitmap& bitmap,
    wxWindow* parent,
    wxWindowID id,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxNO_BORDER
  );

  void OnPaint(wxPaintEvent& event);
  void OnEraseBackground(wxEraseEvent& event);

  void SetBitmap(const wxBitmap& bitmap)
  {
    m_bitmap = bitmap;
  }
  wxBitmap& GetBitmap()
  {
    return m_bitmap;
  }

protected:
  wxBitmap m_bitmap;

  DECLARE_EVENT_TABLE()
};
