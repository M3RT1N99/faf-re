// wx-lite (android/native/WX_LITE.md): bitmaps, icons, images and device contexts of wxWindows 2.4.2
// (src/msw/bitmap.cpp, icon.cpp, dc.cpp, dcclient.cpp; src/common/image.cpp). Images hold their
// pixels; device contexts belong to a window and draw nothing (Android shows nothing wx draws),
// and a paint DC opens and ends the window's paint as on Windows.

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "Private.h"
#include "wx/bitmap.h"
#include "wx/dc.h"
#include "wx/dcclient.h"
#include "wx/icon.h"
#include "wx/image.h"
#include "wx/imagpng.h"
#include "wx/window.h"

wxBitmap wxNullBitmap;
wxIcon wxNullIcon;
wxImage wxNullImage;

namespace
{
  /// The image handlers (wxImage::sm_handlers), which the image owns.
  std::vector<wxImageHandler*>& Handlers()
  {
    static std::vector<wxImageHandler*>* const handlers = new std::vector<wxImageHandler*>;
    return *handlers;
  }
} // namespace

// --- wxBitmap -----------------------------------------------------------------------------------

wxBitmap::wxBitmap(const wxString& filename, const long type)
  : wxBitmap()
{
  LoadFile(filename, type);
}

wxBitmap::wxBitmap(const int w, const int h, const int d)
  : wxBitmap()
{
  (void)Create(w, h, d);
}

wxBitmap::wxBitmap(const wxImage& image, const int depth)
  : wxBitmap()
{
  if (!image.Ok()) {
    return;
  }
  m_ok = true;
  m_width = image.GetWidth();
  m_height = image.GetHeight();
  m_depth = depth == -1 ? 24 : depth;
  m_pixels = std::make_shared<const wxImage>(image);
}

wxBitmap::~wxBitmap() = default;

bool wxBitmap::Create(const int w, const int h, const int d)
{
  if (w <= 0 || h <= 0) {
    *this = wxBitmap();
    return false;
  }
  m_ok = true;
  m_width = w;
  m_height = h;
  // the screen's depth for -1
  m_depth = d == -1 ? 32 : d;
  m_pixels = std::make_shared<const wxImage>(w, h);
  return true;
}

wxImage wxBitmap::ConvertToImage() const
{
  return m_ok && m_pixels ? *m_pixels : wxImage();
}

bool wxBitmap::LoadFile(const wxString& filename, const long type)
{
  // the bitmap handlers of wxMSW load BMP resources, which Android has none of: a file goes to
  // the image handlers, as wxBitmap::LoadFile hands it on to wxImage when no bitmap handler takes
  // it
  *this = wxBitmap();
  wxImage image;
  if (image.LoadFile(filename, type) && image.Ok()) {
    *this = wxBitmap(image);
    return true;
  }
  return false;
}

// --- wxIcon -------------------------------------------------------------------------------------

wxIcon::wxIcon(const wxString& iconfile, const long flags, const int desiredWidth, const int desiredHeight)
  : m_ok(false)
{
  LoadFile(iconfile, flags, desiredWidth, desiredHeight);
}

wxIcon::~wxIcon() = default;

bool wxIcon::LoadFile(const wxString& filename, long WXUNUSED(type), int WXUNUSED(desiredWidth), int WXUNUSED(desiredHeight))
{
  // a resource or an .ico file: there are no resources on Android, and no window shows an icon
  m_ok = false;
  m_name = filename;
  return false;
}

// --- wxImage ------------------------------------------------------------------------------------

wxImage::wxImage()
  : m_ok(false)
  , m_width(0)
  , m_height(0)
{}

wxImage::wxImage(const int width, const int height)
  : wxImage()
{
  Create(width, height);
}

wxImage::wxImage(const wxString& name, const long type)
  : wxImage()
{
  LoadFile(name, type);
}

wxImage::~wxImage() = default;

void wxImage::Create(const int width, const int height)
{
  if (width <= 0 || height <= 0) {
    Destroy();
    return;
  }
  m_data.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3, 0);
  m_width = width;
  m_height = height;
  m_ok = true;
}

void wxImage::Destroy()
{
  m_data.clear();
  m_width = 0;
  m_height = 0;
  m_ok = false;
}

wxImage wxImage::Scale(const int width, const int height) const
{
  wxImage image;

  wxCHECK_MSG(Ok(), image, wxT("invalid image"));
  wxCHECK_MSG((width > 0) && (height > 0), image, wxT("invalid image size"));

  image.Create(width, height);

  const long old_height = m_height;
  const long old_width = m_width;

  const unsigned char* const source_data = m_data.data();
  unsigned char* target_data = image.m_data.data();

  for (long j = 0; j < height; j++) {
    const long y_offset = (j * old_height / height) * old_width;

    for (long i = 0; i < width; i++) {
      std::memcpy(target_data, source_data + 3 * (y_offset + ((i * old_width) / width)), 3);
      target_data += 3;
    }
  }
  return image;
}

