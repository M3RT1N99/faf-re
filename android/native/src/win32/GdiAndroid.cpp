// The GDI font functions of include/win32/Windows.h: no GDI on Android yet. CreateFontA finds no
// font, so the font renderer (moho/render/d3d/CD3DFont.cpp) creates none and the game draws no
// text, the way GDI answers for a face it does not have. The rest answer with GDI's failure values;
// without a font nothing reaches them. CLIENT_PLAN.md's C3 renders through these same functions
// with FreeType, so CD3DFont.cpp stays as it is.

#include <Windows.h>

namespace
{
  constexpr COLORREF kInvalidColour = 0xFFFFFFFFu; // CLR_INVALID
  constexpr UINT kGdiError = 0xFFFFFFFFu;          // GDI_ERROR
} // namespace

HFONT CreateFontA(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, const char*)
{
  ::SetLastError(ERROR_NOT_SUPPORTED);
  return nullptr;
}

BOOL DeleteObject(HGDIOBJ)
{
  return FALSE;
}

HDC CreateCompatibleDC(HDC)
{
  return nullptr;
}

HBITMAP CreateBitmap(int, int, UINT, UINT, const void*)
{
  return nullptr;
}

HGDIOBJ SelectObject(HDC, HGDIOBJ)
{
  return nullptr;
}

BOOL GetTextMetricsW(HDC, TEXTMETRICW* const metrics)
{
  if (metrics != nullptr) {
    *metrics = TEXTMETRICW{};
  }
  return FALSE;
}

DWORD GetKerningPairsW(HDC, DWORD, KERNINGPAIR*)
{
  return 0;
}

int GetObjectW(HGDIOBJ, int, void*)
{
  return 0;
}

LONG GetBitmapBits(HBITMAP, LONG, void*)
{
  return 0;
}

BOOL GdiFlush()
{
  return TRUE;
}

UINT SetTextAlign(HDC, UINT)
{
  return kGdiError;
}

COLORREF SetBkColor(HDC, COLORREF)
{
  return kInvalidColour;
}

COLORREF SetTextColor(HDC, COLORREF)
{
  return kInvalidColour;
}

COLORREF GetPixel(HDC, int, int)
{
  return kInvalidColour;
}

int DrawTextW(HDC, const wchar_t*, int, RECT*, UINT)
{
  return 0;
}

HGDIOBJ GetStockObject(int)
{
  return nullptr;
}

int FillRect(HDC, const RECT*, HBRUSH)
{
  return 0;
}
