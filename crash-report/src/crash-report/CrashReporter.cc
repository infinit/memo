#include <crash-report/CrashReporter.hh>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/range/algorithm/count_if.hpp>

#include <elle/algorithm.hh>
#include <elle/filesystem.hh>
#include <elle/format/base64.hh>
#include <elle/json/json.hh>
#include <elle/log.hh>
#include <elle/os/environ.hh>
#include <elle/reactor/http/Request.hh>
#include <elle/reactor/http/exceptions.hh>
#include <elle/system/platform.hh>
#include <elle/system/user_paths.hh>

#if defined MEMO_LINUX
# include <client/linux/handler/exception_handler.h>
#elif defined MEMO_MACOSX
# include <crash-report/gcc_fix.hh>
// FIXME: Adding `pragma GCC diagnostic ignored "-Wdeprecated"` does not work
// for removing #import warnings.
# include <client/mac/handler/exception_handler.h>
#else
# error Unsupported platform.
#endif

ELLE_LOG_COMPONENT("CrashReporter");

namespace crash_report
{
  namespace
  {
    /// Typed callback invoked by breakpad once the minidump saved.
    void
    dump(CrashReporter& report, std::string const& base)
    {
      if (report.make_payload)
        report.make_payload(base);
    }

#if defined MEMO_LINUX
    /// Untyped callback invoked by breakpad once the minidump saved.
    bool
    dump_callback(const breakpad::MinidumpDescriptor& descriptor,
                  void* context,
                  bool succeeded)
    {
      std::string base = descriptor.path();
      if (boost::ends_with(base, ".dmp"))
      {
        base.substr(base.size() - 4);
        dump(*static_cast<CrashReporter*>(context), base);
        return succeeded;
      }
      else
      {
        ELLE_ERR("unexpected minidump extension: %s", base);
        return false;
      }
    }
#elif defined MEMO_MACOSX
    /// Untyped callback invoked by breakpad once the minidump saved.
    bool
    dump_callback(const char* dump_dir, const char* minidump_id,
                  void* context,
                  bool succeeded)
    {
      dump(*static_cast<CrashReporter*>(context),
           elle::print("{}/{}", dump_dir, minidump_id));
      return succeeded;
    }
#endif

    /// Prepare the exception handler.
    auto
    make_exception_handler(CrashReporter& report)
    {
      auto const& dumps_path = report.dumps_path().string();
#if defined MEMO_LINUX
      breakpad::MinidumpDescriptor descriptor(dumps_path);
      return
        std::make_unique<breakpad::ExceptionHandler>
        (descriptor,
         nullptr, // filter
         dump_callback,
         &report, // callback context
         true,    // install handler
         -1);     // server-fd.
#elif defined MEMO_MACOSX
      return
        std::make_unique<breakpad::ExceptionHandler>
        (dumps_path,
         nullptr, // filter
         dump_callback,
         &report, // callback context.
         true,    // install handler
         nullptr);
#endif
    }

    bool
    _is_crash_report(bfs::path const& path)
    {
      return bfs::is_regular_file(path) && path.extension().string() == ".dmp";
    }

    bool
    _is_crash_report(bfs::directory_entry const& p)
    {
      return _is_crash_report(p.path());
    }

    /// The content of this file, encoded in base64.
    std::string
    contents_base64(bfs::path const& path)
    {
      auto&& f = bfs::ifstream(path, std::ios_base::in | std::ios_base::binary);
      if (!f.good())
        elle::err("unable to read crash dump: %s", path);

      auto buf = elle::Buffer{};
      auto stream = elle::IOStream(buf.ostreambuf());
      auto&& base64_stream = elle::format::base64::Stream{stream};
      auto constexpr chunk_size = 16 * 1024;
      char chunk[chunk_size + 1];
      chunk[chunk_size] = 0;
      while (!f.eof())
      {
        f.read(chunk, chunk_size);
        base64_stream.write(chunk, chunk_size);
        base64_stream.flush();
      }
      return buf.string();
    }

    bool constexpr production_build
#ifdef MEMO_PRODUCTION_BUILD
      = true;
#else
      = false;
#endif
  }

  CrashReporter::CrashReporter(std::string crash_url,
                               bfs::path dumps_path,
                               std::string version)
    : _crash_url(std::move(crash_url))
    , _dumps_path(std::move(dumps_path))
    , _version(std::move(version))
  {
    using elle::os::getenv;
    if (getenv("MEMO_CRASH_REPORT", production_build))
    {
      this->_exception_handler = make_exception_handler(*this);
      ELLE_TRACE("crash handler started");
    }
  }

  CrashReporter::~CrashReporter() = default;

  int
  CrashReporter::crashes_pending_upload() const
  {
    auto res =
      boost::count_if(bfs::directory_iterator(this->_dumps_path),
                      [](auto const& p)
                      {
                        return _is_crash_report(p);
                      });
    ELLE_DEBUG("%s: %s %s awaiting upload",
               this, res, res == 1 ? "crash" : "crashes");
    return res;
  }

  void
  CrashReporter::_upload(bfs::path const& path) const
  {
    try
    {
      if (!boost::ends_with(path.string(), ".dmp"))
        elle::err("unexpected minidump extension: %s", path);
      auto content = elle::json::Object
        {
          {"dump", contents_base64(path)},
          {"platform", elle::system::platform::os_description()},
          // Beware that there is no reason for the version of memo
          // sending the report to be the same as the one that
          // generated the crash.
          {"version", this->_version},
        };
      // Check if there are other payloads to attach.  Remove "dmp"
      // from the minidump file name (i.e., ends with the period).
      auto const base = path.string().substr(0, path.string().size() - 3);
      for (auto const& p: bfs::directory_iterator(path.parent_path()))
        if (auto ext = elle::tail(p.path().string(), base))
          if (*ext != "dmp")
            // Put everything in base64.  Valid JSON requires that the
            // string is valid UTF-8, and it might always be the case.
            // In particular the logs may embed ANSI escapes for
            // colors, or simply include raw bytes.
            content[*ext] = contents_base64(p);
      ELLE_DEBUG("%s: uploading: %s", this, path);
      ELLE_DUMP("%s: content to upload: %s", this, content);
      namespace http = elle::reactor::http;
      auto r = http::Request(this->_crash_url,
                             http::Method::PUT, "application/json");
      elle::json::write(r, content);
      if (r.status() == http::StatusCode::OK)
      {
        ELLE_DUMP("%s: successfully uploaded: %s", this, path);
        for (auto const& p: bfs::directory_iterator(path.parent_path()))
          if (boost::starts_with(p.path().string(), base))
            {
              ELLE_DUMP("%s: removing uploaded file: %s", this, p);
              elle::try_remove(p);
            }
      }
      else
        ELLE_ERR("%s: unable to upload crash report (%s) to %s: %s",
                 this, path, this->_crash_url, r.status());
    }
    catch (elle::Error const& e)
    {
      ELLE_ERR("%s: unable to upload crash report: %s", e);
    }
  }

  void
  CrashReporter::upload_existing() const
  {
    for (auto const& p: bfs::directory_iterator(this->_dumps_path))
      if (_is_crash_report(p))
        try
        {
          this->_upload(p.path());
        }
        catch (elle::reactor::http::RequestError const& e)
        {
          ELLE_TRACE("%s: unable to complete upload of %s: %s", this, p.path(), e);
        }
      else
        ELLE_DUMP("%s: file is not crash dump: %s", this, p.path());
  }
}