bool wxImage::LoadFile(const wxString& filename, const long type)
{
  Destroy();

  if (::GetFileAttributesW(filename.c_str()) == INVALID_FILE_ATTRIBUTES) {
    // Can't load image from file: file does not exist.
    return false;
  }

  if (type == wxBITMAP_TYPE_ANY) {
    for (wxImageHandler* const handler : Handlers()) {
      if (handler->LoadFile(this, filename)) {
        return true;
      }
    }
    // No handler found for image type.
    return false;
  }

  wxImageHandler* const handler = FindHandler(type);
  if (handler == nullptr) {
    // No image handler for type defined.
    return false;
  }
  return handler->LoadFile(this, filename);
}

void wxImage::AddHandler(wxImageHandler* const handler)
{
  // make sure that the memory will be freed at the program end
  Handlers().push_back(handler);
}

void wxImage::InsertHandler(wxImageHandler* const handler)
{
  Handlers().insert(Handlers().begin(), handler);
}

bool wxImage::RemoveHandler(const wxString& name)
{
  std::vector<wxImageHandler*>& handlers = Handlers();
  const auto found = std::find_if(handlers.begin(), handlers.end(), [&name](const wxImageHandler* h) { return h->GetName() == name; });
  if (found == handlers.end()) {
    return false;
  }
  delete *found;
  handlers.erase(found);
  return true;
}

wxImageHandler* wxImage::FindHandler(const wxString& name)
{
  for (wxImageHandler* const handler : Handlers()) {
    if (handler->GetName().IsSameAs(name.c_str())) {
      return handler;
    }
  }
  return nullptr;
}

wxImageHandler* wxImage::FindHandler(const long bitmapType)
{
  for (wxImageHandler* const handler : Handlers()) {
    if (handler->GetType() == bitmapType) {
      return handler;
    }
  }
  return nullptr;
}

void wxImage::CleanUpHandlers()
{
  for (wxImageHandler* const handler : Handlers()) {
    delete handler;
  }
  Handlers().clear();
}

bool wxImageHandler::LoadFile(wxImage* WXUNUSED(image), const wxString& WXUNUSED(name), bool WXUNUSED(verbose), int WXUNUSED(index))
{
  return false;
}

bool wxPNGHandler::LoadFile(wxImage* WXUNUSED(image), const wxString& WXUNUSED(name), bool WXUNUSED(verbose), int WXUNUSED(index))
{
  return false;
}

// --- Device contexts ----------------------------------------------------------------------------

wxDC::wxDC()
  : m_ok(false)
  , m_canvas(nullptr)
{}

wxDC::~wxDC() = default;

void wxDC::Clear() {}

void wxDC::SetBrush(const wxBrush& brush)
{
  m_brush = brush;
}

void wxDC::DoDrawRectangle(wxCoord WXUNUSED(x), wxCoord WXUNUSED(y), wxCoord WXUNUSED(width), wxCoord WXUNUSED(height)) {}

void wxDC::DoDrawBitmap(const wxBitmap& WXUNUSED(bmp), wxCoord WXUNUSED(x), wxCoord WXUNUSED(y), bool WXUNUSED(useMask)) {}

void wxDC::DoGetSize(int* const width, int* const height) const
{
  int w = 0;
  int h = 0;
  if (m_canvas != nullptr) {
    m_canvas->GetClientSize(&w, &h);
  }
  if (width) {
    *width = w;
  }
  if (height) {
    *height = h;
  }
}

wxWindowDC::wxWindowDC() {}

wxWindowDC::wxWindowDC(wxWindow* const win)
{
  wxCHECK_RET(win, wxT("invalid window in wxWindowDC"));
  m_canvas = win;
  m_ok = true;
}

wxClientDC::wxClientDC() {}

wxClientDC::wxClientDC(wxWindow* const win)
  : wxWindowDC(win)
{}

wxClientDC::~wxClientDC() {}

namespace
{
  /// A window's paint between BeginPaint and EndPaint, shared by the paint DCs made for it in
  /// the meantime (wxPaintDC's cache in wxWindows 2.4.2: the handler of a derived class that
  /// skips the paint event, then the base class's, each make one).
  struct OpenPaint
  {
    PAINTSTRUCT paint{};
    int count = 0;
  };

  std::unordered_map<wxWindow*, OpenPaint>& OpenPaints()
  {
    static std::unordered_map<wxWindow*, OpenPaint>* const paints = new std::unordered_map<wxWindow*, OpenPaint>;
    return *paints;
  }
} // namespace

wxPaintDC::wxPaintDC()
  : m_paintStruct(nullptr)
{}

wxPaintDC::wxPaintDC(wxWindow* const canvas)
  : m_paintStruct(nullptr)
{
  wxCHECK_RET(canvas, wxT("NULL canvas in wxPaintDC ctor"));
  m_canvas = canvas;

  // see if we had already called BeginPaint() for this window
  OpenPaint& open = OpenPaints()[canvas];
  if (open.count == 0) {
    (void)::BeginPaint(wxlite::GetHwndOf(canvas), &open.paint);
  }
  ++open.count;
  m_paintStruct = &open;
  m_ok = true;
}

wxPaintDC::~wxPaintDC()
{
  if (m_paintStruct == nullptr) {
    return;
  }
  auto& paints = OpenPaints();
  const auto found = paints.find(m_canvas);
  if (found != paints.end() && --found->second.count == 0) {
    ::EndPaint(wxlite::GetHwndOf(m_canvas), &found->second.paint);
    paints.erase(found);
  }
}
