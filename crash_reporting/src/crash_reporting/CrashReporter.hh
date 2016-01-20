#ifndef CRASH_REPORTING_CRASH_REPORTER_HH
# define CRASH_REPORTING_CRASH_REPORTER_HH

#include <string>

#include <boost/filesystem.hpp>

#include <crash_reporting/fwd.hh>

namespace crash_reporting
{
  class CrashReporter
  {
  public:
    CrashReporter(std::string crash_url);
    ~CrashReporter();

    bool
    enabled() const;
    void
    upload_existing();
    int32_t
    crashes_pending_upload();

  private:
    boost::filesystem::path
    _get_dump_path();

    std::string _crash_url;
    boost::filesystem::path _dump_path;
    bool _enabled;
    google_breakpad::ExceptionHandler* _exception_handler;
  };
}

#endif
