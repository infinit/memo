#include <infinit/report-crash.hh>

#include <elle/log.hh>

ELLE_LOG_COMPONENT("CrashReporter");

#if INFINIT_ENABLE_CRASH_REPORTER

#include <crash_reporting/CrashReporter.hh>

#include <infinit/utility.hh> //canonical_folder, etc.

namespace infinit
{
  using CrashReporter = crash_reporting::CrashReporter;

  /// Crash reporter.
  auto
  make_reporter()
  {
    auto const host = elle::os::getenv("INFINIT_CRASH_REPORT_HOST", beyond());
    auto const url = elle::sprintf("%s/crash/report", host);
    auto const dumps_path = canonical_folder(xdg_cache_home() / "crashes");
    ELLE_DEBUG("dump to %s", dumps_path);
    // FIXME: Should be unique_ptr, but something in our handling of
    // reactor::Threads prevents it.
    return std::make_shared<CrashReporter>(url, dumps_path, version_describe());
  }

  /// Thread to send the crash reports (some might be pending from
  /// previous runs, saved on disk).
  ///
  /// This routine also creates the CrashReporter object which is in
  /// charge of catching errors and generating the minidumps.  So be
  /// sure to always create this object early enough.
  ///
  /// It should never cause the program to fail, catch all errors.
  std::unique_ptr<elle::reactor::Thread>
  make_reporter_thread()
  {
    ELLE_DEBUG("enabled");
    try
    {
      // Don't create the error handler within the thread, it might not
      // like that, and might be created too late to catch very early crashes.
      return std::make_unique<elle::reactor::Thread>("crash report",
        [cr = make_reporter()]
        {
          cr->upload_existing();
        });
    }
    catch (elle::Error const& e)
    {
      ELLE_LOG("cannot set up crash-handler: %s", e);
      return {};
    }
    catch (...)
    {
      ELLE_LOG("cannot set up crash-handler: %s", elle::exception_string());
      return {};
    }
  }
}

#else // INFINIT_ENABLE_CRASH_REPORTER

namespace infinit
{
  std::unique_ptr<elle::reactor::Thread>
  make_reporter_thread()
  {
    ELLE_DEBUG("disabled");
    return {};
  }
}
#endif
