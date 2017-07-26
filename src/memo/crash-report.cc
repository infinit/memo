#include <memo/crash-report.hh>

#include <elle/assert.hh>
#include <elle/log.hh>

ELLE_LOG_COMPONENT("memo.crash-report");



#if MEMO_ENABLE_CRASH_REPORT

#include <string>

#include <elle/algorithm.hh>
#include <elle/assert.hh>
#include <elle/bytes.hh>
#include <elle/log.hh>
#include <elle/log/FileLogger.hh>

#include <crash-report/CrashReporter.hh>

#include <memo/log.hh>
#include <memo/utility.hh> //canonical_folder, etc.

namespace memo
{
  using CrashReporter = crash_report::CrashReporter;

  /// Crash reporter.
  auto
  make_reporter()
  {
    auto const dumps_path = canonical_folder(xdg_cache_home() / "crashes");
    ELLE_DEBUG("dump to %s", dumps_path);

    // FIXME: Should be unique_ptr, but something in our handling of
    // reactor::Threads prevents it.
    auto res = std::make_shared<CrashReporter>(dumps_path);
    // Attach the logs to the crash dump.
    res->make_payload = [](auto const& base)
      {
        tar_logs(elle::print("{}.log.tgz", base),
                 "main", 2);
      };

    // Hook elle::assert to generate the minidumps when an assertion
    // is broken.  We do not want to die immediately though, as we
    // might have some clean up to do.
    elle::on_abort([res](auto const& error) {
        res->write_minidump();
      });
    return res;
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

#else // ! MEMO_ENABLE_CRASH_REPORT

namespace memo
{
  std::unique_ptr<elle::reactor::Thread>
  make_reporter_thread()
  {
    ELLE_DEBUG("disabled");
    return {};
  }
}
#endif
