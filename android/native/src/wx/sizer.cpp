// wx-lite (android/native/WX_LITE.md): the sizers of wxWindows 2.4.2 the engine uses
// (src/common/sizer.cpp): wxSizerItem, wxSizer, wxBoxSizer and wxStaticBoxSizer, with their layout.

#include <algorithm>

#include "wx/sizer.h"
#include "wx/statbox.h"
#include "wx/window.h"

// --- wxSizerItem --------------------------------------------------------------------------------

wxSizerItem::wxSizerItem(const int width, const int height, const int option, const int flag, const int border, wxObject* const userData)
{
  m_window = nullptr;
  m_sizer = nullptr;
  m_option = option;
  m_border = border;
  m_flag = flag;
  m_show = true; // Cannot be changed
  m_userData = userData;

  // minimal size is the initial size
  m_minSize.x = width;
  m_minSize.y = height;

  SetRatio(width, height);

  // size is set directly
  m_size = m_minSize;
}

wxSizerItem::wxSizerItem(wxWindow* const window, const int option, const int flag, const int border, wxObject* const userData)
{
  m_window = window;
  m_sizer = nullptr;
  m_option = option;
  m_border = border;
  m_flag = flag;
  m_show = true;
  m_userData = userData;

  // minimal size is the initial size
  m_minSize = window->GetSize();

  // aspect ratio calculated from initial size
  SetRatio(m_minSize);

  // size is calculated later
}

wxSizerItem::wxSizerItem(wxSizer* const sizer, const int option, const int flag, const int border, wxObject* const userData)
{
  m_window = nullptr;
  m_sizer = sizer;
  m_option = option;
  m_border = border;
  m_flag = flag;
  m_show = true;
  m_userData = userData;

  // minimal size is calculated later
  m_ratio = 0;

  // size is calculated later
}

wxSizerItem::~wxSizerItem()
{
  delete m_userData;
  delete m_sizer;
}

wxSize wxSizerItem::GetSize()
{
  wxSize ret;
  if (IsSizer()) {
    ret = m_sizer->GetSize();
  } else if (IsWindow()) {
    ret = m_window->GetSize();
  } else {
    ret = m_size;
  }

  if (m_flag & wxWEST) {
    ret.x += m_border;
  }
  if (m_flag & wxEAST) {
    ret.x += m_border;
  }
  if (m_flag & wxNORTH) {
    ret.y += m_border;
  }
  if (m_flag & wxSOUTH) {
    ret.y += m_border;
  }
  return ret;
}

wxSize wxSizerItem::CalcMin()
{
  wxSize ret;
  if (IsSizer()) {
    ret = m_sizer->GetMinSize();

    // if we have to preserve aspect ratio _AND_ this is the first-time calculation, consider ret
    // to be initial size
    if ((m_flag & wxSHAPED) && !m_ratio) {
      SetRatio(ret);
    }
  } else {
    if (IsWindow() && (m_flag & wxADJUST_MINSIZE)) {
      // By user request, keep the minimal size for this item in sync with the largest of
      // BestSize and any user supplied minimum size hint. Useful in cases where the item is
      // changeable -- static text labels, etc.
      m_minSize = m_window->GetAdjustedBestSize();
    }
    ret = m_minSize;
  }

  if (m_flag & wxWEST) {
    ret.x += m_border;
  }
  if (m_flag & wxEAST) {
    ret.x += m_border;
  }
  if (m_flag & wxNORTH) {
    ret.y += m_border;
  }
  if (m_flag & wxSOUTH) {
    ret.y += m_border;
  }
  return ret;
}

