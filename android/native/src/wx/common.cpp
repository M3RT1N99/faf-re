// wx-lite (android/native/WX_LITE.md): wxObject, wxString's conversions, the geometry and the
// default values, colours and brushes, the window names and the validator of wxWindows 2.4.2
// (src/common/object.cpp, strconv.cpp, gdicmn.cpp, datacmn.cpp, valgen.cpp).

#include <Windows.h>

#include <algorithm>
#include <cwctype>

#include "Private.h"
#include "wx/brush.h"
#include "wx/colour.h"
#include "wx/gdicmn.h"
#include "wx/object.h"
#include "wx/string.h"
#include "wx/validate.h"
#include "wx/window.h"

wxObject::~wxObject() = default;

// --- Strings ------------------------------------------------------------------------------------

std::wstring wxMBConvUTF8::ToWide(const char* const text) const
{
  // An invalid sequence converts to nothing, as wxMBConvUTF8::MB2WC fails on it.
  std::wstring out;
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  while (*p != 0) {
    std::uint32_t c = *p++;
    int more = 0;
    if (c >= 0xF8 || (c >= 0x80 && c < 0xC0)) {
      return std::wstring();
    }
    if (c >= 0xF0) {
      c &= 0x07;
      more = 3;
    } else if (c >= 0xE0) {
      c &= 0x0F;
      more = 2;
    } else if (c >= 0xC0) {
      c &= 0x1F;
      more = 1;
    }
    for (; more > 0; --more) {
      if ((*p & 0xC0) != 0x80) {
        return std::wstring();
      }
      c = (c << 6) | (*p++ & 0x3F);
    }
    out += static_cast<wchar_t>(c);
  }
  return out;
}

