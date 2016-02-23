#include <crash_reporting/CrashReporter.hh>

#include <elle/os/environ.hh>
#include <elle/log.hh>
#include <elle/system/platform.hh>
#include <elle/system/user_paths.hh>

#include <boost/filesystem/fstream.hpp>

#include <reactor/http/exceptions.hh>
#include <reactor/http/Request.hh>

#ifdef INFINIT_LINUX
# include <client/linux/handler/exception_handler.h>
#elif defined(INFINIT_MACOSX)
# include <crash_reporting/gcc_fix.hh>
// FIXME: Adding `pragma GCC diagnostic ignored "-Wdeprecated"` does not work
// for removing #import warnings.
# include <client/mac/handler/exception_handler.h>
#else
# error Unsupported platform.
#endif

ELLE_LOG_COMPONENT("CrashReporter");

namespace crash_reporting
{
#ifdef INFINIT_LINUX
  static
  bool
  dump_callback(const google_breakpad::MinidumpDescriptor& descriptor,
                void* context,
                bool success)
#elif INFINIT_MACOSX
  static
  bool
  dump_callback(const char* dump_dir,
                const char* minidump_id,
                void* context,
                bool success)
#endif
  {
    return success;
  }

  CrashReporter::CrashReporter(std::string crash_url,
                               boost::filesystem::path dumps_path)
    : _crash_url(std::move(crash_url))
    , _enabled(false)
    , _exception_handler(nullptr)
    , _dumps_path(std::move(dumps_path))
  {
#ifdef INFINIT_LINUX
    google_breakpad::MinidumpDescriptor descriptor(this->_dumps_path.string());
    this->_exception_handler =
      new google_breakpad::ExceptionHandler(descriptor,
                                            NULL,
                                            dump_callback,
                                            NULL,
                                            true,
                                            -1);
#elif defined(INFINIT_MACOSX)
    this->_exception_handler =
      new google_breakpad::ExceptionHandler(this->_dumps_path.string(),
                                            NULL,
                                            dump_callback,
                                            NULL,
                                            true,
                                            NULL);
#endif
    ELLE_TRACE("crash reporter started");
  }

  CrashReporter::~CrashReporter()
  {
    if (this->_exception_handler != nullptr)
      delete this->_exception_handler;
  }

  bool
  CrashReporter::enabled() const
  {
    return this->_enabled;
  }

  static
  bool
  _is_crash_report(boost::filesystem::path const& path)
  {
    namespace fs = boost::filesystem;
    if (fs::is_regular_file(path) && path.extension().string() == ".dmp")
      return true;
    return false;
  }

  static
  void
  _remove_file(boost::filesystem::path const& path)
  {
    boost::system::error_code erc;
    boost::filesystem::remove(path, erc);
    if (erc)
      ELLE_WARN("unable to remove crash dump (%s): %s", path, erc.message());
  }

  int32_t
  CrashReporter::crashes_pending_upload()
  {
    int32_t res = 0;
    namespace fs = boost::filesystem;
    for (fs::directory_iterator it(this->_dumps_path);
         it != fs::directory_iterator();
         ++it)
    {
      if (_is_crash_report(it->path()))
        res++;
    }
    ELLE_DEBUG("%s: %s %s awaiting upload",
               *this, res, (res == 1 ? "crash" : "crashes"));
    return res;
  }

  void
  CrashReporter::upload_existing()
  {
    if (!this->_enabled)
      return;
    namespace fs = boost::filesystem;
    for (fs::directory_iterator it(this->_dumps_path);
         it != fs::directory_iterator();
         ++it)
    {
      if (!_is_crash_report(it->path()))
      {
        ELLE_DUMP("%s: file is not crash dump: %s", *this, it->path());
        continue;
      }
      auto const& path = it->path();
      fs::ifstream f;
      f.open(path, std::ios_base::in | std::ios_base::binary);
      if (!f.good())
      {
        ELLE_ERR("%s: unable to read crash dump: %s", *this, path);
        continue;
      }
      try
      {
        ELLE_DEBUG("%s: uploading: %s", *this, path);
        reactor::http::Request r(this->_crash_url,
                                 reactor::http::Method::PUT,
                                 "application/octet-stream");
        std::istreambuf_iterator<char> end;
        std::copy(std::istreambuf_iterator<char>(f),
                  end,
                  std::ostreambuf_iterator<char>(r));
        if (r.status() == reactor::http::StatusCode::OK)
        {
          ELLE_DUMP("%s: removing uploaded crash dump: %s", *this, path);
          f.close();
          _remove_file(path);
        }
        else
        {
          ELLE_ERR("%s: unable to upload crash report (%s) to %s: %s",
                   *this, path, this->_crash_url, r.status());
        }
        if (f.is_open())
          f.close();
      }
      catch (reactor::http::RequestError const& e)
      {
        ELLE_TRACE("%s: unable to complete upload of %s: %s", *this, path, e);
      }
    }
  }
}
