#pragma once

#include <memory>
#include <string>

#include <boost/filesystem.hpp>

#include <elle/attribute.hh>

#include <crash-report/fwd.hh>

namespace crash_report
{
  namespace bfs = boost::filesystem;
  namespace breakpad = google_breakpad;

  /// Deal with crash reports: their generation, and their uploading.
  class CrashReporter
  {
  public:
    /// If production build, install the crash handler.
    ///
    /// @param dumps_path directory where to find the dumps to save/send.
    CrashReporter(bfs::path dumps_path);
    ~CrashReporter();

    /// Upload the existing crash reports (and possible associated payloads).
    void
    upload_existing() const;

    /// The number of minidumps waiting to be sent.
    int
    crashes_pending_upload() const;

    /// Generate a minidump now, and resume normal execution.
    void write_minidump() const;

    /// This function is called when a minidump was saved.  Use it to
    /// add payload when the minidump will be uploaded.  This payload
    /// must be a file.  Its name must be based on `base`, the
    /// argument, which does not include the final period.  So for
    /// instance create `base.log`, `base.env`, etc.  FWIW, the file
    /// name of the minidump is `base.dmp`.
    using MakePayload = std::function<auto (std::string const& base) -> void>;
    MakePayload make_payload;

  private:
    void _upload(bfs::path const& path) const noexcept;
    ELLE_ATTRIBUTE(std::string, crash_url);
    ELLE_ATTRIBUTE(std::unique_ptr<breakpad::ExceptionHandler>, exception_handler);
    ELLE_ATTRIBUTE_R(bfs::path, dumps_path);
    ELLE_ATTRIBUTE(std::string, version);
  };
}
