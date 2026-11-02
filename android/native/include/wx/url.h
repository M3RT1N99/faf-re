#pragma once

// wx-lite (android/native/WX_LITE.md): wxURL of wxWindows 2.4.2, for what the engine uses: the
// parse, its error and the protocol's name (moho/app/WxUrl.cpp opens a link only when the parse
// succeeds and its protocol is allowed). It knows wxWindows' three protocols, http, ftp and file.
// It makes no protocol objects, so there is no GetProtocol, GetInputStream or proxy: nothing in
// the engine reads a URL.

#include "wx/defs.h"
#include "wx/object.h"
#include "wx/string.h"

typedef enum
{
  wxURL_NOERR = 0,
  wxURL_SNTXERR,
  wxURL_NOPROTO,
  wxURL_NOHOST,
  wxURL_NOPATH,
  wxURL_CONNERR,
  wxURL_PROTOERR
} wxURLError;

/// A protocol wxURL knows (src/wx/url.cpp).
class wxProtoInfo;

class wxURL : public wxObject
{
public:
  wxURL(const wxString& url);
  ~wxURL() override;

  /// The protocol's name; only after a parse without an error.
  [[nodiscard]] wxString GetProtocolName() const;
  [[nodiscard]] wxString GetHostName() const
  {
    return m_hostname;
  }
  [[nodiscard]] wxString GetURL() const
  {
    return m_url;
  }
  [[nodiscard]] wxURLError GetError() const
  {
    return m_error;
  }
  [[nodiscard]] wxString GetPath() const
  {
    return m_path;
  }

  /// `uri` with its spaces as %20 and every character but letters, digits, the unreserved marks
  /// and `delims` as %XX.
  [[nodiscard]] static wxString ConvertToValidURI(const wxString& uri, const wxChar* delims = wxT(";/?:@&=+$,"));

protected:
  const wxProtoInfo* m_protoinfo;

  wxURLError m_error;
  wxString m_protoname, m_hostname, m_servname, m_path, m_url;
  wxString m_user, m_password;

  bool PrepProto(wxString& url);
  bool PrepHost(wxString& url);
  bool PrepPath(wxString& url);
  bool ParseURL();
  bool FetchProtocol();

  DECLARE_NO_COPY_CLASS(wxURL)
};
