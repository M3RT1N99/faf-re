#pragma once

// wx-lite (android/native/WX_LITE.md): the window device contexts of wxWindows 2.4.2
// (include/wx/msw/dcclient.h), for what the engine uses. A wxPaintDC opens the window's paint as
// the MSW port does (BeginPaint, EndPaint when it goes): the window's update region is validated,
// so the window gets no more WM_PAINT until the next change.

#include "wx/dc.h"

class wxWindowDC : public wxDC
{
public:
  wxWindowDC();
  // Create a DC corresponding to the whole window
  wxWindowDC(wxWindow* win);
};

class wxClientDC : public wxWindowDC
{
public:
  wxClientDC();
  // Create a DC corresponding to the client area of the window
  wxClientDC(wxWindow* win);
  ~wxClientDC() override;
};

class wxPaintDC : public wxClientDC
{
public:
  wxPaintDC();
  // Create a DC corresponding for painting the window in OnPaint()
  wxPaintDC(wxWindow* win);
  ~wxPaintDC() override;

private:
  // the window being painted, between BeginPaint and EndPaint
  void* m_paintStruct;
};
