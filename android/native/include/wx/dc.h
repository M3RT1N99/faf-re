#pragma once

// wx-lite (android/native/WX_LITE.md): wxDC of wxWindows 2.4.2 (include/wx/dc.h,
// include/wx/msw/dc.h), for what the engine uses. On Android the game's picture is the renderer's
// alone: a device context belongs to a window and knows its size, but draws nothing (no GDI).

#include "wx/bitmap.h"
#include "wx/brush.h"
#include "wx/gdicmn.h"
#include "wx/object.h"

class wxWindow;

class wxDC : public wxObject
{
public:
  wxDC();
  ~wxDC() override;

  bool Ok() const
  {
    return m_ok;
  }

  // the window the DC draws on, if any
  void SetWindow(wxWindow* win)
  {
    m_canvas = win;
  }
  wxWindow* GetWindow() const
  {
    return m_canvas;
  }

  // drawing: nothing reaches a screen here
  void DrawRectangle(wxCoord x, wxCoord y, wxCoord width, wxCoord height)
  {
    DoDrawRectangle(x, y, width, height);
  }
  void DrawRectangle(const wxPoint& pt, const wxSize& sz)
  {
    DoDrawRectangle(pt.x, pt.y, sz.x, sz.y);
  }
  void DrawRectangle(const wxRect& rect)
  {
    DoDrawRectangle(rect.x, rect.y, rect.width, rect.height);
  }
  void DrawBitmap(const wxBitmap& bmp, wxCoord x, wxCoord y, bool useMask = false)
  {
    DoDrawBitmap(bmp, x, y, useMask);
  }
  void DrawBitmap(const wxBitmap& bmp, const wxPoint& pt, bool useMask = false)
  {
    DoDrawBitmap(bmp, pt.x, pt.y, useMask);
  }
  virtual void Clear();

  // the brush the shapes are filled with
  virtual void SetBrush(const wxBrush& brush);
  virtual const wxBrush& GetBrush() const
  {
    return m_brush;
  }

  // the size of the window's client area (or the bitmap's, for a memory DC)
  void GetSize(int* width, int* height) const
  {
    DoGetSize(width, height);
  }
  wxSize GetSize() const
  {
    int w, h;
    DoGetSize(&w, &h);
    return wxSize(w, h);
  }

protected:
  virtual void DoDrawRectangle(wxCoord x, wxCoord y, wxCoord width, wxCoord height);
  virtual void DoDrawBitmap(const wxBitmap& bmp, wxCoord x, wxCoord y, bool useMask = false);
  virtual void DoGetSize(int* width, int* height) const;

  bool m_ok;
  wxWindow* m_canvas;
  wxBrush m_brush;
};
