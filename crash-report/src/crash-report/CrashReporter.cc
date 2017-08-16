#include <crash-report/CrashReporter.hh>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/range/algorithm/count_if.hpp>

#include <elle/Error.hh>
#include <elle/algorithm.hh>
#include <elle/filesystem.hh>
#include <elle/log.hh>
#include <elle/os/environ.hh>
#include <elle/printf.hh> // for elle::err

#if defined ELLE_LINUX
# include <client/linux/handler/exception_handler.h>
#elif defined ELLE_MACOS
# include <crash-report/gcc_fix.hh>
// FIXME: Adding `pragma GCC diagnostic ignored "-Wdeprecated"` does not work
// for removing #import warnings.
# include <client/mac/handler/exception_handler.h>
#else
# error Unsupported platform.
#endif

// FIXME: nasty dependency: we should not include things from src/memo
// here.
#include <memo/Hub.hh>

ELLE_LOG_COMPONENT("crash-report");

namespace crash_report
{
  namespace
  {
    /// Typed callback invoked by breakpad once the minidump saved.
    void
    dump(CrashReporter& report, std::string const& base)
    {
      ELLE_TRACE("saved crash-report: {}", base);
      if (report.make_payload)
        report.make_payload(base);
    }

#if defined ELLE_LINUX
    /// Untyped callback invoked by breakpad once the minidump saved.
    bool
    dump_callback(const breakpad::MinidumpDescriptor& descriptor,
                  void* context,
                  bool succeeded)
    {
      std::string base = descriptor.path();
      if (boost::ends_with(base, ".dmp"))
      {
        base.resize(base.size() - 4);
        dump(*static_cast<CrashReporter*>(context), base);
        return succeeded;
      }
      else
      {
        ELLE_ERR("unexpected minidump extension: %s", base);
        return false;
      }
    }
#elif defined ELLE_MACOS
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
#if defined ELLE_LINUX
      breakpad::MinidumpDescriptor descriptor(dumps_path);
      return
        std::make_unique<breakpad::ExceptionHandler>
        (descriptor,
         nullptr, // filter
         dump_callback,
         &report, // callback context
         true,    // install handler
         -1);     // server-fd.
#elif defined ELLE_MACOS
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

    bool constexpr production_build
#ifdef MEMO_PRODUCTION_BUILD
      = true;
#else
      = false;
#endif
  }

  CrashReporter::CrashReporter(bfs::path dumps_path)
    : _dumps_path(std::move(dumps_path))
  {
    if (elle::os::getenv("MEMO_CRASH_REPORT", production_build))
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
  CrashReporter::write_minidump() const
  {
    if (auto& h = this->_exception_handler)
      h->WriteMinidump();
  }

  void
  CrashReporter::_upload(bfs::path const& path) const noexcept
  {
    try
    {
      if (!boost::ends_with(path.string(), ".dmp"))
        elle::err("unexpected minidump extension: %s", path);
      // Every file sharing the same basename (without extension) as `path`.
      // Including `path`.
      auto files = [&path]
        {
          // Remove "dmp" from the minidump file name (i.e., `base` ends
          // with a period).
          auto const base = path.string().substr(0, path.string().size() - 3);
          auto res = std::unordered_map<std::string, bfs::path>{};
          for (auto const& p: bfs::directory_iterator(path.parent_path()))
            if (auto ext = elle::tail(p.path().string(), base))
              res[*ext] = p;
          return res;
        }();
      ELLE_DEBUG("%s: uploading: %s (%s)", this, path, files);
      if (memo::Hub::upload_crash(files))
        for (auto f: files)
        {
          ELLE_DUMP("%s: removing uploaded file: %s: %s", this,
                    f.first, f.second);
          elle::try_remove(f.second);
        }
      else
        ELLE_ERR("%s: unable to upload crash report (%s)", this, path);
    }
    catch (elle::Error const& e)
    {
      ELLE_ERR("%s: unable to upload crash report: %s", this, e);
    }
  }

  void
  CrashReporter::upload_existing() const
  {
    for (auto const& p: bfs::directory_iterator(this->_dumps_path))
      if (_is_crash_report(p))
        this->_upload(p.path());
      else
        ELLE_DUMP("%s: file is not crash dump: %s", this, p.path());
  }
}