void wxSizerItem::SetDimension(wxPoint pos, wxSize size)
{
  if (m_flag & wxSHAPED) {
    // adjust aspect ratio
    const int rwidth = static_cast<int>(size.y * m_ratio);
    if (rwidth > size.x) {
      // fit horizontally
      const int rheight = static_cast<int>(size.x / m_ratio);
      // add vertical space
      if (m_flag & wxALIGN_CENTER_VERTICAL) {
        pos.y += (size.y - rheight) / 2;
      } else if (m_flag & wxALIGN_BOTTOM) {
        pos.y += (size.y - rheight);
      }
      // use reduced dimensions
      size.y = rheight;
    } else if (rwidth < size.x) {
      // add horizontal space
      if (m_flag & wxALIGN_CENTER_HORIZONTAL) {
        pos.x += (size.x - rwidth) / 2;
      } else if (m_flag & wxALIGN_RIGHT) {
        pos.x += (size.x - rwidth);
      }
      size.x = rwidth;
    }
  }

  // This is what GetPosition() returns. Since we calculate borders afterwards, GetPosition() will
  // be the left/top corner of the surrounding border.
  m_pos = pos;

  if (m_flag & wxWEST) {
    pos.x += m_border;
    size.x -= m_border;
  }
  if (m_flag & wxEAST) {
    size.x -= m_border;
  }
  if (m_flag & wxNORTH) {
    pos.y += m_border;
    size.y -= m_border;
  }
  if (m_flag & wxSOUTH) {
    size.y -= m_border;
  }

  if (IsSizer()) {
    m_sizer->SetDimension(pos.x, pos.y, size.x, size.y);
  }

  if (IsWindow()) {
    m_window->SetSize(pos.x, pos.y, size.x, size.y, wxSIZE_ALLOW_MINUS_ONE);
  }

  m_size = size;
}

void wxSizerItem::DeleteWindows()
{
  if (m_window) {
    m_window->Destroy();
  }
  if (m_sizer) {
    m_sizer->DeleteWindows();
  }
}

bool wxSizerItem::IsWindow()
{
  return m_window != nullptr;
}

bool wxSizerItem::IsSizer()
{
  return m_sizer != nullptr;
}

bool wxSizerItem::IsSpacer()
{
  return (m_window == nullptr) && (m_sizer == nullptr);
}

// --- wxSizer ------------------------------------------------------------------------------------

wxSizer::wxSizer()
{
  m_minSize.x = 0;
  m_minSize.y = 0;
}

wxSizer::~wxSizer()
{
  Clear();
}

void wxSizer::Add(wxWindow* const window, const int option, const int flag, const int border, wxObject* const userData)
{
  m_children.push_back(new wxSizerItem(window, option, flag, border, userData));
  window->SetContainingSizer(this);
}

void wxSizer::Add(wxSizer* const sizer, const int option, const int flag, const int border, wxObject* const userData)
{
  m_children.push_back(new wxSizerItem(sizer, option, flag, border, userData));
}

void wxSizer::Add(const int width, const int height, const int option, const int flag, const int border, wxObject* const userData)
{
  m_children.push_back(new wxSizerItem(width, height, option, flag, border, userData));
}

void wxSizer::Prepend(wxWindow* const window, const int option, const int flag, const int border, wxObject* const userData)
{
  m_children.insert(m_children.begin(), new wxSizerItem(window, option, flag, border, userData));
  window->SetContainingSizer(this);
}

void wxSizer::Prepend(wxSizer* const sizer, const int option, const int flag, const int border, wxObject* const userData)
{
  m_children.insert(m_children.begin(), new wxSizerItem(sizer, option, flag, border, userData));
}

void wxSizer::Prepend(const int width, const int height, const int option, const int flag, const int border, wxObject* const userData)
{
  m_children.insert(m_children.begin(), new wxSizerItem(width, height, option, flag, border, userData));
}

void wxSizer::Insert(const int before, wxWindow* const window, const int option, const int flag, const int border, wxObject* const userData)
{
  const std::size_t at = std::min<std::size_t>(static_cast<std::size_t>(std::max(before, 0)), m_children.size());
  m_children.insert(m_children.begin() + static_cast<std::ptrdiff_t>(at), new wxSizerItem(window, option, flag, border, userData));
  window->SetContainingSizer(this);
}

void wxSizer::Insert(const int before, wxSizer* const sizer, const int option, const int flag, const int border, wxObject* const userData)
{
  const std::size_t at = std::min<std::size_t>(static_cast<std::size_t>(std::max(before, 0)), m_children.size());
  m_children.insert(m_children.begin() + static_cast<std::ptrdiff_t>(at), new wxSizerItem(sizer, option, flag, border, userData));
}

