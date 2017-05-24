#pragma once

#include <memory>
#include <string>

#include <boost/filesystem.hpp>

#include <crash_reporting/fwd.hh>

#include <elle/attribute.hh>

namespace crash_reporting
{
  namespace bfs = boost::filesystem;
  namespace breakpad = google_breakpad;

  class CrashReporter
  {
  public:
    CrashReporter(std::string crash_url,
                  bfs::path dumps_path,
                  std::string version);
    ~CrashReporter();

    void
    upload_existing() const;
    /// The number of minidump waiting to be sent.
    int
    crashes_pending_upload() const;

  private:
    void
    _upload(bfs::path const& path) const;
    ELLE_ATTRIBUTE(std::string, crash_url);
    ELLE_ATTRIBUTE(std::unique_ptr<breakpad::ExceptionHandler>, exception_handler);
    ELLE_ATTRIBUTE(bfs::path, dumps_path);
    ELLE_ATTRIBUTE(std::string, version);
  };
}
