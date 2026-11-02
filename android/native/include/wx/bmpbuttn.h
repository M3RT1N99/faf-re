#pragma once

// wx-lite (android/native/WX_LITE.md): wxWindows 2.4.2's wxBitmapButtonBase (include/wx/bmpbuttn.h)
// and the MSW port's wxBitmapButton (include/wx/msw/bmpbuttn.h), for what the engine uses.

#include "wx/bitmap.h"
#include "wx/button.h"

class wxBitmapButtonBase : public wxButton
{
public:
  wxBitmapButtonBase()
    : m_marginX(0)
    , m_marginY(0)
  {}

  // set the bitmaps
  void SetBitmapLabel(const wxBitmap& bitmap)
  {
    m_bmpNormal = bitmap;
    OnSetBitmap();
  }
  void SetBitmapSelected(const wxBitmap& sel)
  {
    m_bmpSelected = sel;
    OnSetBitmap();
  }
  void SetBitmapFocus(const wxBitmap& focus)
  {
    m_bmpFocus = focus;
    OnSetBitmap();
  }
  void SetBitmapDisabled(const wxBitmap& disabled)
  {
    m_bmpDisabled = disabled;
    OnSetBitmap();
  }

  // retrieve the bitmaps
  const wxBitmap& GetBitmapLabel() const
  {
    return m_bmpNormal;
  }
  const wxBitmap& GetBitmapSelected() const
  {
    return m_bmpSelected;
  }
  const wxBitmap& GetBitmapFocus() const
  {
    return m_bmpFocus;
  }
  const wxBitmap& GetBitmapDisabled() const
  {
    return m_bmpDisabled;
  }
  wxBitmap& GetBitmapLabel()
  {
    return m_bmpNormal;
  }
  wxBitmap& GetBitmapSelected()
  {
    return m_bmpSelected;
  }
  wxBitmap& GetBitmapFocus()
  {
    return m_bmpFocus;
  }
  wxBitmap& GetBitmapDisabled()
  {
    return m_bmpDisabled;
  }

  // set/get the margins around the button
  virtual void SetMargins(int x, int y)
  {
    m_marginX = x;
    m_marginY = y;
  }
  int GetMarginX() const
  {
    return m_marginX;
  }
  int GetMarginY() const
  {
    return m_marginY;
  }

protected:
  // function called when any of the bitmaps changes
  virtual void OnSetBitmap() {}

  // the bitmaps for various states
  wxBitmap m_bmpNormal, m_bmpSelected, m_bmpFocus, m_bmpDisabled;

  // the margins around the bitmap
  int m_marginX, m_marginY;
};

class wxBitmapButton : public wxBitmapButtonBase
{
public:
  wxBitmapButton() {}
  wxBitmapButton(
    wxWindow* parent,
    wxWindowID id,
    const wxBitmap& bitmap,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxBU_AUTODRAW,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxButtonNameStr
  )
  {
    Create(parent, id, bitmap, pos, size, style, validator, name);
  }

  bool Create(
    wxWindow* parent,
    wxWindowID id,
    const wxBitmap& bitmap,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxBU_AUTODRAW,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxButtonNameStr
  );

  void SetLabel(const wxBitmap& bitmap)
  {
    SetBitmapLabel(bitmap);
  }
  void SetLabel(const wxString& label) override
  {
    wxWindow::SetLabel(label);
  }

protected:
  wxSize DoGetBestSize() const override;
};
