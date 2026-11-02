#pragma once

// wx-lite (android/native/WX_LITE.md): wxBitmap of wxWindows 2.4.2 (include/wx/msw/bitmap.h), for
// what the engine uses. A bitmap holds its pixels as the image it was made from (copies share
// them, as wxWindows' reference counting does); wx-lite's device contexts draw nothing
// (wx/dc.h). Loading a file goes through the image handlers (wx/image.h).

#include <memory>

#include "wx/gdicmn.h"
#include "wx/object.h"
#include "wx/string.h"

class wxImage;

class wxBitmap : public wxObject
{
public:
  // default ctor creates an invalid bitmap, you must Create() it later
  wxBitmap()
    : m_ok(false)
    , m_width(0)
    , m_height(0)
    , m_depth(0)
  {}
  wxBitmap(const wxBitmap&) = default;
  wxBitmap& operator=(const wxBitmap&) = default;

  // Initialize with XPM data or a resource: Android has neither
  wxBitmap(const wxString& name, long type = wxBITMAP_TYPE_BMP_RESOURCE);

  // New constructor for generalised creation from data
  wxBitmap(int width, int height, int depth = -1);

  // Convert from wxImage
  wxBitmap(const wxImage& image, int depth = -1);

  ~wxBitmap() override;

  wxImage ConvertToImage() const;

  bool Create(int width, int height, int depth = -1);

  /// Loads `name` through the image handler for `type`; false when none can (wx/image.h).
  bool LoadFile(const wxString& name, long type = wxBITMAP_TYPE_BMP_RESOURCE);

  bool Ok() const
  {
    return m_ok;
  }
  int GetWidth() const
  {
    return m_width;
  }
  int GetHeight() const
  {
    return m_height;
  }
  int GetDepth() const
  {
    return m_depth;
  }

  // the same bitmap: copies of one (wxWindows compares the reference data)
  bool operator==(const wxBitmap& bitmap) const
  {
    return m_ok == bitmap.m_ok && m_pixels == bitmap.m_pixels && m_width == bitmap.m_width
        && m_height == bitmap.m_height && m_depth == bitmap.m_depth;
  }
  bool operator!=(const wxBitmap& bitmap) const
  {
    return !(*this == bitmap);
  }

private:
  bool m_ok;
  int m_width;
  int m_height;
  int m_depth;
  std::shared_ptr<const wxImage> m_pixels;
};

extern wxBitmap wxNullBitmap;
