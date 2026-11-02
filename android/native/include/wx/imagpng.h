#pragma once

// wx-lite (android/native/WX_LITE.md): wxPNGHandler of wxWindows 2.4.2 (include/wx/imagpng.h).
// wx-lite has no PNG decoder: what wx would draw is never shown on Android (the renderer draws
// the game), so the one PNG the engine loads through wx, the splash screen
// (moho::WINX_InitSplash), does not load, and the engine starts without a splash.

#include "wx/image.h"

class wxPNGHandler : public wxImageHandler
{
public:
  wxPNGHandler()
  {
    m_name = wxT("PNG file");
    m_extension = wxT("png");
    m_type = wxBITMAP_TYPE_PNG;
    m_mime = wxT("image/png");
  }

  /// False: no decoder (see above).
  bool LoadFile(wxImage* image, const wxString& name, bool verbose = true, int index = -1) override;
};
