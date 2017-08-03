#include <memo/Hub.hh>

#include <elle/das/serializer.hh>
#include <elle/format/base64.hh>
#include <elle/fstream.hh>
#include <elle/json/json.hh>
#include <elle/log.hh>
#include <elle/reactor/http/Request.hh>
#include <elle/reactor/http/exceptions.hh>
#include <elle/system/platform.hh>
#include <elle/system/user_paths.hh>

#include <memo/utility.hh>

ELLE_LOG_COMPONENT("memo.Hub");

namespace http = elle::reactor::http;

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

    /// Where crash reports must be sent.  Merely prepends `{beyond}/`.
    template <typename... Args>
    auto
    report_url(Args&&... args)
    {
      auto const host = memo::getenv("CRASH_REPORT_HOST", beyond());
      return elle::print("%s/%s",
                         host,
                         elle::print(std::forward<Args>(args)...));
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

    auto const url = report_url("crash/report");
    ELLE_DUMP("uploading %s to %s", fs, url);
    // It can extremely long to checkout the debug-symbols on the
    // server side.  Of course it should be asynchronous, but
    // currently it is not.  So cut it some five minutes of slack.
    auto r = http::Request(url, http::Method::PUT, "application/json",
                           {5min});
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

  namespace
  {
    namespace s
    {
      ELLE_DAS_SYMBOL(url);
      ELLE_DAS_SYMBOL(path);
    }

    struct Body
    {
      std::string url;
      std::string path;

      using Model = elle::das::Model<
        Body,
        decltype(elle::meta::list(s::url, s::path))>;
    };

  }
}

ELLE_DAS_SERIALIZE(memo::Body);

namespace memo
{
  bool
  Hub::upload_log(std::string const& username, bfs::path const& log)
  {
    ELLE_DEBUG("get upload url");
    auto const url_path = [&] {
      auto const url = report_url("log/{}/get_url", username);
      auto r = http::Request(url, http::Method::GET, "application/json",
                             {1min});
      r.wait();
      if (r.status() != http::StatusCode::OK)
        elle::err("failed to get upload url: %s", r.status());
      auto in = elle::serialization::json::SerializerIn(elle::json::read(r),
                                                        false);
      return in.deserialize<Body>();
    }();
    ELLE_DUMP("upload url: %s", url_path.url);

    // FIXME: don't load it in RAM, work with the stream.
    auto const payload = elle::content(bfs::ifstream{log});
    auto const conf = [&]
      {
        auto const end_size = payload.size();
        auto const position = 0;
        auto const end = end_size - 1;

        auto res = http::Request::Configuration{};
        res.stall_timeout(30s);
        res.timeout(elle::DurationOpt());
        res.header_add("Content-Length", std::to_string(end_size));
        res.header_add("Content-Range",
                       elle::print("bytes %s-%s/%s", position, end, end_size));
        return res;
      }();
    auto r = http::Request(url_path.url,
                           http::Method::PUT,
                           "application/octet-stream",
                           conf);
    r.write(payload.data(), payload.size());
    r.finalize();
    r.wait();
    ELLE_LOG("request: %s", r);
    ELLE_LOG("status code: %s", r.status());
    return true;
  }
}
