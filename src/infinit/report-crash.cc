#include <infinit/report-crash.hh>

#include <elle/log.hh>

ELLE_LOG_COMPONENT("CrashReporter");

#if INFINIT_ENABLE_CRASH_REPORTER

#include <crash_reporting/CrashReporter.hh>

#include <infinit/utility.hh> //canonical_folder, etc.

namespace infinit
{
  /// Crash reporter.
  crash_reporting::CrashReporter
  make_reporter()
  {
    auto const host = elle::os::getenv("INFINIT_CRASH_REPORT_HOST", beyond());
    auto const url = elle::sprintf("%s/crash/report", host);
    auto const dumps_path = canonical_folder(xdg_cache_home() / "crashes");
    ELLE_DEBUG("dump to %s", dumps_path);
    return {url, dumps_path, version_describe()};
  }

  /// Thread to send the crash reports (some might be pending from
  /// previous runs, saved on disk).
  std::unique_ptr<elle::reactor::Thread>
  make_reporter_thread()
  {
    ELLE_DEBUG("enabled");
    return std::make_unique<elle::reactor::Thread>("crash report", [] {
        auto&& cr = make_reporter();
        cr.upload_existing();
      });
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
