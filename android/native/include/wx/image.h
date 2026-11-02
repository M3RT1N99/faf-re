#pragma once

// wx-lite (android/native/WX_LITE.md): wxImage and wxImageHandler of wxWindows 2.4.2
// (include/wx/image.h), for what the engine uses. An image is 24-bit RGB data. Loading goes to the
// handler registered for the type (AddHandler), as in wxWindows; wx-lite's handlers do not decode
// (wx/imagpng.h), so a load fails.

#include <vector>

#include "wx/gdicmn.h"
#include "wx/object.h"
#include "wx/string.h"

class wxImage;

class wxImageHandler : public wxObject
{
public:
  wxImageHandler()
    : m_type(0)
  {}

  /// Fills `image` from the file `name`; false when the handler cannot.
  virtual bool LoadFile(wxImage* image, const wxString& name, bool verbose = true, int index = -1);

  void SetName(const wxString& name)
  {
    m_name = name;
  }
  void SetExtension(const wxString& ext)
  {
    m_extension = ext;
  }
  void SetType(long type)
  {
    m_type = type;
  }
  void SetMimeType(const wxString& type)
  {
    m_mime = type;
  }
  wxString GetName() const
  {
    return m_name;
  }
  wxString GetExtension() const
  {
    return m_extension;
  }
  long GetType() const
  {
    return m_type;
  }
  wxString GetMimeType() const
  {
    return m_mime;
  }

protected:
  wxString m_name;
  wxString m_extension;
  wxString m_mime;
  long m_type;
};

class wxImage : public wxObject
{
public:
  wxImage();
  wxImage(int width, int height);
  wxImage(const wxString& name, long type = wxBITMAP_TYPE_ANY);
  wxImage(const wxImage&) = default;
  wxImage& operator=(const wxImage&) = default;
  ~wxImage() override;

  void Create(int width, int height);
  void Destroy();

  /// The image scaled to `width` x `height`, each pixel from the nearest one (wxImage::Scale).
  wxImage Scale(int width, int height) const;
  /// Scales the image in place.
  wxImage& Rescale(int width, int height)
  {
    return *this = Scale(width, height);
  }

  /// Loads `name` with the handler of `type` (any registered handler for wxBITMAP_TYPE_ANY).
  bool LoadFile(const wxString& name, long type = wxBITMAP_TYPE_ANY);

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
  /// The pixels, three bytes (R, G, B) each, row by row.
  unsigned char* GetData() const
  {
    return m_ok ? const_cast<unsigned char*>(m_data.data()) : nullptr;
  }

  bool operator==(const wxImage& image) const
  {
    return m_ok == image.m_ok && m_width == image.m_width && m_height == image.m_height && m_data == image.m_data;
  }
  bool operator!=(const wxImage& image) const
  {
    return !(*this == image);
  }

  /// The registered handlers: the image owns them from here on (CleanUpHandlers deletes them).
  static void AddHandler(wxImageHandler* handler);
  static void InsertHandler(wxImageHandler* handler);
  static bool RemoveHandler(const wxString& name);
  static wxImageHandler* FindHandler(const wxString& name);
  static wxImageHandler* FindHandler(long imageType);
  static void CleanUpHandlers();

private:
  bool m_ok;
  int m_width;
  int m_height;
  std::vector<unsigned char> m_data;
};

extern wxImage wxNullImage;

#include "wx/imagpng.h"
