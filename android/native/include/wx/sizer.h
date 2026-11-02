#pragma once

// wx-lite (android/native/WX_LITE.md): wxWindows 2.4.2's sizers (include/wx/sizer.h) the engine
// uses: wxSizerItem, wxSizer, wxBoxSizer and wxStaticBoxSizer, with the layout of
// src/common/sizer.cpp (the engine reads the sizes they give its dialogs back into its
// preferences).

#include <vector>

#include "wx/defs.h"
#include "wx/gdicmn.h"
#include "wx/object.h"

class wxSizer;
class wxStaticBox;
class wxWindow;

class wxSizerItem : public wxObject
{
public:
  // spacer
  wxSizerItem(int width, int height, int option, int flag, int border, wxObject* userData);
  // window
  wxSizerItem(wxWindow* window, int option, int flag, int border, wxObject* userData);
  // subsizer
  wxSizerItem(wxSizer* sizer, int option, int flag, int border, wxObject* userData);

  ~wxSizerItem() override;

  virtual void DeleteWindows();

  virtual wxSize GetSize();
  virtual wxSize CalcMin();
  virtual void SetDimension(wxPoint pos, wxSize size);

  wxSize GetMinSize()
  {
    return m_minSize;
  }

  void SetRatio(int width, int height)
  {
    m_ratio = (width && height) ? (static_cast<float>(width) / static_cast<float>(height)) : 1;
  }
  void SetRatio(wxSize size)
  {
    m_ratio = (size.x && size.y) ? (static_cast<float>(size.x) / static_cast<float>(size.y)) : 1;
  }
  void SetRatio(float ratio)
  {
    m_ratio = ratio;
  }
  float GetRatio() const
  {
    return m_ratio;
  }

  bool IsWindow();
  bool IsSizer();
  bool IsSpacer();

  void SetInitSize(int x, int y)
  {
    m_minSize.x = x;
    m_minSize.y = y;
  }
  void SetOption(int option)
  {
    m_option = option;
  }
  void SetFlag(int flag)
  {
    m_flag = flag;
  }
  void SetBorder(int border)
  {
    m_border = border;
  }
  void Show(bool show)
  {
    m_show = show;
  }

  wxWindow* GetWindow() const
  {
    return m_window;
  }
  void SetWindow(wxWindow* window)
  {
    m_window = window;
  }
  wxSizer* GetSizer() const
  {
    return m_sizer;
  }
  void SetSizer(wxSizer* sizer)
  {
    m_sizer = sizer;
  }
  int GetOption() const
  {
    return m_option;
  }
  int GetFlag() const
  {
    return m_flag;
  }
  int GetBorder() const
  {
    return m_border;
  }
  bool IsShown() const
  {
    return m_show;
  }
  wxObject* GetUserData()
  {
    return m_userData;
  }
  wxPoint GetPosition()
  {
    return m_pos;
  }

protected:
  wxWindow* m_window;
  wxSizer* m_sizer;
  wxSize m_size;
  wxPoint m_pos;
  wxSize m_minSize;
  int m_option;
  int m_border;
  int m_flag;

  // If TRUE, then this item is considered in the layout calculation. Otherwise, it is skipped
  // over.
  bool m_show;

  // aspect ratio can always be calculated from m_size, but this would cause precision loss when
  // the window is shrunk. it is safer to preserve initial value.
  float m_ratio;

  wxObject* m_userData;
};

/// The items of a sizer (wxWindows 2.4.2 keeps a wxList of them).
using wxSizerItemList = std::vector<wxSizerItem*>;

class wxSizer : public wxObject
{
public:
  wxSizer();
  ~wxSizer() override;

  /* These should be called Append() really. */
  virtual void Add(wxWindow* window, int option = 0, int flag = 0, int border = 0, wxObject* userData = nullptr);
  virtual void Add(wxSizer* sizer, int option = 0, int flag = 0, int border = 0, wxObject* userData = nullptr);
  virtual void Add(int width, int height, int option = 0, int flag = 0, int border = 0, wxObject* userData = nullptr);