std::string wxMBConvUTF8::FromWide(const wchar_t* text) const
{
  std::string out;
  for (; *text != 0; ++text) {
    const std::uint32_t c = static_cast<std::uint32_t>(*text);
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else if (c < 0x800) {
      out += static_cast<char>(0xC0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += static_cast<char>(0xE0 | (c >> 12));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (c >> 18));
      out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
  }
  return out;
}

wxMBConvUTF8 wxConvUTF8;

const wxChar* wxEmptyString = wxT("");

bool wxString::IsSameAs(const wxChar* const text, const bool caseSensitive) const
{
  const std::wstring other = text != nullptr ? text : L"";
  if (caseSensitive) {
    return mText == other;
  }
  return mText.size() == other.size()
      && std::equal(mText.begin(), mText.end(), other.begin(), [](const wchar_t a, const wchar_t b) {
           return std::towlower(static_cast<wint_t>(a)) == std::towlower(static_cast<wint_t>(b));
         });
}

bool wxString::IsNumber() const
{
  const wxChar* s = mText.c_str();
  if (*s == wxT('-') || *s == wxT('+')) {
    ++s;
  }
  for (; *s != wxT('\0'); ++s) {
    if (!std::iswdigit(static_cast<wint_t>(*s))) {
      return false;
    }
  }
  return true;
}

int wxString::Find(const wxChar ch, const bool fromEnd) const
{
  // wxStrchr finds the terminating NUL as well.
  if (ch == wxT('\0')) {
    return static_cast<int>(mText.size());
  }
  const std::size_t position = fromEnd ? mText.rfind(ch) : mText.find(ch);
  return position == std::wstring::npos ? wxNOT_FOUND : static_cast<int>(position);
}

wxString wxString::Mid(const std::size_t first, std::size_t count) const
{
  // wxString::Mid's bounds, which also hold for a `first` of (size_t)wxNOT_FOUND.
  const std::size_t length = mText.size();
  if (count == wxSTRING_MAXLEN) {
    count = length - first;
  }
  if (first + count > length) {
    count = length - first;
  }
  if (first > length) {
    count = 0;
  }
  return count == 0 ? wxString() : wxString(mText.substr(first, count));
}

// --- Geometry -----------------------------------------------------------------------------------

const wxSize wxDefaultSize(-1, -1);
const wxPoint wxDefaultPosition(-1, -1);

wxRect::wxRect(const wxPoint& topLeft, const wxPoint& bottomRight)
{
  x = topLeft.x;
  y = topLeft.y;
  width = bottomRight.x - topLeft.x + 1;
  height = bottomRight.y - topLeft.y + 1;
  if (width < 0) {
    width = -width;
    x -= width;
  }
  if (height < 0) {
    height = -height;
    y -= height;
  }
}

bool wxRect::operator==(const wxRect& rect) const
{
  return x == rect.x && y == rect.y && width == rect.width && height == rect.height;
}

wxRect& wxRect::Inflate(const wxCoord dx, const wxCoord dy)
{
  x -= dx;
  y -= dy;
  width += 2 * dx;
  height += 2 * dy;
  // check that we didn't make the rectangle invalid
  if (width < 0) {
    // change the width to be 0, but don't move the rectangle
    x += width / 2;
    width = 0;
  }
  if (height < 0) {
    y += height / 2;
    height = 0;
  }
  return *this;
}

bool wxRect::Inside(const int cx, const int cy) const
{
  return (cx >= x) && (cy >= y) && ((cy - y) < height) && ((cx - x) < width);
}

wxRect& wxRect::Intersect(const wxRect& rect)
{
  int x2 = GetRight();
  int y2 = GetBottom();
  if (x < rect.x) {
    x = rect.x;
  }
  if (y < rect.y) {
    y = rect.y;
  }
  if (x2 > rect.GetRight()) {
    x2 = rect.GetRight();
  }
  if (y2 > rect.GetBottom()) {
    y2 = rect.GetBottom();
  }
  width = x2 - x + 1;
  height = y2 - y + 1;
  if (width <= 0 || height <= 0) {
    width = 0;
    height = 0;
  }
  return *this;
}

bool wxRect::Intersects(const wxRect& rect) const
{
  wxRect r = *this;
  r.Intersect(rect);
  // if there is no intersection, both width and height are 0
  return r.width != 0;
}

wxPoint wxGetMousePosition()
{
  POINT point{};
  (void)::GetCursorPos(&point);
  return wxPoint(point.x, point.y);
}

void wxGetMousePosition(int* const x, int* const y)
{
  const wxPoint point = wxGetMousePosition();
  if (x != nullptr) {
    *x = point.x;
  }
  if (y != nullptr) {
    *y = point.y;
  }
}

wxSize wxlite::TextExtent(const wxString& text)
{
  int lines = 1;
  int longest = 0;
  int current = 0;
  for (std::size_t i = 0; i < text.Len(); ++i) {
    if (text[i] == L'\n') {
      ++lines;
      longest = std::max(longest, current);
      current = 0;
    } else {
      ++current;
    }
  }
  longest = std::max(longest, current);
  return wxSize(longest * kCharWidth, lines * kCharHeight);
}

// --- Colours, brushes, stock objects -------------------------------------------------------------

wxColour wxNullColour;
wxBrush wxNullBrush;
// Made by wxApp::Initialize and deleted by wxApp::CleanUp, as wxWindows' stock objects are.
wxBrush* wxBLACK_BRUSH = nullptr;
wxBrush* wxWHITE_BRUSH = nullptr;
wxBrush* wxTRANSPARENT_BRUSH = nullptr;

// --- Window names --------------------------------------------------------------------------------

const wxChar* wxControlNameStr = wxT("control");
const wxChar* wxButtonNameStr = wxT("button");
const wxChar* wxCheckBoxNameStr = wxT("check");
const wxChar* wxDialogNameStr = wxT("dialog");
const wxChar* wxFrameNameStr = wxT("frame");
const wxChar* wxStaticBoxNameStr = wxT("groupBox");
const wxChar* wxStaticTextNameStr = wxT("message");
const wxChar* wxPanelNameStr = wxT("panel");
const wxChar* wxTextCtrlNameStr = wxT("text");

// --- Validators ----------------------------------------------------------------------------------

wxValidator::wxValidator()
  : m_validatorWindow(nullptr)
{}

wxValidator::~wxValidator() = default;

wxWindow* wxValidator::GetWindow() const
{
  return static_cast<wxWindow*>(m_validatorWindow);
}

const wxValidator wxDefaultValidator;