void wxSizer::Insert(
  const int before, const int width, const int height, const int option, const int flag, const int border, wxObject* const userData
)
{
  const std::size_t at = std::min<std::size_t>(static_cast<std::size_t>(std::max(before, 0)), m_children.size());
  m_children.insert(
    m_children.begin() + static_cast<std::ptrdiff_t>(at), new wxSizerItem(width, height, option, flag, border, userData)
  );
}

bool wxSizer::Remove(wxWindow* const window)
{
  for (auto it = m_children.begin(); it != m_children.end(); ++it) {
    wxSizerItem* const item = *it;
    if (item->GetWindow() == window) {
      item->GetWindow()->SetContainingSizer(nullptr);
      m_children.erase(it);
      delete item;
      return true;
    }
  }
  return false;
}

bool wxSizer::Remove(wxSizer* const sizer)
{
  for (auto it = m_children.begin(); it != m_children.end(); ++it) {
    wxSizerItem* const item = *it;
    if (item->GetSizer() == sizer) {
      m_children.erase(it);
      delete item;
      return true;
    }
  }
  return false;
}

bool wxSizer::Remove(const int pos)
{
  if (pos < 0 || static_cast<std::size_t>(pos) >= m_children.size()) {
    return false;
  }
  wxSizerItem* const item = m_children[static_cast<std::size_t>(pos)];
  m_children.erase(m_children.begin() + pos);
  delete item;
  return true;
}

void wxSizer::Clear(const bool delete_windows)
{
  // First clear the ContainingSizer pointers
  for (wxSizerItem* const item : m_children) {
    if (item->IsWindow()) {
      item->GetWindow()->SetContainingSizer(nullptr);
    }
  }

  // Destroy the windows if needed
  if (delete_windows) {
    DeleteWindows();
  }

  // Now empty the list
  for (wxSizerItem* const item : m_children) {
    delete item;
  }
  m_children.clear();
}

void wxSizer::DeleteWindows()
{
  for (wxSizerItem* const item : m_children) {
    item->DeleteWindows();
  }
}

wxSize wxSizer::Fit(wxWindow* const window)
{
  wxSize size;
  if (window->IsTopLevel()) {
    size = FitSize(window);
  } else {
    size = GetMinWindowSize(window);
  }

  window->SetSize(size);
  return size;
}

void wxSizer::Layout()
{
  CalcMin();
  RecalcSizes();
}

void wxSizer::SetSizeHints(wxWindow* const window)
{
  // Preserve the window's max size hints, but set the lower bound according to the sizer
  // calculations.
  const wxSize size = Fit(window);
  window->SetSizeHints(size.x, size.y, window->GetMaxWidth(), window->GetMaxHeight());
}

wxSize wxSizer::GetMaxWindowSize(wxWindow* const window)
{
  return window->GetMaxSize();
}

wxSize wxSizer::GetMinWindowSize(wxWindow* const window)
{
  const wxSize minSize(GetMinSize());
  const wxSize size(window->GetSize());
  const wxSize client_size(window->GetClientSize());
  return wxSize(minSize.x + size.x - client_size.x, minSize.y + size.y - client_size.y);
}

// Return a window size that will fit within the screens dimensions
wxSize wxSizer::FitSize(wxWindow* const window)
{
  wxSize size = GetMinWindowSize(window);
  const wxSize sizeMax = GetMaxWindowSize(window);

  // Limit the size if sizeMax != wxDefaultSize
  if (size.x > sizeMax.x && sizeMax.x != -1) {
    size.x = sizeMax.x;
  }
  if (size.y > sizeMax.y && sizeMax.y != -1) {
    size.y = sizeMax.y;
  }
  return size;
}

wxSize wxSizer::GetMaxClientSize(wxWindow* const window)
{
  const wxSize maxSize(window->GetMaxSize());
  if (maxSize != wxDefaultSize) {
    const wxSize size(window->GetSize());
    const wxSize client_size(window->GetClientSize());
    return wxSize(maxSize.x + client_size.x - size.x, maxSize.y + client_size.y - size.y);
  }
  return wxDefaultSize;
}

wxSize wxSizer::GetMinClientSize(wxWindow* WXUNUSED(window))
{
  return GetMinSize(); // Already returns client size.
}

void wxSizer::SetDimension(const int x, const int y, const int width, const int height)
{
  m_position.x = x;
  m_position.y = y;
  m_size.x = width;
  m_size.y = height;
  Layout();
}

