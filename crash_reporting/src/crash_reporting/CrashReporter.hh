#pragma once

#include <string>

#include <boost/filesystem.hpp>

#include <crash_reporting/fwd.hh>

#include <elle/attribute.hh>

namespace crash_reporting
{
  namespace bfs = boost::filesystem;
  class CrashReporter
  {
  public:
    CrashReporter(std::string crash_url,
                  bfs::path dumps_path,
                  std::string version);
    ~CrashReporter();

    bool
    enabled() const;
    void
    upload_existing() const;
    int32_t
    crashes_pending_upload();

  private:
    void
    _upload(bfs::path const& path) const;
    std::string _crash_url;
    bool _enabled;
    google_breakpad::ExceptionHandler* _exception_handler;
    ELLE_ATTRIBUTE(boost::filesystem::path, dumps_path);
    ELLE_ATTRIBUTE(std::string, version);
  };
}