  virtual void Insert(
    int before, wxWindow* window, int option = 0, int flag = 0, int border = 0, wxObject* userData = nullptr
  );
  virtual void Insert(
    int before, wxSizer* sizer, int option = 0, int flag = 0, int border = 0, wxObject* userData = nullptr
  );
  virtual void Insert(
    int before, int width, int height, int option = 0, int flag = 0, int border = 0, wxObject* userData = nullptr
  );

  virtual void Prepend(wxWindow* window, int option = 0, int flag = 0, int border = 0, wxObject* userData = nullptr);
  virtual void Prepend(wxSizer* sizer, int option = 0, int flag = 0, int border = 0, wxObject* userData = nullptr);
  virtual void Prepend(
    int width, int height, int option = 0, int flag = 0, int border = 0, wxObject* userData = nullptr
  );

  virtual bool Remove(wxWindow* window);
  virtual bool Remove(wxSizer* sizer);
  virtual bool Remove(int pos);

  virtual void Clear(bool delete_windows = false);
  virtual void DeleteWindows();

  void SetMinSize(int width, int height)
  {
    DoSetMinSize(width, height);
  }
  void SetMinSize(wxSize size)
  {
    DoSetMinSize(size.x, size.y);
  }

  wxSize GetSize()
  {
    return m_size;
  }
  wxPoint GetPosition()
  {
    return m_position;
  }

  /* Calculate the minimal size or return m_minSize if bigger. */
  wxSize GetMinSize();

  virtual void RecalcSizes() = 0;
  virtual wxSize CalcMin() = 0;

  virtual void Layout();

  /// Sizes `window` to the sizer's minimum (within the window's limits) and returns that size.
  wxSize Fit(wxWindow* window);
  /// Makes the sizer's minimum the window's size hints.
  void SetSizeHints(wxWindow* window);

  wxSizerItemList& GetChildren()
  {
    return m_children;
  }

  void SetDimension(int x, int y, int width, int height);

  void Show(wxWindow* window, bool show = true);
  void Hide(wxWindow* window)
  {
    Show(window, false);
  }
  void Show(wxSizer* sizer, bool show = true);
  void Hide(wxSizer* sizer)
  {
    Show(sizer, false);
  }
  bool IsShown(wxWindow* window);
  bool IsShown(wxSizer* sizer);

  // Recursively call wxWindow::Show () on all sizer items.
  void ShowItems(bool show);

protected:
  wxSize m_size;
  wxSize m_minSize;
  wxPoint m_position;
  wxSizerItemList m_children;

  wxSize GetMaxWindowSize(wxWindow* window);
  wxSize GetMinWindowSize(wxWindow* window);
  wxSize GetMaxClientSize(wxWindow* window);
  wxSize GetMinClientSize(wxWindow* window);
  wxSize FitSize(wxWindow* window);

  virtual void DoSetMinSize(int width, int height);
};

class wxBoxSizer : public wxSizer
{
public:
  wxBoxSizer(int orient);

  void RecalcSizes() override;
  wxSize CalcMin() override;

  int GetOrientation()
  {
    return m_orient;
  }
  void SetOrientation(int orient)
  {
    m_orient = orient;
  }

protected:
  int m_orient;
  int m_stretchable;
  int m_minWidth;
  int m_minHeight;
  int m_fixedWidth;
  int m_fixedHeight;
};

class wxStaticBoxSizer : public wxBoxSizer
{
public:
  wxStaticBoxSizer(wxStaticBox* box, int orient);

  void RecalcSizes() override;
  wxSize CalcMin() override;

  wxStaticBox* GetStaticBox()
  {
    return m_staticBox;
  }

protected:
  wxStaticBox* m_staticBox;
};