wxSize wxSizer::GetMinSize()
{
  wxSize ret(CalcMin());
  if (ret.x < m_minSize.x) {
    ret.x = m_minSize.x;
  }
  if (ret.y < m_minSize.y) {
    ret.y = m_minSize.y;
  }
  return ret;
}

void wxSizer::DoSetMinSize(const int width, const int height)
{
  m_minSize.x = width;
  m_minSize.y = height;
}

void wxSizer::Show(wxWindow* const window, const bool show)
{
  for (wxSizerItem* const item : m_children) {
    if (item->IsWindow() && item->GetWindow() == window) {
      item->Show(show);
      window->Show(show);
      return;
    }
  }
}

void wxSizer::Show(wxSizer* const sizer, const bool show)
{
  for (wxSizerItem* const item : m_children) {
    if (item->IsSizer() && item->GetSizer() == sizer) {
      item->Show(show);
      sizer->ShowItems(show);
      return;
    }
  }
}

void wxSizer::ShowItems(const bool show)
{
  for (wxSizerItem* const item : m_children) {
    if (item->IsWindow()) {
      item->GetWindow()->Show(show);
    } else if (item->IsSizer()) {
      item->GetSizer()->ShowItems(show);
    }
  }
}

bool wxSizer::IsShown(wxWindow* const window)
{
  for (wxSizerItem* const item : m_children) {
    if (item->IsWindow() && item->GetWindow() == window) {
      return item->IsShown();
    }
  }
  return false;
}

bool wxSizer::IsShown(wxSizer* const sizer)
{
  for (wxSizerItem* const item : m_children) {
    if (item->IsSizer() && item->GetSizer() == sizer) {
      return item->IsShown();
    }
  }
  return false;
}

// --- wxBoxSizer ---------------------------------------------------------------------------------

wxBoxSizer::wxBoxSizer(const int orient)
  : m_orient(orient)
  , m_stretchable(0)
  , m_minWidth(0)
  , m_minHeight(0)
  , m_fixedWidth(0)
  , m_fixedHeight(0)
{}

void wxBoxSizer::RecalcSizes()
{
  if (m_children.empty()) {
    return;
  }

  int delta = 0;
  int extra = 0;
  if (m_stretchable) {
    if (m_orient == wxHORIZONTAL) {
      delta = (m_size.x - m_fixedWidth) / m_stretchable;
      extra = (m_size.x - m_fixedWidth) % m_stretchable;
    } else {
      delta = (m_size.y - m_fixedHeight) / m_stretchable;
      extra = (m_size.y - m_fixedHeight) % m_stretchable;
    }
  }

  wxPoint pt(m_position);

  for (wxSizerItem* const item : m_children) {
    if (item->IsShown()) {
      int weight = 1;
      if (item->GetOption()) {
        weight = item->GetOption();
      }

      const wxSize size(item->CalcMin());

      if (m_orient == wxVERTICAL) {
        wxCoord height = size.y;
        if (item->GetOption()) {
          height = (delta * weight) + extra;
          extra = 0; // only the first item will get the remainder as extra size
        }

        wxPoint child_pos(pt);
        wxSize child_size(wxSize(size.x, height));

        if (item->GetFlag() & (wxEXPAND | wxSHAPED)) {
          child_size.x = m_size.x;
        } else if (item->GetFlag() & wxALIGN_RIGHT) {
          child_pos.x += m_size.x - size.x;
        } else if (item->GetFlag() & (static_cast<int>(wxCENTER) | wxALIGN_CENTER_HORIZONTAL)) {
          // XXX wxCENTER is added for backward compatibility; wxALIGN_CENTER should be used in
          // new code
          child_pos.x += (m_size.x - size.x) / 2;
        }

        item->SetDimension(child_pos, child_size);

        pt.y += height;
      } else {
        wxCoord width = size.x;
        if (item->GetOption()) {
          width = (delta * weight) + extra;
          extra = 0; // only the first item will get the remainder as extra size
        }

        wxPoint child_pos(pt);
        wxSize child_size(wxSize(width, size.y));

        if (item->GetFlag() & (wxEXPAND | wxSHAPED)) {
          child_size.y = m_size.y;
        } else if (item->GetFlag() & wxALIGN_BOTTOM) {
          child_pos.y += m_size.y - size.y;
        } else if (item->GetFlag() & (static_cast<int>(wxCENTER) | wxALIGN_CENTER_VERTICAL)) {
          // XXX wxCENTER is added for backward compatibility; wxALIGN_CENTER should be used in
          // new code
          child_pos.y += (m_size.y - size.y) / 2;
        }

        item->SetDimension(child_pos, child_size);

        pt.x += width;
      }
    }
  }
}

