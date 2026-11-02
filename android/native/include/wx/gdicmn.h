#pragma once

// wx-lite (android/native/WX_LITE.md): wxWindows 2.4.2's <wx/gdicmn.h>, for what the engine uses:
// the bitmap types, the geometry types and their defaults, the stock objects.

#include "wx/defs.h"

class wxBrush;

enum wxBitmapType
{
  wxBITMAP_TYPE_INVALID,
  wxBITMAP_TYPE_BMP,
  wxBITMAP_TYPE_BMP_RESOURCE,
  wxBITMAP_TYPE_RESOURCE = wxBITMAP_TYPE_BMP_RESOURCE,
  wxBITMAP_TYPE_ICO,
  wxBITMAP_TYPE_ICO_RESOURCE,
  wxBITMAP_TYPE_CUR,
  wxBITMAP_TYPE_CUR_RESOURCE,
  wxBITMAP_TYPE_XBM,
  wxBITMAP_TYPE_XBM_DATA,
  wxBITMAP_TYPE_XPM,
  wxBITMAP_TYPE_XPM_DATA,
  wxBITMAP_TYPE_TIF,
  wxBITMAP_TYPE_TIF_RESOURCE,
  wxBITMAP_TYPE_GIF,
  wxBITMAP_TYPE_GIF_RESOURCE,
  wxBITMAP_TYPE_PNG,
  wxBITMAP_TYPE_PNG_RESOURCE,
  wxBITMAP_TYPE_JPEG,
  wxBITMAP_TYPE_JPEG_RESOURCE,
  wxBITMAP_TYPE_PNM,
  wxBITMAP_TYPE_PNM_RESOURCE,
  wxBITMAP_TYPE_PCX,
  wxBITMAP_TYPE_PCX_RESOURCE,
  wxBITMAP_TYPE_PICT,
  wxBITMAP_TYPE_PICT_RESOURCE,
  wxBITMAP_TYPE_ICON,
  wxBITMAP_TYPE_ICON_RESOURCE,
  wxBITMAP_TYPE_ANI,
  wxBITMAP_TYPE_IFF,
  wxBITMAP_TYPE_MACCURSOR,
  wxBITMAP_TYPE_MACCURSOR_RESOURCE,
  wxBITMAP_TYPE_ANY = 50
};

class wxSize
{
public:
  int x;
  int y;

  wxSize()
    : x(0)
    , y(0)
  {}
  wxSize(int xx, int yy)
    : x(xx)
    , y(yy)
  {}

  bool operator==(const wxSize& sz) const
  {
    return x == sz.x && y == sz.y;
  }
  bool operator!=(const wxSize& sz) const
  {
    return x != sz.x || y != sz.y;
  }
  wxSize operator+(const wxSize& sz) const
  {
    return wxSize(x + sz.x, y + sz.y);
  }
  wxSize operator-(const wxSize& sz) const
  {
    return wxSize(x - sz.x, y - sz.y);
  }

  void Set(int xx, int yy)
  {
    x = xx;
    y = yy;
  }
  void SetWidth(int w)
  {
    x = w;
  }
  void SetHeight(int h)
  {
    y = h;
  }
  int GetWidth() const
  {
    return x;
  }
  int GetHeight() const
  {
    return y;
  }
  int GetX() const
  {
    return x;
  }
  int GetY() const
  {
    return y;
  }
};

class wxPoint
{
public:
  int x;
  int y;

  wxPoint()
    : x(0)
    , y(0)
  {}
  wxPoint(int xx, int yy)
    : x(xx)
    , y(yy)
  {}

  bool operator==(const wxPoint& p) const
  {
    return x == p.x && y == p.y;
  }
  bool operator!=(const wxPoint& p) const
  {
    return !(*this == p);
  }
  wxPoint operator+(const wxPoint& p) const
  {
    return wxPoint(x + p.x, y + p.y);
  }
  wxPoint operator-(const wxPoint& p) const
  {
    return wxPoint(x - p.x, y - p.y);
  }
  wxPoint& operator+=(const wxPoint& p)
  {
    x += p.x;
    y += p.y;
    return *this;
  }
  wxPoint& operator-=(const wxPoint& p)
  {
    x -= p.x;
    y -= p.y;
    return *this;
  }
};

class wxRect
{
public:
  int x;
  int y;
  int width;
  int height;

  wxRect()
    : x(0)
    , y(0)
    , width(0)
    , height(0)
  {}
  wxRect(int xx, int yy, int ww, int hh)
    : x(xx)
    , y(yy)
    , width(ww)
    , height(hh)
  {}
  wxRect(const wxPoint& topLeft, const wxPoint& bottomRight);
  wxRect(const wxPoint& pos, const wxSize& size)
    : x(pos.x)
    , y(pos.y)
    , width(size.x)
    , height(size.y)
  {}

  int GetX() const
  {
    return x;
  }
  void SetX(int xx)
  {
    x = xx;
  }
  int GetY() const
  {
    return y;
  }
  void SetY(int yy)
  {
    y = yy;
  }
  int GetWidth() const
  {
    return width;
  }
  void SetWidth(int w)
  {
    width = w;
  }
  int GetHeight() const
  {
    return height;
  }
  void SetHeight(int h)
  {
    height = h;
  }
  wxPoint GetPosition() const
  {
    return wxPoint(x, y);
  }
  void SetPosition(const wxPoint& p)
  {
    x = p.x;
    y = p.y;
  }
  wxSize GetSize() const
  {
    return wxSize(width, height);
  }
  void SetSize(const wxSize& s)
  {
    width = s.GetWidth();
    height = s.GetHeight();
  }
  int GetLeft() const
  {
    return x;
  }
  int GetTop() const
  {
    return y;
  }
  int GetBottom() const
  {
    return y + height - 1;
  }
  int GetRight() const
  {
    return x + width - 1;
  }

  wxRect& Inflate(wxCoord dx, wxCoord dy);
  wxRect& Intersect(const wxRect& rect);
  bool operator==(const wxRect& rect) const;
  bool operator!=(const wxRect& rect) const
  {
    return !(*this == rect);
  }
  bool Inside(int cx, int cy) const;
  bool Inside(const wxPoint& pt) const
  {
    return Inside(pt.x, pt.y);
  }
  bool Intersects(const wxRect& rect) const;
};

extern const wxSize wxDefaultSize;
extern const wxPoint wxDefaultPosition;

// The stock objects (wxApp::Initialize makes them, wxApp::CleanUp deletes them).
extern wxBrush* wxBLACK_BRUSH;
extern wxBrush* wxWHITE_BRUSH;
extern wxBrush* wxTRANSPARENT_BRUSH;
extern wxBrush wxNullBrush;

/// The pointer's position on the screen, as GetCursorPos has it (include/win32: on Android, the
/// screen is the activity's window).
wxPoint wxGetMousePosition();
void wxGetMousePosition(int* x, int* y);
