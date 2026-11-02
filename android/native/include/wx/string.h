#pragma once

// wx-lite (android/native/WX_LITE.md): wxString of wxWindows 2.4.2's Unicode build, for what the
// engine uses, on std::wstring (wchar_t is 32 bits on Android, UTF-32, where Windows' is UTF-16),
// and the UTF-8 converter the engine builds strings from (wxConvUTF8).

#include <climits>
#include <cstddef>
#include <string>

#include "wx/defs.h"

/// The length that means "to the end".
constexpr unsigned int wxSTRING_MAXLEN = UINT_MAX - 100;

/// A multibyte converter (wx/strconv.h). The engine names only wxConvUTF8.
class wxMBConv
{
public:
  virtual ~wxMBConv() = default;
  /// The wide form of the NUL-terminated `text`.
  [[nodiscard]] virtual std::wstring ToWide(const char* text) const = 0;
  /// The multibyte form of the NUL-terminated `text`.
  [[nodiscard]] virtual std::string FromWide(const wchar_t* text) const = 0;
};

class wxMBConvUTF8 final : public wxMBConv
{
public:
  [[nodiscard]] std::wstring ToWide(const char* text) const override;
  [[nodiscard]] std::string FromWide(const wchar_t* text) const override;
};

extern wxMBConvUTF8 wxConvUTF8;

class wxString
{
public:
  wxString() = default;
  wxString(const wxChar* text)
    : mText(text != nullptr ? text : L"")
  {}
  wxString(const wxChar* text, std::size_t length)
    : mText(text != nullptr ? std::wstring(text, length) : std::wstring())
  {}
  wxString(const char* text, const wxMBConv& conv)
    : mText(text != nullptr ? conv.ToWide(text) : std::wstring())
  {}
  wxString(wxChar ch, std::size_t repeat = 1)
    : mText(repeat, ch)
  {}
  explicit wxString(std::wstring text)
    : mText(std::move(text))
  {}

  [[nodiscard]] const wxChar* c_str() const noexcept
  {
    return mText.c_str();
  }
  operator const wxChar*() const noexcept
  {
    return mText.c_str();
  }
  /// The multibyte form, in a std::string that holds it (wxWindows returns a buffer object).
  [[nodiscard]] std::string mb_str(const wxMBConv& conv = wxConvUTF8) const
  {
    return conv.FromWide(mText.c_str());
  }

  [[nodiscard]] std::size_t Len() const noexcept
  {
    return mText.size();
  }
  [[nodiscard]] std::size_t Length() const noexcept
  {
    return mText.size();
  }
  [[nodiscard]] std::size_t length() const noexcept
  {
    return mText.size();
  }
  [[nodiscard]] bool IsEmpty() const noexcept
  {
    return mText.empty();
  }
  [[nodiscard]] bool IsNull() const noexcept
  {
    return mText.empty();
  }
  /// True for digits after an optional sign, and for an empty string.
  [[nodiscard]] bool IsNumber() const;

  /// The character at `index`: the terminating NUL at the length. wxWindows only checks in a
  /// debug build that `index` is not past the length and reads beyond the end otherwise; here
  /// that is the NUL as well.
  [[nodiscard]] wxChar GetChar(std::size_t index) const noexcept
  {
    return index < mText.size() ? mText[index] : wxT('\0');
  }
  /// The position of the first (`fromEnd`: the last) `ch`, or wxNOT_FOUND.
  [[nodiscard]] int Find(wxChar ch, bool fromEnd = false) const;
  /// `count` characters from `first`, as many as there are: empty when `first` is past the end.
  [[nodiscard]] wxString Mid(std::size_t first, std::size_t count = wxSTRING_MAXLEN) const;
  [[nodiscard]] wxString operator()(std::size_t start, std::size_t count) const
  {
    return Mid(start, count);
  }
  [[nodiscard]] bool empty() const noexcept
  {
    return mText.empty();
  }
  void Empty()
  {
    mText.clear();
  }
  void Clear()
  {
    mText.clear();
  }

  wxString& operator=(const wxChar* text)
  {
    mText = text != nullptr ? text : L"";
    return *this;
  }
  wxString& operator+=(const wxString& text)
  {
    mText += text.mText;
    return *this;
  }
  wxString& operator+=(const wxChar* text)
  {
    mText += text != nullptr ? text : L"";
    return *this;
  }
  wxString& operator+=(wxChar ch)
  {
    mText += ch;
    return *this;
  }
  wxString& operator<<(const wxString& text)
  {
    return *this += text;
  }

  [[nodiscard]] wxChar operator[](std::size_t index) const
  {
    return mText[index];
  }
  [[nodiscard]] int Cmp(const wxChar* text) const
  {
    return mText.compare(text != nullptr ? text : L"");
  }
  [[nodiscard]] bool IsSameAs(const wxChar* text, bool caseSensitive = true) const;

  [[nodiscard]] const std::wstring& Wide() const noexcept
  {
    return mText;
  }

  friend bool operator==(const wxString& a, const wxString& b)
  {
    return a.mText == b.mText;
  }
  friend bool operator!=(const wxString& a, const wxString& b)
  {
    return a.mText != b.mText;
  }
  // As wxWindows has them: without these, a string and a wxChar* would compare as pointers too.
  friend bool operator==(const wxString& a, const wxChar* b)
  {
    return a.Cmp(b) == 0;
  }
  friend bool operator==(const wxChar* a, const wxString& b)
  {
    return b.Cmp(a) == 0;
  }
  friend bool operator!=(const wxString& a, const wxChar* b)
  {
    return a.Cmp(b) != 0;
  }
  friend bool operator!=(const wxChar* a, const wxString& b)
  {
    return b.Cmp(a) != 0;
  }
  friend wxString operator+(const wxString& a, const wxString& b)
  {
    return wxString(a.mText + b.mText);
  }

private:
  std::wstring mText;
};

extern const wxChar* wxEmptyString;
