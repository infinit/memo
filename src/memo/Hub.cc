#include <memo/Hub.hh>

#include <elle/format/base64.hh>
#include <elle/json/json.hh>
#include <elle/log.hh>
#include <elle/reactor/http/Request.hh>
#include <elle/reactor/http/exceptions.hh>
#include <elle/system/platform.hh>
#include <elle/system/user_paths.hh>

#include <memo/utility.hh>

ELLE_LOG_COMPONENT("memo.Hub");

namespace memo
{
  namespace
  {
    /// The content of this file, encoded in base64.
    std::string
    contents_base64(bfs::path const& path)
    {
      auto&& f = bfs::ifstream(path, std::ios_base::in | std::ios_base::binary);
      if (!f.good())
        elle::err("unable to read crash dump: %s", path);

      auto buf = elle::Buffer{};
      auto stream = elle::IOStream(buf.ostreambuf());
      auto&& b64 = elle::format::base64::Stream{stream};
      auto constexpr chunk_size = 16 * 1024;
      char chunk[chunk_size + 1];
      chunk[chunk_size] = 0;
      while (!f.eof())
      {
        f.read(chunk, chunk_size);
        b64.write(chunk, chunk_size);
      }
      b64.flush();
      return buf.string();
    }
  }

  bool
  Hub::upload_crash(Files const& fs)
  {
    ELLE_DEBUG("uploading %s", fs);
    auto content = elle::json::Object
      {
        {"platform", elle::system::platform::os_description()},
        // There is no reason for the version of memo sending the
        // report to be the same as the one that generated the crash.
        // So the title of the mail will be misleading.
        {"version", version_describe()},
      };
    for (auto const& f: fs)
      // Put everything in base64.  Valid JSON requires that the
      // string is valid UTF-8, and it might not always be the case.
      // In particular the logs may embed ANSI escapes for colors, or
      // simply include raw bytes, not to mention binary data such as
      // the minidumps.
      content[f.first] = contents_base64(f.second);

    auto const host = memo::getenv("CRASH_REPORT_HOST", beyond());
    auto const url = elle::sprintf("%s/crash/report", host);
    ELLE_DUMP("uploading %s to %s", fs, url);
    namespace http = elle::reactor::http;
    auto r = http::Request(url, http::Method::PUT, "application/json");
    elle::json::write(r, content);
    if (r.status() == http::StatusCode::OK)
    {
      ELLE_DUMP("successfully uploaded: %s", fs);
      return true;
    }
    else
    {
      ELLE_ERR("failed to upload %s to %s: %s", fs, url, r.status());
      return false;
    }
  }
}
