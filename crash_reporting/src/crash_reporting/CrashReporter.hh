#ifndef CRASH_REPORTING_CRASH_REPORTER_HH
# define CRASH_REPORTING_CRASH_REPORTER_HH

#include <string>

#include <crash_reporting/fwd.hh>

namespace crash_reporting
{
  class CrashReporter
  {
  public:
    CrashReporter(std::string binary_name,
                  std::string dump_path);
    ~CrashReporter();

    void
    upload_existing();

  private:
    std::string _binary_name;
    std::string _dump_path;
    google_breakpad::ExceptionHandler* _exception_handler;
  };
}

#endif
