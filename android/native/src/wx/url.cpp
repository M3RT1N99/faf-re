// wx-lite (android/native/WX_LITE.md): wxURL's parse, as wxWindows 2.4.2's src/common/url.cpp
// has it, with the protocols its http.cpp, ftp.cpp and sckfile.cpp register (IMPLEMENT_PROTOCOL).

#include <cwchar>
#include <cwctype>

#include "wx/url.h"

/// wxProtoInfo without its class: a protocol's name, its default service, and whether its URLs
/// name a host.
class wxProtoInfo
{
public:
  const wxChar* m_protoname;
  const wxChar* m_servname;
  bool m_needhost;
};

namespace
{
  // wxWindows puts each protocol at the front of its list as it registers; the names differ, so
  // the order does not matter. The names compare with case.
  const wxProtoInfo kProtocols[] = {
    {wxT("http"), wxT("80"), true},
    {wxT("ftp"), wxT("ftp"), true},
    {wxT("file"), nullptr, false},
  };
} // namespace

wxURL::wxURL(const wxString& url)
  : m_protoinfo(nullptr)
  , m_error(wxURL_NOERR)
  , m_url(url)
{
  ParseURL();
}

wxURL::~wxURL() = default;

wxString wxURL::GetProtocolName() const
{
  return m_protoinfo->m_protoname;
}

bool wxURL::ParseURL()
{
  wxString last_url = m_url;

  // Extract protocol name
  if (!PrepProto(last_url)) {
    m_error = wxURL_SNTXERR;
    return false;
  }

  // Find the protocol
  if (!FetchProtocol()) {
    m_error = wxURL_NOPROTO;
    return false;
  }

  // Do we need a host name ?
  if (m_protoinfo->m_needhost) {
    // Extract it
    if (!PrepHost(last_url)) {
      m_error = wxURL_SNTXERR;
      return false;
    }
  }

  // Extract full path
  if (!PrepPath(last_url)) {
    m_error = wxURL_NOPATH;
    return false;
  }

  m_error = wxURL_NOERR;
  return true;
}

bool wxURL::PrepProto(wxString& url)
{
  // Find end
  const int pos = url.Find(wxT(':'));
  if (pos == wxNOT_FOUND) {
    return false;
  }

  m_protoname = url(0, pos);

  url = url(pos + 1, url.Length());

  return true;
}

bool wxURL::PrepHost(wxString& url)
{
  if ((url.GetChar(0) != wxT('/')) || (url.GetChar(1) != wxT('/'))) {
    return false;
  }

  url = url(2, url.Length());

  int pos = url.Find(wxT('/'));
  if (pos == wxNOT_FOUND) {
    pos = static_cast<int>(url.Length());
  }

  if (pos == 0) {
    return false;
  }

  wxString temp_url = url(0, pos);
  // Without a '/', wxNOT_FOUND starts past the end: the path is empty.
  url = url(static_cast<std::size_t>(url.Find(wxT('/'))), url.Length());

  // Retrieve service number
  int pos2 = temp_url.Find(wxT(':'), true);
  if (pos2 != wxNOT_FOUND && pos2 < pos) {
    m_servname = temp_url(pos2 + 1, pos);
    if (!m_servname.IsNumber()) {
      return false;
    }
    temp_url = temp_url(0, pos2);
  }

  // Retrieve user and password.
  pos2 = temp_url.Find(wxT('@'));
  // Even if pos2 equals -1, this code is right.
  m_hostname = temp_url(pos2 + 1, temp_url.Length());

  m_user = wxT("");
  m_password = wxT("");

  if (pos2 == wxNOT_FOUND) {
    return true;
  }

  temp_url = temp_url(0, pos2);
  pos2 = temp_url.Find(wxT(':'));

  if (pos2 == wxNOT_FOUND) {
    return false;
  }

  m_user = temp_url(0, pos2);
  m_password = temp_url(pos2 + 1, url.Length());

  return true;
}

bool wxURL::PrepPath(wxString& url)
{
  if (url.Length() != 0) {
    m_path = ConvertToValidURI(url);
  } else {
    m_path = wxT("/");
  }
  return true;
}

bool wxURL::FetchProtocol()
{
  for (const wxProtoInfo& info : kProtocols) {
    if (m_protoname == info.m_protoname) {
      if (m_servname.IsNull()) {
        m_servname = info.m_servname;
      }

      m_protoinfo = &info;
      return true;
    }
  }
  return false;
}

wxString wxURL::ConvertToValidURI(const wxString& uri, const wxChar* const delims)
{
  wxString out_str;

  for (std::size_t i = 0; i < uri.Len(); i++) {
    const wxChar c = uri.GetChar(i);

    if (c == wxT(' ')) {
      out_str += wxT("%20");
    } else {
      static const wxChar marks[] = wxT("-_.!~*()'");

      if (!std::iswalnum(static_cast<wint_t>(c)) && !std::wcschr(marks, c) && !std::wcschr(delims, c)) {
        // A character beyond the BMP is one wchar_t here, so one escape where wxMSW, on UTF-16,
        // writes two.
        wxChar hexa_code[16];
        (void)std::swprintf(hexa_code, sizeof(hexa_code) / sizeof(hexa_code[0]), wxT("%%%02X"), static_cast<unsigned int>(c));
        out_str += hexa_code;
      } else {
        out_str += c;
      }
    }
  }

  return out_str;
}
