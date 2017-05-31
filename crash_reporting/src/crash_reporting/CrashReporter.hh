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

  /// Deal with crash reports: their generation, and their uploading.
  class CrashReporter
  {
  public:
    /// If production build, install the crash handler.
    ///
    /// @param crash_url  where to upload the crash reports.
    /// @param dumps_path directory where to find the dumps to save/send.
    /// @param version   of the program currently running.
    CrashReporter(std::string crash_url,
                  bfs::path dumps_path,
                  std::string version);
    ~CrashReporter();

    /// Upload the existing crash reports.
    void
    upload_existing() const;

    /// The number of minidumps waiting to be sent.
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
