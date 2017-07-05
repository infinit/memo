#include <crash-report/CrashReporter.hh>

#include <elle/format/base64.hh>
#include <elle/json/json.hh>
#include <elle/log.hh>
#include <elle/os/environ.hh>
#include <elle/system/platform.hh>
#include <elle/system/user_paths.hh>

#include <boost/filesystem/fstream.hpp>
#include <boost/range/algorithm/count_if.hpp>

#include <elle/reactor/http/exceptions.hh>
#include <elle/reactor/http/Request.hh>

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
    /// Called by breakpad once the minidump saved.
#if defined MEMO_LINUX
    bool
    dump_callback(const breakpad::MinidumpDescriptor& descriptor,
                  void* context,
                  bool succeeded)
#elif defined MEMO_MACOSX
    bool
    dump_callback(const char* dump_dir,
                  const char* minidump_id,
                  void* context,
                  bool succeeded)
#endif
    {
      auto& report = *static_cast<CrashReporter*>(context);
      if (report.make_payload)
        report.make_payload(elle::print("{}/{}", dump_dir, minidump_id));
      return succeeded;
    }

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

    void
    _remove_file(bfs::path const& path)
    {
      boost::system::error_code erc;
      bfs::remove(path, erc);
      if (erc)
        ELLE_WARN("unable to remove crash dump (%s): %s", path, erc.message());
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
               this, res, (res == 1 ? "crash" : "crashes"));
    return res;
  }

  void
  CrashReporter::_upload(bfs::path const& path) const
  {
    auto&& f = bfs::ifstream(path, std::ios_base::in | std::ios_base::binary);
    if (!f.good())
    {
      ELLE_ERR("%s: unable to read crash dump: %s", this, path);
      return;
    }
    // The content of `path`, in base 64.
    auto const minidump = [&f]
      {
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
      }();
    // There is no reason for the version sending the report to be the
    // same as the one that generated the crash.
    auto content = elle::json::Object
      {
        {"dump", minidump},
        {"platform", elle::system::platform::os_description()},
        {"version", this->_version},
      };
    ELLE_DEBUG("%s: uploading: %s", this, path);
    ELLE_DUMP("%s: content to upload: %s", this, content);
    auto r = elle::reactor::http::Request(this->_crash_url,
                                          elle::reactor::http::Method::PUT,
                                          "application/json");
    elle::json::write(r, content);
    if (r.status() == elle::reactor::http::StatusCode::OK)
    {
      ELLE_DUMP("%s: removing uploaded crash dump: %s", this, path);
      f.close();
      _remove_file(path);
    }
    else
      ELLE_ERR("%s: unable to upload crash report (%s) to %s: %s",
               this, path, this->_crash_url, r.status());
  }

  void
  CrashReporter::upload_existing() const
  {
    for (auto const& p: bfs::directory_iterator(this->_dumps_path))
      if (_is_crash_report(p))
        try
        {
          _upload(p.path());
        }
        catch (elle::reactor::http::RequestError const& e)
        {
          ELLE_TRACE("%s: unable to complete upload of %s: %s", this, p.path(), e);
        }
      else
        ELLE_DUMP("%s: file is not crash dump: %s", this, p.path());
  }
}