wxSize wxBoxSizer::CalcMin()
{
  if (m_children.empty()) {
    return wxSize(10, 10);
  }

  m_stretchable = 0;
  m_minWidth = 0;
  m_minHeight = 0;
  m_fixedWidth = 0;
  m_fixedHeight = 0;

  // Find how long each stretch unit needs to be
  int stretchSize = 1;
  for (wxSizerItem* const item : m_children) {
    if (item->IsShown() && item->GetOption() != 0) {
      const int stretch = item->GetOption();
      const wxSize size(item->CalcMin());
      int sizePerStretch;
      // Integer division rounded up is (a + b - 1) / b
      if (m_orient == wxHORIZONTAL) {
        sizePerStretch = (size.x + stretch - 1) / stretch;
      } else {
        sizePerStretch = (size.y + stretch - 1) / stretch;
      }
      if (sizePerStretch > stretchSize) {
        stretchSize = sizePerStretch;
      }
    }
  }

  // Calculate overall minimum size
  for (wxSizerItem* const item : m_children) {
    if (item->IsShown()) {
      m_stretchable += item->GetOption();

      wxSize size(item->CalcMin());
      if (item->GetOption() != 0) {
        if (m_orient == wxHORIZONTAL) {
          size.x = stretchSize * item->GetOption();
        } else {
          size.y = stretchSize * item->GetOption();
        }
      }

      if (m_orient == wxHORIZONTAL) {
        m_minWidth += size.x;
        m_minHeight = std::max(m_minHeight, size.y);
      } else {
        m_minHeight += size.y;
        m_minWidth = std::max(m_minWidth, size.x);
      }

      if (item->GetOption() == 0) {
        if (m_orient == wxVERTICAL) {
          m_fixedHeight += size.y;
          m_fixedWidth = std::max(m_fixedWidth, size.x);
        } else {
          m_fixedWidth += size.x;
          m_fixedHeight = std::max(m_fixedHeight, size.y);
        }
      }
    }
  }

  return wxSize(m_minWidth, m_minHeight);
}

// --- wxStaticBoxSizer ---------------------------------------------------------------------------

wxStaticBoxSizer::wxStaticBoxSizer(wxStaticBox* const box, const int orient)
  : wxBoxSizer(orient)
{
  wxASSERT_MSG(box, wxT("wxStaticBoxSizer needs a static box"));
  m_staticBox = box;
}

namespace
{
  void GetStaticBoxBorders(wxStaticBox* const box, int* const borderTop, int* const borderOther)
  {
    // this has to be done platform by platform as there is no way to guess the thickness of a
    // wxStaticBox border
    if (box->GetLabel().IsEmpty()) {
      *borderTop = 5;
    } else {
      *borderTop = box->GetCharHeight();
    }
    *borderOther = 5;
  }
} // namespace

void wxStaticBoxSizer::RecalcSizes()
{
  int top_border, other_border;
  GetStaticBoxBorders(m_staticBox, &top_border, &other_border);

  m_staticBox->SetSize(m_position.x, m_position.y, m_size.x, m_size.y);

  const wxPoint old_pos(m_position);
  m_position.x += other_border;
  m_position.y += top_border;
  const wxSize old_size(m_size);
  m_size.x -= 2 * other_border;
  m_size.y -= top_border + other_border;

  wxBoxSizer::RecalcSizes();

  m_position = old_pos;
  m_size = old_size;
}

wxSize wxStaticBoxSizer::CalcMin()
{
  int top_border, other_border;
  GetStaticBoxBorders(m_staticBox, &top_border, &other_border);

  wxSize ret(wxBoxSizer::CalcMin());
  ret.x += 2 * other_border;
  ret.y += other_border + top_border;
  return ret;
}
