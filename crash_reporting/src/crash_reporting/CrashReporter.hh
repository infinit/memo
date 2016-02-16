#ifndef CRASH_REPORTING_CRASH_REPORTER_HH
# define CRASH_REPORTING_CRASH_REPORTER_HH

# include <string>

# include <boost/filesystem.hpp>

# include <crash_reporting/fwd.hh>

# include <elle/attribute.hh>

namespace crash_reporting
{
  class CrashReporter
  {
  public:
    CrashReporter(std::string crash_url,
                  boost::filesystem::path dumps_path);
    ~CrashReporter();

    bool
    enabled() const;
    void
    upload_existing();
    int32_t
    crashes_pending_upload();

  private:
    std::string _crash_url;
    bool _enabled;
    google_breakpad::ExceptionHandler* _exception_handler;
    ELLE_ATTRIBUTE_R(boost::filesystem::path, dumps_path);
  };
}

#endif
